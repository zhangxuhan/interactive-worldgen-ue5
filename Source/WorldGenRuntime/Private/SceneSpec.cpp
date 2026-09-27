#include "SceneSpec.h"
#include "WorldGenRuntime.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Misc/FileHelper.h"
#include "Misc/Char.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

// ---------------------------------------------------------------- helpers
// Note: FJsonValue::GetType() is protected in UE 5.7; the public API is
// IsNull() / TryGetString / TryGetNumber / TryGetBool / TryGetArray / TryGetObject.

namespace
{
	/** Allowed identifier charset for ids: [A-Za-z_][A-Za-z0-9_]*, max 64 chars. */
	bool IsValidIdentifier(const FString& S)
	{
		if (S.IsEmpty() || S.Len() > 64)
		{
			return false;
		}
		const TCHAR First = S[0];
		if (!(FChar::IsAlpha(First) || First == TEXT('_')))
		{
			return false;
		}
		for (const TCHAR C : S)
		{
			if (!(FChar::IsAlpha(C) || FChar::IsDigit(C) || C == TEXT('_')))
			{
				return false;
			}
		}
		return true;
	}

	bool IsValidWallName(const FString& S)
	{
		return S == TEXT("north") || S == TEXT("south") || S == TEXT("east") || S == TEXT("west");
	}

	/** Run-length of a wall: north/south run along Y (= room length), east/west along X (= room width). */
	double WallRunLength(const FString& Wall, const FSceneSpec& Spec)
	{
		return (Wall == TEXT("north") || Wall == TEXT("south")) ? Spec.RoomLengthCm : Spec.RoomWidthCm;
	}

	// --- typed JSON field readers; every failure appends a path-qualified error ---

	bool ReadString(const TSharedPtr<FJsonObject>& Obj, const FString& Path, const TCHAR* Field,
		FString& Out, TArray<FString>& Errors, bool bRequired, const TCHAR* Default = TEXT(""))
	{
		const TSharedPtr<FJsonValue> V = Obj->TryGetField(Field);
		if (!V.IsValid() || V->IsNull())
		{
			if (bRequired)
			{
				Errors.Add(FString::Printf(TEXT("%s%s: missing required field"), *Path, Field));
			}
			else
			{
				Out = Default;
			}
			return !bRequired;
		}
		FString S;
		if (!V->TryGetString(S))
		{
			Errors.Add(FString::Printf(TEXT("%s%s: must be a string"), *Path, Field));
			return false;
		}
		Out = S;
		return true;
	}

	bool ReadNumber(const TSharedPtr<FJsonObject>& Obj, const FString& Path, const TCHAR* Field,
		double& Out, TArray<FString>& Errors, bool bRequired, double Default = 0.0)
	{
		const TSharedPtr<FJsonValue> V = Obj->TryGetField(Field);
		if (!V.IsValid() || V->IsNull())
		{
			if (bRequired)
			{
				Errors.Add(FString::Printf(TEXT("%s%s: missing required field"), *Path, Field));
			}
			else
			{
				Out = Default;
			}
			return !bRequired;
		}
		double N = 0.0;
		if (!V->TryGetNumber(N) || !FMath::IsFinite(N))
		{
			Errors.Add(FString::Printf(TEXT("%s%s: must be a finite number"), *Path, Field));
			return false;
		}
		Out = N;
		return true;
	}

	bool ReadBool(const TSharedPtr<FJsonObject>& Obj, const FString& Path, const TCHAR* Field,
		bool& Out, TArray<FString>& Errors, bool bRequired, bool Default = false)
	{
		const TSharedPtr<FJsonValue> V = Obj->TryGetField(Field);
		if (!V.IsValid() || V->IsNull())
		{
			if (bRequired)
			{
				Errors.Add(FString::Printf(TEXT("%s%s: missing required field"), *Path, Field));
			}
			else
			{
				Out = Default;
			}
			return !bRequired;
		}
		bool B = false;
		if (!V->TryGetBool(B))
		{
			Errors.Add(FString::Printf(TEXT("%s%s: must be a boolean"), *Path, Field));
			return false;
		}
		Out = B;
		return true;
	}

	bool ReadRange(const TSharedPtr<FJsonObject>& Obj, const FString& Path, const TCHAR* Field,
		double& Out, double Min, double Max, TArray<FString>& Errors)
	{
		if (!ReadNumber(Obj, Path, Field, Out, Errors, true))
		{
			return false;
		}
		if (Out < Min || Out > Max)
		{
			Errors.Add(FString::Printf(TEXT("%s%s: value %.1f out of range [%.0f, %.0f]"),
				*Path, Field, Out, Min, Max));
			return false;
		}
		return true;
	}

	/** Returns the sub-object of Parent->Field when it is a JSON object, else reports an error. */
	TSharedPtr<FJsonObject> ReadObjectField(const TSharedPtr<FJsonObject>& Parent, const FString& Path,
		const TCHAR* Field, bool bRequired, TArray<FString>& Errors)
	{
		const TSharedPtr<FJsonValue> V = Parent->TryGetField(Field);
		if (!V.IsValid() || V->IsNull())
		{
			if (bRequired)
			{
				Errors.Add(FString::Printf(TEXT("%s%s: missing required field"), *Path, Field));
			}
			return nullptr;
		}
		const TSharedPtr<FJsonObject>* SubPtr = nullptr;
		if (!V->TryGetObject(SubPtr) || SubPtr == nullptr || !SubPtr->IsValid())
		{
			Errors.Add(FString::Printf(TEXT("%s%s: must be an object"), *Path, Field));
			return nullptr;
		}
		return *SubPtr;
	}

	/** Returns the array of Parent->Field when it is a JSON array, else reports an error. */
	bool ReadArrayField(const TSharedPtr<FJsonObject>& Parent, const FString& Path, const TCHAR* Field,
		const TArray<TSharedPtr<FJsonValue>>*& OutArr, TArray<FString>& Errors, bool bRequired)
	{
		const TSharedPtr<FJsonValue> V = Parent->TryGetField(Field);
		if (!V.IsValid() || V->IsNull())
		{
			if (bRequired)
			{
				Errors.Add(FString::Printf(TEXT("%s%s: missing required field"), *Path, Field));
			}
			return false;
		}
		if (!V->TryGetArray(OutArr))
		{
			Errors.Add(FString::Printf(TEXT("%s%s: must be an array"), *Path, Field));
			return false;
		}
		return true;
	}

	/**
	 * Entrance/exit door: required object { wall, offset_cm, width_cm [, locked] }.
	 * Checks wall name, width range, and that the door span stays on the wall run.
	 */
	void ValidateDoorObject(const TSharedRef<FJsonObject>& Root, const FSceneSpec& SpecSoFar,
		bool bEntrance, FSceneSpecDoor& OutDoor, TArray<FString>& Errors)
	{
		const TCHAR* FieldName = bEntrance ? TEXT("entrance") : TEXT("exit");
		const FString Path = FString::Printf(TEXT("%s."), FieldName);

		const TSharedPtr<FJsonObject> DoorObj = ReadObjectField(Root, TEXT(""), FieldName, true, Errors);
		if (!DoorObj.IsValid())
		{
			return;
		}

		if (ReadString(DoorObj, Path, TEXT("wall"), OutDoor.Wall, Errors, true))
		{
			if (!IsValidWallName(OutDoor.Wall))
			{
				Errors.Add(FString::Printf(TEXT("%swall: invalid wall \"%s\" (expected north|south|east|west)"), *Path, *OutDoor.Wall));
			}
		}

		ReadRange(DoorObj, Path, TEXT("width_cm"), OutDoor.WidthCm, 60.0, 400.0, Errors);

		if (ReadNumber(DoorObj, Path, TEXT("offset_cm"), OutDoor.OffsetCm, Errors, true))
		{
			const double Run = WallRunLength(OutDoor.Wall, SpecSoFar);
			if (Run > 0.0)
			{
				const double Half = OutDoor.WidthCm / 2.0;
				if (OutDoor.OffsetCm - Half < 0.0 || OutDoor.OffsetCm + Half > Run)
				{
					Errors.Add(FString::Printf(TEXT("%soffset_cm: door span [%.1f, %.1f] exceeds wall run %.1f"),
						*Path, OutDoor.OffsetCm - Half, OutDoor.OffsetCm + Half, Run));
				}
			}
		}

		if (!bEntrance)
		{
			ReadBool(DoorObj, Path, TEXT("locked"), OutDoor.bLocked, Errors, false, false);
		}
	}
}

// ---------------------------------------------------------------- FSceneSpec

FString FSceneSpec::MakeCanonicalSummary() const
{
	FString Out = FString::Printf(TEXT("scene=%s room=%.1fx%.1fx%.1f ent=%s@%.1f,w%.1f exit=%s@%.1f,w%.1f,locked=%d agent=r%.1f,h%.1f objects=%d objective=%s/%s"),
		*SceneId, RoomWidthCm, RoomLengthCm, RoomHeightCm,
		*Entrance.Wall, Entrance.OffsetCm, Entrance.WidthCm,
		*Exit.Wall, Exit.OffsetCm, Exit.WidthCm, Exit.bLocked ? 1 : 0,
		AgentRadiusCm, AgentHeightCm, Objects.Num(),
		*ObjectiveType, *ObjectiveItemId);
	for (const FSceneSpecObject& O : Objects)
	{
		Out += FString::Printf(TEXT(" | %s:%s:[%.1f,%.1f,%.1f]"), *O.Id, *O.SemanticType, O.SizeXCm, O.SizeYCm, O.SizeZCm);
		if (O.bHasPosition)
		{
			Out += FString::Printf(TEXT("@(%.1f,%.1f,rot%.0f)"), O.PositionXCm, O.PositionYCm, O.RotationDeg);
		}
		else if (O.bOnTopOf)
		{
			Out += FString::Printf(TEXT("@on(%s)"), *O.OnTopOfId);
		}
	}
	return Out;
}

// ---------------------------------------------------------------- Validate

bool FSceneSpecValidator::Validate(const FString& JsonText, FSceneSpec& OutSpec, TArray<FString>& OutErrors)
{
	OutSpec = FSceneSpec();
	OutErrors.Reset();
	TArray<FString>& E = OutErrors;

	// 1) JSON parse -----------------------------------------------------------
	TSharedPtr<FJsonObject> RootPtr;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, RootPtr) || !RootPtr.IsValid())
	{
		E.Add(FString::Printf(TEXT("(root): invalid JSON (%s)"), *Reader->GetErrorMessage()));
		return false;
	}
	const TSharedRef<FJsonObject> Root = RootPtr.ToSharedRef();
	FSceneSpec& S = OutSpec;

	// 2) schema_version -------------------------------------------------------
	if (ReadString(Root, TEXT(""), TEXT("schema_version"), S.SchemaVersion, E, true) &&
		S.SchemaVersion != TEXT("0.1"))
	{
		E.Add(FString::Printf(TEXT("schema_version: unsupported version \"%s\" (expected \"0.1\")"), *S.SchemaVersion));
	}

	// 3) scene_id -------------------------------------------------------------
	ReadString(Root, TEXT(""), TEXT("scene_id"), S.SceneId, E, true);
	if (!S.SceneId.IsEmpty() && !IsValidIdentifier(S.SceneId))
	{
		E.Add(TEXT("scene_id: invalid id (must match [A-Za-z_][A-Za-z0-9_]{0,63})"));
	}

	// 4) room -----------------------------------------------------------------
	if (const TSharedPtr<FJsonObject> Room = ReadObjectField(Root, TEXT(""), TEXT("room"), true, E))
	{
		const FString P = TEXT("room.");
		ReadRange(Room, P, TEXT("width_cm"), S.RoomWidthCm, 200.0, 10000.0, E);
		ReadRange(Room, P, TEXT("length_cm"), S.RoomLengthCm, 200.0, 10000.0, E);
		ReadRange(Room, P, TEXT("height_cm"), S.RoomHeightCm, 200.0, 2000.0, E);
	}

	// 5) seed (optional) ------------------------------------------------------
	{
		double Seed = 0.0;
		if (ReadNumber(Root, TEXT(""), TEXT("seed"), Seed, E, false))
		{
			S.bSeedProvided = true;
			S.Seed = static_cast<int64>(Seed);
		}
	}

	// 6) entrance / exit ------------------------------------------------------
	ValidateDoorObject(Root, S, /*bEntrance*/true, S.Entrance, E);
	ValidateDoorObject(Root, S, /*bEntrance*/false, S.Exit, E);

	// 7) agent ----------------------------------------------------------------
	if (const TSharedPtr<FJsonObject> Agent = ReadObjectField(Root, TEXT(""), TEXT("agent"), true, E))
	{
		const FString P = TEXT("agent.");
		ReadRange(Agent, P, TEXT("radius_cm"), S.AgentRadiusCm, 10.0, 100.0, E);
		ReadRange(Agent, P, TEXT("height_cm"), S.AgentHeightCm, 50.0, 300.0, E);
	}

	// 8) doors vs agent -------------------------------------------------------
	if (S.AgentRadiusCm > 0.0)
	{
		const double MinDoor = 2.0 * S.AgentRadiusCm + 10.0; // agent diameter + 10cm clearance
		if (S.Entrance.WidthCm > 0.0 && S.Entrance.WidthCm < MinDoor)
		{
			E.Add(FString::Printf(TEXT("entrance.width_cm: door (%.1fcm) is narrower than agent diameter + clearance (%.1fcm)"),
				S.Entrance.WidthCm, MinDoor));
		}
		if (S.Exit.WidthCm > 0.0 && S.Exit.WidthCm < MinDoor)
		{
			E.Add(FString::Printf(TEXT("exit.width_cm: door (%.1fcm) is narrower than agent diameter + clearance (%.1fcm)"),
				S.Exit.WidthCm, MinDoor));
		}
	}

	// 9) objects --------------------------------------------------------------
	TSet<FString> SeenIds;
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!ReadArrayField(Root, TEXT(""), TEXT("objects"), Arr, E, true))
		{
			E.Add(TEXT("objects: missing required field (must be an array)"));
		}
		else if (Arr->Num() < 1 || Arr->Num() > 64)
		{
			E.Add(FString::Printf(TEXT("objects: count %d out of range [1, 64]"), Arr->Num()));
		}
		else
		{
			for (int32 i = 0; i < Arr->Num(); ++i)
			{
				const FString P = FString::Printf(TEXT("objects[%d]."), i);
				const TSharedPtr<FJsonObject>* OPtr = nullptr;
				if (!(*Arr)[i]->TryGetObject(OPtr) || OPtr == nullptr)
				{
					E.Add(FString::Printf(TEXT("objects[%d]: must be an object"), i));
					continue;
				}
				const TSharedRef<FJsonObject> O = OPtr->ToSharedRef();
				FSceneSpecObject Obj;

				ReadString(O, P, TEXT("id"), Obj.Id, E, true);
				if (!Obj.Id.IsEmpty())
				{
					if (!IsValidIdentifier(Obj.Id))
					{
						E.Add(FString::Printf(TEXT("%sid: invalid id \"%s\" (must match [A-Za-z_][A-Za-z0-9_]{0,63})"), *P, *Obj.Id));
					}
					if (SeenIds.Contains(Obj.Id))
					{
						E.Add(FString::Printf(TEXT("%sid: duplicate object id \"%s\""), *P, *Obj.Id));
					}
					SeenIds.Add(Obj.Id);
				}

				ReadString(O, P, TEXT("semantic_type"), Obj.SemanticType, E, true);
				if (!Obj.SemanticType.IsEmpty() && !IsValidIdentifier(Obj.SemanticType))
				{
					E.Add(FString::Printf(TEXT("%ssemantic_type: invalid value \"%s\""), *P, *Obj.SemanticType));
				}

				ReadString(O, P, TEXT("asset_id"), Obj.AssetId, E, false);

				// target_size_cm: required array of exactly 3 numbers in [1, 5000]
				{
					const TArray<TSharedPtr<FJsonValue>>* SA = nullptr;
					if (!ReadArrayField(O, P, TEXT("target_size_cm"), SA, E, true))
					{
						E.Add(FString::Printf(TEXT("%starget_size_cm: missing required field (must be an array of 3 numbers)"), *P));
					}
					else if (SA->Num() != 3)
					{
						E.Add(FString::Printf(TEXT("%starget_size_cm: must have exactly 3 elements (got %d)"), *P, SA->Num()));
					}
					else
					{
						double N[3] = {0.0, 0.0, 0.0};
						bool bAllNumbers = true;
						for (int32 k = 0; k < 3; ++k)
						{
							if (!(*SA)[k]->TryGetNumber(N[k]) || !FMath::IsFinite(N[k]))
							{
								E.Add(FString::Printf(TEXT("%starget_size_cm[%d]: must be a finite number"), *P, k));
								bAllNumbers = false;
							}
						}
						if (bAllNumbers)
						{
							Obj.SizeXCm = N[0];
							Obj.SizeYCm = N[1];
							Obj.SizeZCm = N[2];
							if (N[0] < 1.0 || N[0] > 5000.0 || N[1] < 1.0 || N[1] > 5000.0 || N[2] < 1.0 || N[2] > 5000.0)
							{
								E.Add(FString::Printf(TEXT("%starget_size_cm: value [%.1f, %.1f, %.1f] out of range [1, 5000] per axis"),
									*P, N[0], N[1], N[2]));
							}
						}
					}
				}

				// placement (optional object)
				const TSharedPtr<FJsonValue> PL = O->TryGetField(TEXT("placement"));
				if (PL.IsValid() && !PL->IsNull())
				{
					const TSharedPtr<FJsonObject>* PlacePtr = nullptr;
					if (!PL->TryGetObject(PlacePtr) || PlacePtr == nullptr)
					{
						E.Add(FString::Printf(TEXT("%splacement: must be an object"), *P));
					}
					else
					{
						const TSharedRef<FJsonObject> Place = PlacePtr->ToSharedRef();
						const FString PP = P + TEXT("placement.");

						const TArray<TSharedPtr<FJsonValue>>* PA = nullptr;
						const TSharedPtr<FJsonValue> PosVal = Place->TryGetField(TEXT("position_cm"));
						if (PosVal.IsValid() && !PosVal->IsNull() &&
							ReadArrayField(Place, PP, TEXT("position_cm"), PA, E, false))
						{
							if (PA->Num() != 2)
							{
								E.Add(FString::Printf(TEXT("%sposition_cm: must be an array of 2 numbers [x, y]"), *PP));
							}
							else
							{
								double PX = 0.0, PY = 0.0;
								if ((*PA)[0]->TryGetNumber(PX) && (*PA)[1]->TryGetNumber(PY) &&
									FMath::IsFinite(PX) && FMath::IsFinite(PY))
								{
									Obj.bHasPosition = true;
									Obj.PositionXCm = PX;
									Obj.PositionYCm = PY;
								}
								else
								{
									E.Add(FString::Printf(TEXT("%sposition_cm: elements must be finite numbers"), *PP));
								}
							}
						}

						FString OnTop;
						if (ReadString(Place, PP, TEXT("on_top_of"), OnTop, E, false) && !OnTop.IsEmpty())
						{
							Obj.bOnTopOf = true;
							Obj.OnTopOfId = OnTop;
						}

						double Rot = 0.0;
						if (ReadNumber(Place, PP, TEXT("rotation_deg"), Rot, E, false))
						{
							if (Rot < 0.0 || Rot >= 360.0)
							{
								E.Add(FString::Printf(TEXT("%srotation_deg: value %.1f out of range [0, 360)"), *PP, Rot));
							}
							Obj.RotationDeg = Rot;
						}
					}
				}

				ReadString(O, P, TEXT("collision_policy"), Obj.CollisionPolicy, E, false);
				if (!Obj.CollisionPolicy.IsEmpty() &&
					!(Obj.CollisionPolicy == TEXT("none") || Obj.CollisionPolicy == TEXT("simple") || Obj.CollisionPolicy == TEXT("complex")))
				{
					E.Add(FString::Printf(TEXT("%scollision_policy: invalid value \"%s\" (expected none|simple|complex)"), *P, *Obj.CollisionPolicy));
				}

				ReadBool(O, P, TEXT("nav_obstacle"), Obj.bNavObstacle, E, false, false);

				ReadString(O, P, TEXT("priority"), Obj.Priority, E, false);
				if (!Obj.Priority.IsEmpty() &&
					!(Obj.Priority == TEXT("required") || Obj.Priority == TEXT("optional")))
				{
					E.Add(FString::Printf(TEXT("%spriority: invalid value \"%s\" (expected required|optional)"), *P, *Obj.Priority));
				}

				S.Objects.Add(Obj);
			}
		}
	}

	// 10) object footprint vs room -------------------------------------------
	if (S.RoomWidthCm > 0.0)
	{
		for (int32 i = 0; i < S.Objects.Num(); ++i)
		{
			const FSceneSpecObject& Obj = S.Objects[i];
			if (!Obj.bHasPosition)
			{
				continue;
			}
			const double Rad = FMath::DegreesToRadians(Obj.RotationDeg);
			const double Cos = FMath::Abs(FMath::Cos(Rad));
			const double Sin = FMath::Abs(FMath::Sin(Rad));
			const double Hx = (Obj.SizeXCm / 2.0) * Cos + (Obj.SizeYCm / 2.0) * Sin;
			const double Hy = (Obj.SizeXCm / 2.0) * Sin + (Obj.SizeYCm / 2.0) * Cos;
			const double HW = S.RoomWidthCm / 2.0;
			const double HL = S.RoomLengthCm / 2.0;
			const double LoX = Obj.PositionXCm - Hx;
			const double HiX = Obj.PositionXCm + Hx;
			const double LoY = Obj.PositionYCm - Hy;
			const double HiY = Obj.PositionYCm + Hy;
			if (LoX < -HW || HiX > HW || LoY < -HL || HiY > HL)
			{
				E.Add(FString::Printf(TEXT("objects[%d].placement.position_cm: object \"%s\" footprint [x %.1f..%.1f, y %.1f..%.1f] extends outside the room"),
					i, *Obj.Id, LoX, HiX, LoY, HiY));
			}
			if (Obj.SizeZCm > S.RoomHeightCm)
			{
				E.Add(FString::Printf(TEXT("objects[%d].target_size_cm: object \"%s\" height %.1f exceeds room height %.1f"),
					i, *Obj.Id, Obj.SizeZCm, S.RoomHeightCm));
			}
		}
	}

	// 11) references ----------------------------------------------------------
	for (int32 i = 0; i < S.Objects.Num(); ++i)
	{
		const FSceneSpecObject& Obj = S.Objects[i];
		if (Obj.bOnTopOf && !SeenIds.Contains(Obj.OnTopOfId))
		{
			E.Add(FString::Printf(TEXT("objects[%d].placement.on_top_of: unknown reference \"%s\" (no object with this id)"),
				i, *Obj.OnTopOfId));
		}
	}

	// 12) objective -----------------------------------------------------------
	if (const TSharedPtr<FJsonObject> Objv = ReadObjectField(Root, TEXT(""), TEXT("objective"), true, E))
	{
		const FString P = TEXT("objective.");
		ReadString(Objv, P, TEXT("type"), S.ObjectiveType, E, true);
		if (!S.ObjectiveType.IsEmpty() && S.ObjectiveType != TEXT("collect_then_exit"))
		{
			E.Add(FString::Printf(TEXT("objective.type: unknown objective type \"%s\" (expected \"collect_then_exit\")"), *S.ObjectiveType));
		}
		ReadString(Objv, P, TEXT("item_id"), S.ObjectiveItemId, E, true);
		if (!S.ObjectiveItemId.IsEmpty())
		{
			bool bFound = false;
			for (const FSceneSpecObject& O : S.Objects)
			{
				if (O.Id == S.ObjectiveItemId)
				{
					bFound = true;
					if (O.SemanticType != TEXT("pickup"))
					{
						E.Add(FString::Printf(TEXT("objective.item_id: objective item \"%s\" is not a pickup (semantic_type \"%s\")"),
							*O.Id, *O.SemanticType));
					}
					break;
				}
			}
			if (!bFound)
			{
				E.Add(FString::Printf(TEXT("objective.item_id: unknown reference \"%s\" (no object with this id)"), *S.ObjectiveItemId));
			}
		}
		if (S.ObjectiveType == TEXT("collect_then_exit") && !S.Exit.bLocked)
		{
			E.Add(TEXT("exit.locked: objective \"collect_then_exit\" requires the exit to be locked"));
		}
	}

	return E.Num() == 0;
}

bool FSceneSpecValidator::ValidateFile(const FString& AbsolutePath, FSceneSpec& OutSpec,
	TArray<FString>& OutErrors, FString& OutReadError)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *AbsolutePath))
	{
		OutReadError = FString::Printf(TEXT("cannot read file %s"), *AbsolutePath);
		return false;
	}
	return Validate(Text, OutSpec, OutErrors);
}

// ---------------------------------------------------------------- M1a harness

namespace WorldGen
{
namespace SceneSpecTest
{
	namespace
	{
		/**
		 * Registered expectations keep the harness strict: an invalid sample only
		 * "passes" when the validator reports the *intended* error category.
		 */
		const TCHAR* ExpectedErrorTokenFor(const FString& FileName)
		{
			if (FileName == TEXT("01_missing_required_field.json"))   return TEXT("missing required field");
			if (FileName == TEXT("02_bad_schema_version.json"))       return TEXT("schema_version");
			if (FileName == TEXT("03_duplicate_object_id.json"))      return TEXT("duplicate object id");
			if (FileName == TEXT("04_unknown_reference.json"))        return TEXT("unknown reference");
			if (FileName == TEXT("05_door_narrower_than_agent.json")) return TEXT("narrower than agent");
			if (FileName == TEXT("06_object_outside_room.json"))      return TEXT("outside the room");
			if (FileName == TEXT("07_room_size_out_of_range.json"))   return TEXT("out of range");
			if (FileName == TEXT("08_invalid_objective_type.json"))   return TEXT("objective type");
			return nullptr;
		}
	}

	void RunM1aValidation()
	{
		int32 Total = 0;
		int32 Failures = 0;

		const FString ScenesDir = FPaths::ProjectDir() / TEXT("Data/Scenes");
		const FString ValidDir = ScenesDir / TEXT("valid");
		const FString InvalidDir = ScenesDir / TEXT("invalid");

		TArray<FString> ValidFiles, InvalidFiles;
		IFileManager::Get().FindFiles(ValidFiles, *(ValidDir / TEXT("*.json")), true, false);
		IFileManager::Get().FindFiles(InvalidFiles, *(InvalidDir / TEXT("*.json")), true, false);
		ValidFiles.Sort();
		InvalidFiles.Sort();

		UE_LOG(LogWorldGenRT, Display, TEXT("[M1aTest] === SceneSpec sample validation starts (dir=%s) ==="), *ScenesDir);

		if (ValidFiles.Num() == 0)
		{
			UE_LOG(LogWorldGenRT, Error, TEXT("[M1aTest] FAIL: no valid samples found in %s"), *ValidDir);
			++Failures;
		}
		if (InvalidFiles.Num() == 0)
		{
			UE_LOG(LogWorldGenRT, Error, TEXT("[M1aTest] FAIL: no invalid samples found in %s"), *InvalidDir);
			++Failures;
		}

		auto RunFile = [](const FString& Path, FSceneSpec& OutSpec, TArray<FString>& OutErrors, FString& OutReadErr) -> bool
		{
			OutReadErr.Reset();
			return FSceneSpecValidator::ValidateFile(Path, OutSpec, OutErrors, OutReadErr);
		};

		// --- valid samples: must parse with 0 errors, twice-identical ----------
		for (const FString& File : ValidFiles)
		{
			++Total;
			const FString Path = ValidDir / File;
			FSceneSpec S1, S2;
			TArray<FString> E1, E2;
			FString ReadErr;
			const bool bOk1 = RunFile(Path, S1, E1, ReadErr);
			if (!ReadErr.IsEmpty())
			{
				UE_LOG(LogWorldGenRT, Error, TEXT("[M1aTest] valid/%s -> FAIL: %s"), *File, *ReadErr);
				++Failures;
				continue;
			}
			RunFile(Path, S2, E2, ReadErr);
			if (!bOk1)
			{
				UE_LOG(LogWorldGenRT, Error, TEXT("[M1aTest] valid/%s -> FAIL: unexpected errors: %s"),
					*File, *FString::Join(E1, TEXT(" ; ")));
				++Failures;
				continue;
			}
			if (E1.Num() != E2.Num() || S1.MakeCanonicalSummary() != S2.MakeCanonicalSummary())
			{
				UE_LOG(LogWorldGenRT, Error, TEXT("[M1aTest] valid/%s -> FAIL: not deterministic across two parses"), *File);
				++Failures;
				continue;
			}
			UE_LOG(LogWorldGenRT, Display, TEXT("[M1aTest] valid/%s -> PASS (0 errors, deterministic)"), *File);
			UE_LOG(LogWorldGenRT, Display, TEXT("[M1aTest]   parsed: %s"), *S1.MakeCanonicalSummary());
		}

		// --- invalid samples: must fail with the expected error category -------
		for (const FString& File : InvalidFiles)
		{
			++Total;
			const FString Path = InvalidDir / File;
			const TCHAR* Token = ExpectedErrorTokenFor(File);
			if (Token == nullptr)
			{
				UE_LOG(LogWorldGenRT, Error, TEXT("[M1aTest] invalid/%s -> FAIL: no expected error registered for this sample"), *File);
				++Failures;
				continue;
			}
			FSceneSpec S1, S2;
			TArray<FString> E1, E2;
			FString ReadErr;
			const bool bOk1 = RunFile(Path, S1, E1, ReadErr);
			if (!ReadErr.IsEmpty())
			{
				UE_LOG(LogWorldGenRT, Error, TEXT("[M1aTest] invalid/%s -> FAIL: %s"), *File, *ReadErr);
				++Failures;
				continue;
			}
			RunFile(Path, S2, E2, ReadErr);
			if (bOk1)
			{
				UE_LOG(LogWorldGenRT, Error, TEXT("[M1aTest] invalid/%s -> FAIL: unexpectedly passed (expected error containing \"%s\")"),
					*File, Token);
				++Failures;
				continue;
			}
			if (E1.Num() != E2.Num() || S1.MakeCanonicalSummary() != S2.MakeCanonicalSummary())
			{
				UE_LOG(LogWorldGenRT, Error, TEXT("[M1aTest] invalid/%s -> FAIL: not deterministic across two parses"), *File);
				++Failures;
				continue;
			}
			bool bTokenMatch = false;
			for (const FString& Err : E1)
			{
				if (Err.Contains(Token))
				{
					bTokenMatch = true;
					break;
				}
			}
			if (!bTokenMatch)
			{
				UE_LOG(LogWorldGenRT, Error, TEXT("[M1aTest] invalid/%s -> FAIL: errors [%s] do not contain expected token \"%s\""),
					*File, *FString::Join(E1, TEXT(" ; ")), Token);
				++Failures;
				continue;
			}
			UE_LOG(LogWorldGenRT, Display, TEXT("[M1aTest] invalid/%s -> PASS (%d error(s), expected category \"%s\")"),
				*File, E1.Num(), Token);
			UE_LOG(LogWorldGenRT, Display, TEXT("[M1aTest]   first error: %s"), E1.Num() > 0 ? *E1[0] : TEXT("<none>"));
		}

		const bool bAllPass = (Failures == 0);
		UE_LOG(LogWorldGenRT, Display, TEXT("[M1aTest] SUMMARY: %d samples, %d failures -> %s"),
			Total, Failures, bAllPass ? TEXT("ALL PASS") : TEXT("FAILED"));

		// Forced exit carries the failure count as the process exit code
		// (5.7 main loop drops PostQuitMessage's wParam on the graceful path).
		GLog->Flush();
		FPlatformMisc::RequestExitWithStatus(true, static_cast<uint8>(Failures));
	}
}
}
