#include "LevelPlanBuilder.h"

// ---------------------------------------------------------------- helpers

namespace
{
	struct FPlanOp
	{
		FString Op;        // box | player_start | door | pickup | exit_trigger | directional_light | sky_light
		FString Name;
		FString AssetId;   // box only: SceneSpec objects[].asset_id carried through (M2b);
		                   // empty for structural boxes (floor/walls/apron/lintel)
		double X = 0.0, Y = 0.0, Z = 0.0;
		double SizeX = 0.0, SizeY = 0.0, SizeZ = 0.0; // box only
		double Yaw = 0.0;
		double Pitch = 0.0;                            // directional_light only
	};

	const TCHAR* OpTypeName(const FString& Op)
	{
		if (Op == TEXT("box"))              return TEXT("box");
		if (Op == TEXT("player_start"))     return TEXT("player_start");
		if (Op == TEXT("door"))             return TEXT("door");
		if (Op == TEXT("pickup"))           return TEXT("pickup");
		if (Op == TEXT("exit_trigger"))     return TEXT("exit_trigger");
		if (Op == TEXT("directional_light"))return TEXT("directional_light");
		if (Op == TEXT("sky_light"))        return TEXT("sky_light");
		return TEXT("?");
	}

	FString Num(double V)
	{
		return FString::Printf(TEXT("%.2f"), V);
	}

	FString OpToJson(const FPlanOp& O)
	{
	if (O.Op == TEXT("box"))
	{
		if (!O.AssetId.IsEmpty())
		{
			return FString::Printf(TEXT("    {\"op\":\"box\",\"name\":\"%s\",\"asset_id\":\"%s\",\"size_cm\":[%s,%s,%s],\"location_cm\":[%s,%s,%s],\"yaw_deg\":%s}"),
				*O.Name, *O.AssetId, *Num(O.SizeX), *Num(O.SizeY), *Num(O.SizeZ), *Num(O.X), *Num(O.Y), *Num(O.Z), *Num(O.Yaw));
		}
		return FString::Printf(TEXT("    {\"op\":\"box\",\"name\":\"%s\",\"size_cm\":[%s,%s,%s],\"location_cm\":[%s,%s,%s],\"yaw_deg\":%s}"),
			*O.Name, *Num(O.SizeX), *Num(O.SizeY), *Num(O.SizeZ), *Num(O.X), *Num(O.Y), *Num(O.Z), *Num(O.Yaw));
	}
		if (O.Op == TEXT("directional_light"))
		{
			return FString::Printf(TEXT("    {\"op\":\"directional_light\",\"name\":\"%s\",\"location_cm\":[%s,%s,%s],\"pitch_deg\":%s,\"yaw_deg\":%s}"),
				*O.Name, *Num(O.X), *Num(O.Y), *Num(O.Z), *Num(O.Pitch), *Num(O.Yaw));
		}
		return FString::Printf(TEXT("    {\"op\":\"%s\",\"name\":\"%s\",\"location_cm\":[%s,%s,%s],\"yaw_deg\":%s}"),
			OpTypeName(O.Op), *O.Name, *Num(O.X), *Num(O.Y), *Num(O.Z), *Num(O.Yaw));
	}

	/** Absolute run-space span of a doorway: offset is measured from the wall's negative end. */
	void DoorwaySpan(const FSceneSpecDoor& Door, double RunLength, double& Lo, double& Hi, double& Center)
	{
		Center = -RunLength / 2.0 + Door.OffsetCm;
		Lo = Center - Door.WidthCm / 2.0;
		Hi = Center + Door.WidthCm / 2.0;
	}

	bool IsNorthSouth(const FString& Wall)
	{
		return Wall == TEXT("north") || Wall == TEXT("south");
	}
}

// ---------------------------------------------------------------- BuildPlan

bool FLevelPlanBuilder::BuildPlan(const FSceneSpec& Spec, const FString& SourceSpecFileName,
	FString& OutPlanJson, TArray<FString>& OutErrors)
{
	TArray<FString>& E = OutErrors;
	const double T = 20.0;          // wall thickness
	const double DoorH = 220.0;     // AWorldGenDoor native panel height
	const double W = Spec.RoomWidthCm;
	const double L = Spec.RoomLengthCm;
	const double H = Spec.RoomHeightCm;

	if (Spec.Exit.WidthCm != 120.0 || Spec.Entrance.WidthCm != 120.0)
	{
		E.Add(FString::Printf(TEXT("exit/entrance.width_cm: the AWorldGenDoor placeholder is a fixed 120 cm panel; got exit %.1f / entrance %.1f"),
			Spec.Exit.WidthCm, Spec.Entrance.WidthCm));
	}
	if (H < DoorH)
	{
		E.Add(FString::Printf(TEXT("room.height_cm: room height %.1f is lower than the 220 cm door panel"), H));
	}
	if (!E.IsEmpty())
	{
		return false;
	}

	TArray<FPlanOp> Ops;
	auto AddBox = [&Ops](const FString& Name, double Sx, double Sy, double Sz, double X, double Y, double Z, double Yaw = 0.0, const FString& AssetId = FString())
	{
		FPlanOp O; O.Op = TEXT("box"); O.Name = Name; O.AssetId = AssetId;
		O.SizeX = Sx; O.SizeY = Sy; O.SizeZ = Sz; O.X = X; O.Y = Y; O.Z = Z; O.Yaw = Yaw;
		Ops.Add(O);
	};

	// 1) floor ---------------------------------------------------------------
	AddBox(TEXT("WG_Floor"), W + 2 * T, L + 2 * T, 20.0, 0.0, 0.0, -10.0);

	// 2) walls (north, south, east, west) -------------------------------------
	auto AddWall = [&](const FString& WallName, const FString& Wall,
		bool bHasDoorway, const FSceneSpecDoor& Door)
	{
		const double Run = IsNorthSouth(Wall) ? L : W;
		const bool bNS = IsNorthSouth(Wall);
		// Wall boxes extend T past each end to close the corners.
		const double RunLo = -Run / 2.0 - T;
		const double RunHi = Run / 2.0 + T;
		const double Fix = bNS ? (Wall == TEXT("north") ? W / 2.0 : -W / 2.0)
			                   : (Wall == TEXT("east") ? L / 2.0 : -L / 2.0);

		if (!bHasDoorway)
		{
			const double Len = RunHi - RunLo;
			if (bNS) AddBox(WallName, T, Len, H, Fix, (RunLo + RunHi) / 2.0, H / 2.0);
			else     AddBox(WallName, Len, T, H, (RunLo + RunHi) / 2.0, Fix, H / 2.0);
			return;
		}

		double Lo, Hi, Center;
		DoorwaySpan(Door, Run, Lo, Hi, Center);

		// segment A: RunLo..Lo ; segment B: Hi..RunHi ; lintel above the doorway
		struct FSeg { double Lo, Hi; };
		const FSeg Segs[2] = { {RunLo, Lo}, {Hi, RunHi} };
		for (int32 i = 0; i < 2; ++i)
		{
			const double Len = Segs[i].Hi - Segs[i].Lo;
			if (Len <= 0.0) continue;
			const FString N = FString::Printf(TEXT("%s_%s"), *WallName, i == 0 ? TEXT("A") : TEXT("B"));
			if (bNS) AddBox(N, T, Len, H, Fix, (Segs[i].Lo + Segs[i].Hi) / 2.0, H / 2.0);
			else     AddBox(N, Len, T, H, (Segs[i].Lo + Segs[i].Hi) / 2.0, Fix, H / 2.0);
		}
		const double LintelH = H - DoorH;
		const FString LN = WallName + TEXT("_Lintel");
		if (bNS) AddBox(LN, T, Door.WidthCm, LintelH, Fix, Center, DoorH + LintelH / 2.0);
		else     AddBox(LN, Door.WidthCm, T, LintelH, Center, Fix, DoorH + LintelH / 2.0);
	};

	// M2b multi-scene fix: the doorway goes into WHICHEVER wall hosts the door,
	// not unconditionally into north/south. The original M1b-1 code hardcoded
	// bHasDoorway=true for north/south and false for east/west, so an exit on
	// the east/west wall produced a door actor + exit trigger in front of a
	// SOLID wall (found by the 3-scene AI experiment: the E2E opened the door
	// and then walked into the wall forever).
	auto DoorForWall = [&](const FString& Wall) -> const FSceneSpecDoor*
	{
		if (Spec.Exit.Wall == Wall) return &Spec.Exit;
		if (Spec.Entrance.Wall == Wall) return &Spec.Entrance;
		return nullptr; // exit and entrance walls differ (validator rule), never both
	};
	const FSceneSpecDoor* ND = DoorForWall(TEXT("north"));
	const FSceneSpecDoor* SD = DoorForWall(TEXT("south"));
	const FSceneSpecDoor* ED = DoorForWall(TEXT("east"));
	const FSceneSpecDoor* WD = DoorForWall(TEXT("west"));
	AddWall(TEXT("WG_Wall_North"), TEXT("north"), ND != nullptr, ND ? *ND : Spec.Entrance);
	AddWall(TEXT("WG_Wall_South"), TEXT("south"), SD != nullptr, SD ? *SD : Spec.Entrance);
	AddWall(TEXT("WG_Wall_East"), TEXT("east"), ED != nullptr, ED ? *ED : Spec.Entrance);
	AddWall(TEXT("WG_Wall_West"), TEXT("west"), WD != nullptr, WD ? *WD : Spec.Entrance);

	// 3) apron outside the exit + exit trigger --------------------------------
	// M1c-A acceptance rule: the trigger's inner face must sit 2*agent_radius
	// + 20 cm beyond the wall's OUTER face. A capsule of radius R can only
	// overlap the box once its CENTER is at least R + 20 cm past the outer
	// face, i.e. the whole capsule has crossed the wall - standing against the
	// wall on the indoor side can never fire it.
	{
		double Lo, Hi, Center;
		DoorwaySpan(Spec.Exit, IsNorthSouth(Spec.Exit.Wall) ? L : W, Lo, Hi, Center);
		const FString& Wall = Spec.Exit.Wall;
		const double R = FMath::Max(Spec.AgentRadiusCm, 34.0); // UE default capsule fallback
		const double Clear = T + 2.0 * R + 20.0;   // wall outer face -> trigger inner face
		double TX = 0, TY = 0, AX = 0, AY = 0, ASx = 0, ASy = 0;
		if (Wall == TEXT("north"))      { TX = W / 2 + Clear + 200; TY = Center; AX = W / 2 + Clear / 2 + 100; AY = Center; ASx = Clear + 200; ASy = Spec.Exit.WidthCm + 80; }
		else if (Wall == TEXT("south")) { TX = -W / 2 - Clear - 200; TY = Center; AX = -W / 2 - Clear / 2 - 100; AY = Center; ASx = Clear + 200; ASy = Spec.Exit.WidthCm + 80; }
		else if (Wall == TEXT("east"))  { TX = Center; TY = L / 2 + Clear + 200; AX = Center; AY = L / 2 + Clear / 2 + 100; ASx = Spec.Exit.WidthCm + 80; ASy = Clear + 200; }
		else                            { TX = Center; TY = -L / 2 - Clear - 200; AX = Center; AY = -L / 2 - Clear / 2 - 100; ASx = Spec.Exit.WidthCm + 80; ASy = Clear + 200; }

		AddBox(TEXT("WG_Apron_Exit"), ASx, ASy, 20.0, AX, AY, -10.0);

		FPlanOp Trig; Trig.Op = TEXT("exit_trigger"); Trig.Name = TEXT("WG_ExitTrigger");
		Trig.X = TX; Trig.Y = TY; Trig.Z = 0.0; Trig.Yaw = 0.0;
		Ops.Add(Trig);
	}

	// 4) lighting -------------------------------------------------------------
	{
		FPlanOp Sun; Sun.Op = TEXT("directional_light"); Sun.Name = TEXT("WG_Sun");
		Sun.X = 0; Sun.Y = 0; Sun.Z = 500; Sun.Pitch = -55.0; Sun.Yaw = 30.0;
		Ops.Add(Sun);
		FPlanOp Sky; Sky.Op = TEXT("sky_light"); Sky.Name = TEXT("WG_Sky");
		Sky.X = 0; Sky.Y = 0; Sky.Z = 300; Sky.Yaw = 0.0;
		Ops.Add(Sky);
	}

	// 5) player start (inside the entrance, facing the room) -------------------
	{
		double Lo, Hi, Center;
		DoorwaySpan(Spec.Entrance, IsNorthSouth(Spec.Entrance.Wall) ? L : W, Lo, Hi, Center);
		FPlanOp PS; PS.Op = TEXT("player_start"); PS.Name = TEXT("WG_PlayerStart");
		const FString& Wall = Spec.Entrance.Wall;
		if (Wall == TEXT("north"))      { PS.X = W / 2 - 80; PS.Y = Center; PS.Yaw = 180.0; }
		else if (Wall == TEXT("south")) { PS.X = -W / 2 + 80; PS.Y = Center; PS.Yaw = 0.0; }
		else if (Wall == TEXT("east"))  { PS.X = Center; PS.Y = L / 2 - 80; PS.Yaw = 270.0; }
		else                            { PS.X = Center; PS.Y = -L / 2 + 80; PS.Yaw = 90.0; }
		PS.Z = 100.0;
		Ops.Add(PS);
	}

	// 6) exit door (hinge pivot; panel 120 x 220 x 10) --------------------------
	{
		double Lo, Hi, Center;
		DoorwaySpan(Spec.Exit, IsNorthSouth(Spec.Exit.Wall) ? L : W, Lo, Hi, Center);
		FPlanOp D; D.Op = TEXT("door"); D.Name = TEXT("WG_ExitDoor"); D.Z = 0.0;
		const FString& Wall = Spec.Exit.Wall;
		if (Wall == TEXT("north"))      { D.X = W / 2; D.Y = Lo; D.Yaw = 0.0; }    // panel +Y
		else if (Wall == TEXT("south")) { D.X = -W / 2; D.Y = Hi; D.Yaw = 180.0; } // panel -Y
		else if (Wall == TEXT("east"))  { D.X = Hi; D.Y = L / 2; D.Yaw = 90.0; }   // panel -X
		else                            { D.X = Lo; D.Y = -L / 2; D.Yaw = 270.0; } // panel +X
		Ops.Add(D);
	}

	// 7) objects from spec order (stable ids) ----------------------------------
	TMap<FString, const FSceneSpecObject*> ById;
	for (const FSceneSpecObject& O : Spec.Objects)
	{
		ById.Add(O.Id, &O);
	}
	for (int32 i = 0; i < Spec.Objects.Num(); ++i)
	{
		const FSceneSpecObject& O = Spec.Objects[i];
		const FString NodeName = FString::Printf(TEXT("WG_obj_%s"), *O.Id);

		double X = 0, Y = 0, Z = 0;
		if (O.bHasPosition)
		{
			X = O.PositionXCm; Y = O.PositionYCm; Z = O.SizeZCm / 2.0;
		}
		else if (O.bOnTopOf)
		{
			const FSceneSpecObject* const* Sup = ById.Find(O.OnTopOfId);
			if (Sup == nullptr)
			{
				E.Add(FString::Printf(TEXT("objects[%d].placement.on_top_of: unknown reference \"%s\""), i, *O.OnTopOfId));
				continue;
			}
			if (!(*Sup)->bHasPosition)
			{
				E.Add(FString::Printf(TEXT("objects[%d].placement.on_top_of: support \"%s\" has no position_cm; M1b-1 cannot resolve the world position"),
					i, *O.OnTopOfId));
				continue;
			}
			X = (*Sup)->PositionXCm;
			Y = (*Sup)->PositionYCm;
			Z = (*Sup)->SizeZCm; // top surface of the support
		}
		else
		{
			E.Add(FString::Printf(TEXT("objects[%d]: M1b-1 requires position_cm or on_top_of for \"%s\""), i, *O.Id));
			continue;
		}

		const bool bIsObjectivePickup = (O.Id == Spec.ObjectiveItemId && O.SemanticType == TEXT("pickup"));
		if (bIsObjectivePickup)
		{
			FPlanOp P; P.Op = TEXT("pickup"); P.Name = NodeName;
			P.X = X; P.Y = Y; P.Z = Z; P.Yaw = 0.0;
			Ops.Add(P);
		}
		else
		{
			// M2b: carry the SceneSpec asset_id through so the Python executor
			// resolves the real generated asset (or falls back explicitly).
			AddBox(NodeName, O.SizeXCm, O.SizeYCm, O.SizeZCm, X, Y, Z, O.RotationDeg, O.AssetId);
		}
	}

	if (!E.IsEmpty())
	{
		return false;
	}

	// 8) serialize (fixed order, fixed format) ---------------------------------
	FString Json = FString::Printf(TEXT("{\n  \"plan_version\": \"0.1\",\n  \"source_spec\": \"%s\",\n  \"scene_id\": \"%s\",\n  \"seed\": %lld,\n  \"level_path\": \"/Game/Generated/%s\",\n  \"wall_thickness_cm\": %s,\n  \"door_height_cm\": %s,\n  \"note\": \"object box ops carry the SceneSpec asset_id (M2b); the Python executor resolves it against Data/Assets/asset_manifest.json and falls back to the placeholder cube for unregistered/not-ready ids\",\n  \"ops\": [\n"),
		*SourceSpecFileName, *Spec.SceneId, Spec.Seed, *Spec.SceneId, *Num(T), *Num(DoorH));
	for (int32 i = 0; i < Ops.Num(); ++i)
	{
		Json += OpToJson(Ops[i]);
		Json += (i + 1 < Ops.Num()) ? TEXT(",\n") : TEXT("\n");
	}
	Json += TEXT("  ]\n}\n");

	OutPlanJson = Json;
	return true;
}
