#include "WorldGenGameMode.h"
#include "WorldGenCharacter.h"
#include "WorldGenDoor.h"
#include "WorldGenExitTrigger.h"
#include "WorldGenGameState.h"
#include "WorldGenPickup.h"
#include "WorldGenRuntime.h"
#include "SceneSpec.h"
#include "LevelPlanBuilder.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/SphereComponent.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "Engine/Player.h"
#include "InputKeyEventArgs.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "Misc/Parse.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "UnrealClient.h"
#include "HAL/FileManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"

AWorldGenGameMode::AWorldGenGameMode()
{
	DefaultPawnClass = AWorldGenCharacter::StaticClass();
	PlayerControllerClass = APlayerController::StaticClass();
	GameStateClass = AWorldGenGameState::StaticClass();
	// M2bs demo frame capture drives off Tick; off by default for AGameModeBase.
	PrimaryActorTick.bCanEverTick = true;
}

void AWorldGenGameMode::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogWorldGenRT, Display, TEXT("WorldGenGameMode active (world=%s, PIE=%d, game=%d, defaultPawn=%s)"),
		*GetWorld()->GetName(),
		GetWorld()->IsPlayInEditor() ? 1 : 0,
		GetWorld()->IsGameWorld() ? 1 : 0,
		DefaultPawnClass ? *DefaultPawnClass->GetName() : TEXT("NULL"));

	if (FParse::Param(FCommandLine::Get(), TEXT("M0AutoQuit")))
	{
		UE_LOG(LogWorldGenRT, Display, TEXT("M0AutoQuit: exit requested in 8 seconds"));
		GetWorldTimerManager().SetTimer(AutoQuitTimerHandle, FTimerDelegate::CreateWeakLambda(this, []()
		{
			UE_LOG(LogWorldGenRT, Display, TEXT("M0AutoQuit: requesting engine exit now"));
			FGenericPlatformMisc::RequestExit(false);
		}), 8.0f, false);
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("M0_5AutoTest")))
	{
		UE_LOG(LogWorldGenRT, Display, TEXT("[M05Test] armed: state test + E2E playthrough starting in 1s"));
		ScheduleStateStep(1.0f);
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("M1aValidate")))
	{
		UE_LOG(LogWorldGenRT, Display, TEXT("[M1aTest] armed: SceneSpec sample validation starting in 0.5s"));
		GetWorldTimerManager().SetTimer(TestTimerHandle, FTimerDelegate::CreateWeakLambda(this, []()
		{
			WorldGen::SceneSpecTest::RunM1aValidation();
		}), 0.5f, false);
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("M1bGenerate")))
	{
		UE_LOG(LogWorldGenRT, Display, TEXT("[M1bGen] armed: SceneSpec plan generation starting in 0.5s"));
		GetWorldTimerManager().SetTimer(TestTimerHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			RunM1bGenerate();
		}), 0.5f, false);
	}

	// NOTE: arm via FParse::Value, not Param - Param does not match a token
	// that is followed by "=value" on the command line.
	FString M1cSpecArg;
	if (FParse::Value(FCommandLine::Get(), TEXT("M1cValidateSpec="), M1cSpecArg))
	{
		UE_LOG(LogWorldGenRT, Display, TEXT("[M1cTest] armed: single-spec validation starting in 0.5s (spec=%s)"), *M1cSpecArg);
		GetWorldTimerManager().SetTimer(TestTimerHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			RunM1cValidate();
		}), 0.5f, false);
	}

	FString M1cCheckArg;
	if (FParse::Value(FCommandLine::Get(), TEXT("M1cCheckSpec="), M1cCheckArg))
	{
		UE_LOG(LogWorldGenRT, Display, TEXT("[M1cCheck] armed: combined spec + plan-derived placement check starting in 0.5s (spec=%s)"), *M1cCheckArg);
		GetWorldTimerManager().SetTimer(TestTimerHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			RunM1cCheck();
		}), 0.5f, false);
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("M1bE2E")))
	{
		// M2bs demo: visual-only frame capture of the real playthrough.
		if (FString CaptureDir; FParse::Value(FCommandLine::Get(), TEXT("DemoCapture="), CaptureDir))
		{
			DemoCaptureDir = CaptureDir;
			IFileManager::Get().MakeDirectory(*CaptureDir, true);
			UE_LOG(LogWorldGenRT, Display, TEXT("[Demo] frame capture enabled -> %s"), *CaptureDir);
		}
		UE_LOG(LogWorldGenRT, Display, TEXT("[M1bTest] armed: generated-level playthrough starting in 1.5s"));
		GetWorldTimerManager().SetTimer(TestTimerHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			StartM1bE2E();
		}), 1.5f, false);
	}
}

// ---------------------------------------------------------------- helpers

APlayerController* AWorldGenGameMode::GetPC() const
{
	return UGameplayStatics::GetPlayerController(GetWorld(), 0);
}

AWorldGenCharacter* AWorldGenGameMode::GetPlayerChar() const
{
	return Cast<AWorldGenCharacter>(UGameplayStatics::GetPlayerCharacter(GetWorld(), 0));
}

void AWorldGenGameMode::InjectKey(const FKey& Key, EInputEvent Event, float AmountDepressed)
{
	if (APlayerController* PC = GetPC())
	{
		const FInputKeyEventArgs Args = FInputKeyEventArgs::CreateSimulated(Key, Event, AmountDepressed);
		PC->InputKey(Args);
	}
}

void AWorldGenGameMode::RecordState(int32 StepIdx, const FString& What, bool bOk)
{
	++StateChecks;
	if (!bOk)
	{
		++StateFailCount;
	}
	UE_LOG(LogWorldGenRT, Display, TEXT("[StateTest %d] %s -> %s"),
		StepIdx, *What, bOk ? TEXT("PASS") : TEXT("FAIL"));
	if (GEngine)
	{
		static int32 Key = 1000;
		GEngine->AddOnScreenDebugMessage(Key++, 10.0f, bOk ? FColor::Green : FColor::Red,
			FString::Printf(TEXT("StateTest[%d] %s: %s"), StepIdx, *What, bOk ? TEXT("PASS") : TEXT("FAIL")));
	}
}

void AWorldGenGameMode::RecordE2E(int32 StepIdx, const FString& What, bool bOk)
{
	++E2EChecks;
	if (!bOk)
	{
		++E2EFailCount;
	}
	UE_LOG(LogWorldGenRT, Display, TEXT("[E2E %d] %s -> %s"),
		StepIdx, *What, bOk ? TEXT("PASS") : TEXT("FAIL"));
	if (GEngine)
	{
		static int32 Key = 2000;
		GEngine->AddOnScreenDebugMessage(Key++, 10.0f, bOk ? FColor::Green : FColor::Red,
			FString::Printf(TEXT("E2E[%d] %s: %s"), StepIdx, *What, bOk ? TEXT("PASS") : TEXT("FAIL")));
	}
}

void AWorldGenGameMode::E2EAbort(int32 StepIdx, const FString& What)
{
	RecordE2E(StepIdx, What, false);
	UE_LOG(LogWorldGenRT, Error, TEXT("[E2E] aborted at step %d (%s)"), StepIdx, *What);
	E2EState = EE2EState::Done;
	GetWorldTimerManager().ClearTimer(TestTimerHandle);
	FinishAllTests();
}

// ---------------------------------------------------------------- phase 1: state test

void AWorldGenGameMode::ScheduleStateStep(float Delay)
{
	GetWorldTimerManager().SetTimer(TestTimerHandle, this, &AWorldGenGameMode::RunStateStep, Delay, false);
}

void AWorldGenGameMode::RunStateStep()
{
	AWorldGenGameState* GS = GetWorld()->GetGameState<AWorldGenGameState>();

	switch (StateStep++)
	{
	case 0: // collect level actors
	{
		for (TActorIterator<AWorldGenDoor> It(GetWorld()); It; ++It) { TestDoor = *It; }
		for (TActorIterator<AWorldGenPickup> It(GetWorld()); It; ++It) { TestPickup = *It; }
		for (TActorIterator<AWorldGenExitTrigger> It(GetWorld()); It; ++It) { TestExit = *It; }

		bActorsValid = TestDoor.IsValid() && TestPickup.IsValid() && TestExit.IsValid() && GS != nullptr;
		UE_LOG(LogWorldGenRT, Display, TEXT("[StateTest 0] actors found: door=%d pickup=%d exit=%d gamestate=%d"),
			TestDoor.IsValid() ? 1 : 0, TestPickup.IsValid() ? 1 : 0,
			TestExit.IsValid() ? 1 : 0, GS ? 1 : 0);
		if (!bActorsValid)
		{
			RecordState(0, TEXT("level actors present"), false);
			FinishAllTests();
			return;
		}
		ScheduleStateStep(1.0f);
		break;
	}
	case 1: // Test 1: opening the door without the key must fail
	{
		UE_LOG(LogWorldGenRT, Display, TEXT("[StateTest 1] calling Door::Interact() with key=%s"),
			GS && GS->HasKey() ? TEXT("true") : TEXT("false"));
		TestDoor->Interact();
		RecordState(1, TEXT("door stays closed without key"), !TestDoor->IsOpen());
		ScheduleStateStep(1.0f);
		break;
	}
	case 2: // Test 2: picking up the key sets the state (real PickUp path)
	{
		UE_LOG(LogWorldGenRT, Display, TEXT("[StateTest 2] calling Pickup::PickUp()"));
		TestPickup->PickUp();
		RecordState(2, TEXT("pickup sets key state"), GS && GS->HasKey());
		ScheduleStateStep(1.0f);
		break;
	}
	case 3: // Test 3: opening the door with the key must succeed
	{
		UE_LOG(LogWorldGenRT, Display, TEXT("[StateTest 3] calling Door::Interact() with key=%s"),
			GS && GS->HasKey() ? TEXT("true") : TEXT("false"));
		TestDoor->Interact();
		RecordState(3, TEXT("door opens with key"), TestDoor.IsValid() && TestDoor->IsOpen());
		ScheduleStateStep(1.0f);
		break;
	}
	case 4: // Test 4 (M1c-A): teleport to the INDOOR side of the exit wall, WITH the key
	{
		AWorldGenCharacter* Player = GetPlayerChar();
		if (Player && TestExit.IsValid() && TestDoor.IsValid())
		{
			// TestArena is a HAND-BUILT level: its exit wall is the EAST wall
			// (plane X=400, thickness 20 -> inner face 390, outer face 410) and
			// the saved door actor carries a legacy yaw, so the outward normal
			// is hardcoded +X instead of derived from the door rotation.
			// 50 cm inside the wall plane, 230 cm along the wall from the
			// doorway center: the trigger box starts 78 cm beyond the wall
			// plane, so an indoor capsule can never touch it.
			const FVector IndoorPoint = TestDoor->GetActorLocation() + FVector(-50.0f + 0.0f, 230.0f, 100.0f);
			Player->SetActorLocation(IndoorPoint, false, nullptr, ETeleportType::TeleportPhysics);
			UE_LOG(LogWorldGenRT, Display, TEXT("[StateTest 4] player teleported to the INDOOR side of the exit wall at %s (key=%d, overlap must NOT fire)"),
				*IndoorPoint.ToCompactString(), GS && GS->HasKey() ? 1 : 0);
		}
		else
		{
			RecordState(4, TEXT("player + exit zone + door available"), false);
		}
		ScheduleStateStep(1.5f);
		break;
	}
	case 5: // Test 5 (M1c-A): indoor side must not complete; then teleport into the exit zone
	{
		if (GS && GS->IsMissionComplete())
		{
			RecordState(4, TEXT("indoor side of exit wall does NOT complete the mission (with key)"), false);
			UE_LOG(LogWorldGenRT, Error, TEXT("[StateTest 5] mission completed from the INDOOR side - acceptance hole!"));
		}
		else
		{
			RecordState(4, TEXT("indoor side of exit wall does NOT complete the mission (with key)"), true);
		}

		AWorldGenCharacter* Player = GetPlayerChar();
		if (Player && TestExit.IsValid())
		{
			const FVector ExitCenter = TestExit->GetActorLocation() + FVector(0.0f, 0.0f, 100.0f);
			Player->SetActorLocation(ExitCenter, false, nullptr, ETeleportType::TeleportPhysics);
			UE_LOG(LogWorldGenRT, Display, TEXT("[StateTest 5] player teleported into exit zone at %s (overlap fires next tick)"),
				*ExitCenter.ToCompactString());
		}
		else
		{
			RecordState(5, TEXT("player + exit zone available"), false);
		}
		ScheduleStateStep(1.5f);
		break;
	}
	case 6: // Test 5 summary + input-pipeline test (hold W via InputKey)
	{
		RecordState(5, TEXT("exit trigger completes mission"), GS && GS->IsMissionComplete());

		APlayerController* PC = GetPC();
		AWorldGenCharacter* Player = GetPlayerChar();
		if (PC && Player)
		{
			// Hold W for ~1 s through the real input pipeline (same path as OS key
			// events: PlayerInput KeyState -> legacy axis mapping -> MoveForward).
			M05InputTestStartLocation = Player->GetActorLocation();
			InjectKey(EKeys::W, IE_Pressed, 1.0f);
			UE_LOG(LogWorldGenRT, Display, TEXT("[StateTest 6] W pressed via InputKey pipeline at %s"),
				*M05InputTestStartLocation.ToCompactString());
		}
		else
		{
			RecordState(6, TEXT("player controller available"), false);
			StartResetForE2E();
			return;
		}
		ScheduleStateStep(1.0f);
		break;
	}
	case 7: // release W, measure travelled distance, then reset for the E2E run
	{
		APlayerController* PC = GetPC();
		AWorldGenCharacter* Player = GetPlayerChar();
		if (PC && Player)
		{
			InjectKey(EKeys::W, IE_Released, 0.0f);

			const float Travelled = FVector::Dist2D(Player->GetActorLocation(), M05InputTestStartLocation);
			UE_LOG(LogWorldGenRT, Display, TEXT("[StateTest 6] W released, travelled %.1f cm"), Travelled);
			RecordState(6, TEXT("W key drives character via input pipeline"), Travelled > 30.0f);
		}
		StartResetForE2E();
		break;
	}
	default:
		break;
	}
}

void AWorldGenGameMode::StartResetForE2E()
{
	AWorldGenGameState* GS = GetWorld()->GetGameState<AWorldGenGameState>();
	AWorldGenCharacter* Player = GetPlayerChar();

	if (!bActorsValid || !GS || !Player)
	{
		FinishAllTests();
		return;
	}

	UE_LOG(LogWorldGenRT, Display, TEXT("[M05Test] resetting world state for the E2E playthrough"));
	TestDoor->SetClosed();
	TestPickup->ResetForRetest();
	GS->SetHasKey(false);
	GS->ClearMissionComplete();

	// Back to the spawn point facing +X (inter-phase setup teleport, not part
	// of the E2E playthrough itself).
	if (APlayerController* PC = GetPC())
	{
		PC->SetControlRotation(FRotator(0.0f, 0.0f, 0.0f));
	}
	Player->SetActorLocation(FVector(-300.0f, 0.0f, 100.0f), false, nullptr, ETeleportType::TeleportPhysics);

	DoorLoc = TestDoor->GetActorLocation();
	KeyLoc = TestPickup->GetActorLocation();
	ExitLoc = TestExit->GetActorLocation();
	UE_LOG(LogWorldGenRT, Display, TEXT("[M05Test] reset done: spawn=(-300,0) key=(%.0f,%.0f) door=(%.0f,%.0f) exit=(%.0f,%.0f)"),
		KeyLoc.X, KeyLoc.Y, DoorLoc.X, DoorLoc.Y, ExitLoc.X, ExitLoc.Y);

	GetWorldTimerManager().SetTimer(TestTimerHandle, this, &AWorldGenGameMode::StartE2E, 0.6f, false);
}

// ---------------------------------------------------------------- phase 2: E2E playthrough

void AWorldGenGameMode::StartE2E()
{
	AWorldGenCharacter* Player = GetPlayerChar();
	if (!bActorsValid || !Player)
	{
		FinishAllTests();
		return;
	}

	UE_LOG(LogWorldGenRT, Display, TEXT("[E2E] === playthrough starts at %s (no teleports, no direct calls) ==="),
		*Player->GetActorLocation().ToCompactString());
	if (TestDoor.IsValid())
	{
		const USphereComponent* Zone = TestDoor->GetInteractZone();
		UE_LOG(LogWorldGenRT, Display, TEXT("[E2E] door zone diagnostics: loc=%s radius=%.1f enabled=%d genOverlap=%d registered=%d"),
			*Zone->GetComponentLocation().ToCompactString(),
			Zone->GetScaledSphereRadius(),
			static_cast<int32>(Zone->GetCollisionEnabled()),
			Zone->GetGenerateOverlapEvents() ? 1 : 0,
			Zone->IsRegistered() ? 1 : 0);
	}
	E2EState = EE2EState::SeekFirstDoor;
	E2ETicks = 0;
	InjectKey(EKeys::W, IE_Pressed, 1.0f);
	UE_LOG(LogWorldGenRT, Display, TEXT("[E2E] W held: walking from spawn towards the door"));

	GetWorldTimerManager().SetTimer(TestTimerHandle, this, &AWorldGenGameMode::RunE2ETick, 0.2f, true);
}

void AWorldGenGameMode::RunE2ETick()
{
	AWorldGenGameState* GS = GetWorld()->GetGameState<AWorldGenGameState>();
	AWorldGenCharacter* Player = GetPlayerChar();
	if (!Player || !GS || E2EState == EE2EState::Done)
	{
		return;
	}
	const FVector Pos = Player->GetActorLocation();

	// Movement diagnostics: proves whether injected input actually drives the
	// capsule and what movement mode it is in.
	if ((E2ETicks % 5) == 0)
	{
		const UCharacterMovementComponent* Move = Player->GetCharacterMovement();
		const APlayerController* PC = GetPC();
		const bool bWDown = PC && PC->IsInputKeyDown(EKeys::W);
		const float WAxis = (PC && PC->PlayerInput) ? PC->PlayerInput->GetKeyValue(EKeys::W) : -1.0f;
		const bool bDoorZoneOverlap = TestDoor.IsValid() && TestDoor->GetInteractZone()->IsOverlappingActor(Player);
		UE_LOG(LogWorldGenRT, Display, TEXT("[E2E debug] tick=%d state=%d pos=%s vel=%s mode=%d Wdown=%d Waxis=%.2f lastInput=%s doorZone=%d"),
			E2ETicks, static_cast<int32>(E2EState), *Pos.ToCompactString(),
			*Player->GetVelocity().ToCompactString(), static_cast<int32>(Move->MovementMode),
			bWDown ? 1 : 0, WAxis,
			*Move->GetLastInputVector().ToCompactString(),
			bDoorZoneOverlap ? 1 : 0);
	}

	switch (E2EState)
	{
	case EE2EState::SeekFirstDoor:
	{
		++E2ETicks;
		if (E2ETicks > 40)
		{
			E2EAbort(1, FString::Printf(TEXT("timeout walking to door, stuck at %s"), *Pos.ToCompactString()));
			return;
		}
		if (FVector::Dist2D(Pos, DoorLoc) < 160.0f)
		{
			InjectKey(EKeys::W, IE_Released, 0.0f);
			InjectKey(EKeys::E, IE_Pressed, 1.0f);
			E2EState = EE2EState::ProbeInteractLocked;
			E2ETicks = 0;
			UE_LOG(LogWorldGenRT, Display, TEXT("[E2E] arrived at door area %s, pressing E WITHOUT the key"),
				*Pos.ToCompactString());
		}
		break;
	}
	case EE2EState::ProbeInteractLocked:
	{
		++E2ETicks;
		if (E2ETicks >= 2)
		{
			InjectKey(EKeys::E, IE_Released, 0.0f);
			const bool bOpen = TestDoor.IsValid() && TestDoor->IsOpen();
			RecordE2E(1, TEXT("E on locked door leaves it closed"), !bOpen);
			if (bOpen)
			{
				E2EAbort(1, TEXT("door opened without the key"));
				return;
			}
			E2EState = EE2EState::PushClosedDoor;
			E2ETicks = 0;
			InjectKey(EKeys::W, IE_Pressed, 1.0f);
			UE_LOG(LogWorldGenRT, Display, TEXT("[E2E] pushing against the closed door for ~2.4s from X=%.1f"), Pos.X);
		}
		break;
	}
	case EE2EState::PushClosedDoor:
	{
		++E2ETicks;
		if (E2ETicks >= 12)
		{
			InjectKey(EKeys::W, IE_Released, 0.0f);
			E2EState = EE2EState::CheckClosedBlock;
			E2ETicks = 0;
		}
		break;
	}
	case EE2EState::CheckClosedBlock:
	{
		UE_LOG(LogWorldGenRT, Display, TEXT("[E2E] push finished at %s"), *Pos.ToCompactString());
		if (Pos.X >= 380.0f)
		{
			E2EAbort(2, FString::Printf(TEXT("closed door did NOT block, player reached X=%.1f (>=380)"), Pos.X));
			return;
		}
		RecordE2E(2, TEXT("closed door blocks the doorway"), true);

		// Turn around (setup rotation, movement stays physical) and walk back to the key.
		if (APlayerController* PC = GetPC())
		{
			PC->SetControlRotation(FRotator(0.0f, 180.0f, 0.0f));
		}
		InjectKey(EKeys::W, IE_Pressed, 1.0f);
		E2EState = EE2EState::ReturnToKey;
		E2ETicks = 0;
		UE_LOG(LogWorldGenRT, Display, TEXT("[E2E] turned around, walking back to the key at (%.0f,%.0f)"),
			KeyLoc.X, KeyLoc.Y);
		break;
	}
	case EE2EState::ReturnToKey:
	{
		++E2ETicks;
		if (E2ETicks > 40)
		{
			E2EAbort(3, FString::Printf(TEXT("timeout walking back to key, stuck at %s"), *Pos.ToCompactString()));
			return;
		}
		if (FVector::Dist2D(Pos, KeyLoc) < 110.0f)
		{
			InjectKey(EKeys::W, IE_Released, 0.0f);
			InjectKey(EKeys::E, IE_Pressed, 1.0f);
			E2EState = EE2EState::InteractKey;
			E2ETicks = 0;
			UE_LOG(LogWorldGenRT, Display, TEXT("[E2E] arrived at key %s, pressing E to pick it up"),
				*Pos.ToCompactString());
		}
		break;
	}
	case EE2EState::InteractKey:
	{
		++E2ETicks;
		if (E2ETicks >= 2)
		{
			InjectKey(EKeys::E, IE_Released, 0.0f);
			if (!GS->HasKey())
			{
				E2EAbort(3, TEXT("E press near the key did not set the key state"));
				return;
			}
			RecordE2E(3, TEXT("E press picks up the key"), true);

			if (APlayerController* PC = GetPC())
			{
				PC->SetControlRotation(FRotator(0.0f, 0.0f, 0.0f));
			}
			InjectKey(EKeys::W, IE_Pressed, 1.0f);
			E2EState = EE2EState::ReturnToDoor;
			E2ETicks = 0;
			UE_LOG(LogWorldGenRT, Display, TEXT("[E2E] key held, walking back to the door"));
		}
		break;
	}
	case EE2EState::ReturnToDoor:
	{
		++E2ETicks;
		if (E2ETicks > 40)
		{
			E2EAbort(4, FString::Printf(TEXT("timeout walking back to door, stuck at %s"), *Pos.ToCompactString()));
			return;
		}
		if (FVector::Dist2D(Pos, DoorLoc) < 160.0f)
		{
			InjectKey(EKeys::W, IE_Released, 0.0f);
			InjectKey(EKeys::E, IE_Pressed, 1.0f);
			E2EState = EE2EState::InteractDoorOpen;
			E2ETicks = 0;
			UE_LOG(LogWorldGenRT, Display, TEXT("[E2E] arrived at door %s, pressing E WITH the key"),
				*Pos.ToCompactString());
		}
		break;
	}
	case EE2EState::InteractDoorOpen:
	{
		++E2ETicks;
		if (E2ETicks >= 2)
		{
			InjectKey(EKeys::E, IE_Released, 0.0f);
			if (!TestDoor.IsValid() || !TestDoor->IsOpen())
			{
				E2EAbort(4, TEXT("E press with key did not open the door"));
				return;
			}
			RecordE2E(4, TEXT("E press opens the door"), true);

			InjectKey(EKeys::W, IE_Pressed, 1.0f);
			E2EState = EE2EState::WalkToExit;
			E2ETicks = 0;
			UE_LOG(LogWorldGenRT, Display, TEXT("[E2E] door open, walking through the doorway"));
		}
		break;
	}
	case EE2EState::WalkToExit:
	{
		++E2ETicks;
		if (E2ETicks > 40)
		{
			E2EAbort(5, FString::Printf(TEXT("timeout walking to exit, stuck at %s"), *Pos.ToCompactString()));
			return;
		}
		if (Pos.X > 520.0f)
		{
			InjectKey(EKeys::W, IE_Released, 0.0f);
			E2EState = EE2EState::WaitMission;
			E2ETicks = 0;
			UE_LOG(LogWorldGenRT, Display, TEXT("[E2E] through the doorway at %s (past X=520), waiting for the exit overlap"),
				*Pos.ToCompactString());
		}
		break;
	}
	case EE2EState::WaitMission:
	{
		++E2ETicks;
		if (E2ETicks >= 3)
		{
			if (!GS->IsMissionComplete())
			{
				E2EAbort(5, FString::Printf(TEXT("exit overlap did not complete the mission, player at %s"), *Pos.ToCompactString()));
				return;
			}
			RecordE2E(5, TEXT("walked entry -> key -> door -> exit, mission complete"), true);

			// M1c-A: assert the capsule is fully outside the exit wall's outer
			// face. TestArena is hand-built with a legacy door yaw, so the east
			// wall outward normal is hardcoded +X (wall plane = hinge X).
			AWorldGenCharacter* P = GetPlayerChar();
			const float CapsuleR = (P && P->GetCapsuleComponent())
				? P->GetCapsuleComponent()->GetScaledCapsuleRadius() : 0.0f;
			const float OutsideDist = Pos.X - (DoorLoc.X + M1cWallThicknessCm * 0.5f);
			UE_LOG(LogWorldGenRT, Display, TEXT("[E2E] completion diagnostics: pos=%s capsule_r=%.1f wall_plane_x=%.1f outer_face_x=%.1f outside_dist_x=%.1f (need >= %.1f)"),
				*Pos.ToCompactString(), CapsuleR, DoorLoc.X, DoorLoc.X + M1cWallThicknessCm * 0.5f, OutsideDist, CapsuleR);
			RecordE2E(6, FString::Printf(
				TEXT("capsule fully outside the exit wall at completion (outside_dist_x=%.1f >= capsule_r=%.1f, east wall outer face at X=%.0f)"),
				OutsideDist, CapsuleR, DoorLoc.X + M1cWallThicknessCm * 0.5f),
				OutsideDist >= CapsuleR);

			UE_LOG(LogWorldGenRT, Display, TEXT("[E2E] final position %s"), *Pos.ToCompactString());
			E2EState = EE2EState::Done;
			GetWorldTimerManager().ClearTimer(TestTimerHandle);
			FinishAllTests();
		}
		break;
	}
	default:
		break;
	}
}

// ---------------------------------------------------------------- summary

void AWorldGenGameMode::FinishAllTests()
{
	UE_LOG(LogWorldGenRT, Display, TEXT("[M05Test] STATE TEST: %d checks, %d failures"),
		StateChecks, StateFailCount);
	UE_LOG(LogWorldGenRT, Display, TEXT("[M05Test] E2E PLAYTHROUGH: %d checks, %d failures"),
		E2EChecks, E2EFailCount);

	const int32 TotalFailures = StateFailCount + E2EFailCount;
	UE_LOG(LogWorldGenRT, Display, TEXT("[M05Test] TOTAL SUMMARY: %d failures -> %s"),
		TotalFailures, TotalFailures == 0 ? TEXT("ALL PASS") : TEXT("FAILED"));

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(9999, 15.0f,
			TotalFailures == 0 ? FColor::Green : FColor::Red,
			FString::Printf(TEXT("M05 TOTAL: state %d/%d ok, e2e %d/%d ok -> %s"),
				StateChecks - StateFailCount, StateChecks,
				E2EChecks - E2EFailCount, E2EChecks,
				TotalFailures == 0 ? TEXT("ALL PASS") : TEXT("FAILED")));
	}

	if (GetWorld()->IsPlayInEditor())
	{
		AWorldGenGameState::BroadcastMessage(TEXT("M05 test suites finished (PIE stays open)"),
			TotalFailures == 0 ? FColor::Green : FColor::Red);
	}
	else
	{
		UE_LOG(LogWorldGenRT, Display, TEXT("[M05Test] standalone run complete, exit code will be %d"),
			TotalFailures);
		GLog->Flush();
		// Force path: TerminateProcess(GetCurrentProcess(), TotalFailures) AFTER the
		// log flush, because 5.7's main loop drops PostQuitMessage's wParam and
		// always returns 0.
		FPlatformMisc::RequestExitWithStatus(true, static_cast<uint8>(TotalFailures));
	}
}

// ---------------------------------------------------------------- M1b: plan generation

void AWorldGenGameMode::RunM1bGenerate()
{
	FString SpecArg;
	if (!FParse::Value(FCommandLine::Get(), TEXT("M1bSpec="), SpecArg))
	{
		SpecArg = TEXT("Data/Scenes/valid/valid_workroom_001.json");
	}
	const FString SpecPath = FPaths::IsRelative(SpecArg) ? FPaths::ProjectDir() / SpecArg : SpecArg;

	UE_LOG(LogWorldGenRT, Display, TEXT("[M1bGen] validating spec: %s"), *SpecPath);

	FSceneSpec Spec;
	TArray<FString> Errors;
	FString ReadErr;
	const bool bValid = FSceneSpecValidator::ValidateFile(SpecPath, Spec, Errors, ReadErr);

	if (!ReadErr.IsEmpty() || !bValid)
	{
		UE_LOG(LogWorldGenRT, Error, TEXT("[M1bGen] SPEC INVALID - no plan written, no level may be generated (%d error(s))"),
			Errors.Num() + (ReadErr.IsEmpty() ? 0 : 1));
		for (const FString& Err : Errors)
		{
			UE_LOG(LogWorldGenRT, Error, TEXT("[M1bGen]   - %s"), *Err);
		}
		if (!ReadErr.IsEmpty())
		{
			UE_LOG(LogWorldGenRT, Error, TEXT("[M1bGen]   - %s"), *ReadErr);
		}
		GLog->Flush();
		FPlatformMisc::RequestExitWithStatus(true, 2);
		return;
	}

	// Determinism: build the plan twice and compare bytes.
	const FString FileName = FPaths::GetCleanFilename(SpecPath);
	FString Plan1, Plan2;
	TArray<FString> PlanErr1, PlanErr2;
	const bool bOk1 = FLevelPlanBuilder::BuildPlan(Spec, FileName, Plan1, PlanErr1);
	FLevelPlanBuilder::BuildPlan(Spec, FileName, Plan2, PlanErr2);

	if (!bOk1 || PlanErr1.Num() > 0)
	{
		UE_LOG(LogWorldGenRT, Error, TEXT("[M1bGen] spec cannot be realized with the M1b-1 placeholder set:"));
		for (const FString& Err : PlanErr1)
		{
			UE_LOG(LogWorldGenRT, Error, TEXT("[M1bGen]   - %s"), *Err);
		}
		GLog->Flush();
		FPlatformMisc::RequestExitWithStatus(true, 2);
		return;
	}
	if (Plan1 != Plan2 || PlanErr1.Num() != PlanErr2.Num())
	{
		UE_LOG(LogWorldGenRT, Error, TEXT("[M1bGen] plan is not deterministic across two builds"));
		GLog->Flush();
		FPlatformMisc::RequestExitWithStatus(true, 3);
		return;
	}

	const FString PlanPath = FPaths::ProjectDir() / TEXT("Data/Reports") / (Spec.SceneId + TEXT(".plan.json"));
	if (!FFileHelper::SaveStringToFile(Plan1, *PlanPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogWorldGenRT, Error, TEXT("[M1bGen] cannot write plan file %s"), *PlanPath);
		GLog->Flush();
		FPlatformMisc::RequestExitWithStatus(true, 4);
		return;
	}

	UE_LOG(LogWorldGenRT, Display, TEXT("[M1bGen] plan written: %s (%d chars, deterministic across 2 builds)"), *PlanPath, Plan1.Len());

	// M1c: pointer file so the Python level executor picks up the LATEST plan
	// (any scene_id), while remaining deterministic content-wise.
	const FString PointerPath = FPaths::ProjectDir() / TEXT("Data/Reports/m1b_current_plan.txt");
	FFileHelper::SaveStringToFile(PlanPath, *PointerPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

	GLog->Flush();
	FPlatformMisc::RequestExitWithStatus(true, 0);
}

// ---------------------------------------------------------------- M1c: single-spec validation (AI planner feedback loop)

void AWorldGenGameMode::RunM1cValidate()
{
	FString SpecArg;
	if (!FParse::Value(FCommandLine::Get(), TEXT("M1cValidateSpec="), SpecArg) || SpecArg.IsEmpty())
	{
		UE_LOG(LogWorldGenRT, Error, TEXT("[M1cTest] -M1cValidateSpec requires a spec path"));
		GLog->Flush();
		FPlatformMisc::RequestExitWithStatus(true, 2);
		return;
	}
	FString ErrOutArg;
	FString ErrOutPath;
	if (FParse::Value(FCommandLine::Get(), TEXT("M1cErrorOut="), ErrOutArg) && !ErrOutArg.IsEmpty())
	{
		ErrOutPath = FPaths::IsRelative(ErrOutArg) ? FPaths::ProjectDir() / ErrOutArg : ErrOutArg;
	}

	const FString SpecPath = FPaths::IsRelative(SpecArg) ? FPaths::ProjectDir() / SpecArg : SpecArg;
	FSceneSpec Spec;
	TArray<FString> Errors;
	FString ReadErr;
	const bool bValid = FSceneSpecValidator::ValidateFile(SpecPath, Spec, Errors, ReadErr);

	FString Report;
	if (bValid)
	{
		Report = TEXT("VALID\n");
		UE_LOG(LogWorldGenRT, Display, TEXT("[M1cTest] spec VALID: %s"), *Spec.MakeCanonicalSummary());
	}
	else
	{
		Report = TEXT("INVALID\n");
		for (const FString& Err : Errors)
		{
			Report += Err + TEXT("\n");
		}
		if (!ReadErr.IsEmpty())
		{
			Report += ReadErr + TEXT("\n");
		}
		UE_LOG(LogWorldGenRT, Warning, TEXT("[M1cTest] spec INVALID (%d error(s)): %s"), Errors.Num() + (ReadErr.IsEmpty() ? 0 : 1), *SpecPath);
	}

	if (!ErrOutPath.IsEmpty())
	{
		if (!FFileHelper::SaveStringToFile(Report, *ErrOutPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			UE_LOG(LogWorldGenRT, Error, TEXT("[M1cTest] cannot write error file %s"), *ErrOutPath);
			GLog->Flush();
			FPlatformMisc::RequestExitWithStatus(true, 3);
			return;
		}
		UE_LOG(LogWorldGenRT, Display, TEXT("[M1cTest] result written: %s"), *ErrOutPath);
	}

	GLog->Flush();
	// 0 = valid, 1 = invalid (the planner treats 1 as "feed errors back to the model").
	FPlatformMisc::RequestExitWithStatus(true, bValid ? 0 : 1);
}

// ---------------------------------------------------------------- M1c: combined spec + plan-derived placement check
// M2b multi-scene: the AI planner's feedback gate. One editor launch performs
//  1. SceneSpec validation (schema),
//  2. FLevelPlanBuilder (realizable geometry),
//  3. placement checks against the SERIALIZED plan coordinates - the plan JSON
//     is the single source of truth, so this can never drift from the C++
//     coordinate math (the former Python mirror could).

void AWorldGenGameMode::RunM1cCheck()
{
	FString SpecArg;
	if (!FParse::Value(FCommandLine::Get(), TEXT("M1cCheckSpec="), SpecArg) || SpecArg.IsEmpty())
	{
		UE_LOG(LogWorldGenRT, Error, TEXT("[M1cCheck] -M1cCheckSpec requires a spec path"));
		GLog->Flush();
		FPlatformMisc::RequestExitWithStatus(true, 2);
		return;
	}
	FString ErrOutArg;
	FString ErrOutPath;
	if (FParse::Value(FCommandLine::Get(), TEXT("M1cErrorOut="), ErrOutArg) && !ErrOutArg.IsEmpty())
	{
		ErrOutPath = FPaths::IsRelative(ErrOutArg) ? FPaths::ProjectDir() / ErrOutArg : ErrOutArg;
	}

	const FString SpecPath = FPaths::IsRelative(SpecArg) ? FPaths::ProjectDir() / SpecArg : SpecArg;
	FSceneSpec Spec;
	TArray<FString> Errors;
	FString ReadErr;
	bool bOk = FSceneSpecValidator::ValidateFile(SpecPath, Spec, Errors, ReadErr);
	if (!ReadErr.IsEmpty())
	{
		Errors.Add(ReadErr);
	}

	if (bOk)
	{
		FString PlanJson;
		TArray<FString> PlanErrs;
		if (!FLevelPlanBuilder::BuildPlan(Spec, FPaths::GetBaseFilename(SpecPath), PlanJson, PlanErrs))
		{
			bOk = false;
			Errors.Append(PlanErrs);
		}
		else
		{
			// Inspect the SERIALIZED plan: player_start vs object boxes.
			TSharedPtr<FJsonObject> Root;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PlanJson);
			bool bParsed = FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid();
			const TArray<TSharedPtr<FJsonValue>>* OpsArr = nullptr;
			const TSharedPtr<FJsonValue> OpsVal = bParsed ? Root->TryGetField(TEXT("ops")) : nullptr;
			if (!OpsVal.IsValid() || !OpsVal->TryGetArray(OpsArr) || OpsArr == nullptr)
			{
				bOk = false;
				Errors.Add(TEXT("plan.ops: generated plan could not be parsed (internal error)"));
			}
			else
			{
				double PX = 0.0, PY = 0.0;
				bool bHavePlayerStart = false;
				struct FObjBox { FString Name; double X, Y, Yaw, SX, SY; };
				TArray<FObjBox> ObjBoxes;
				for (const TSharedPtr<FJsonValue>& V : *OpsArr)
				{
					const TSharedPtr<FJsonObject>* OP = nullptr;
					if (!V.IsValid() || !V->TryGetObject(OP) || OP == nullptr)
					{
						continue;
					}
					FString Op, Name;
					const TSharedPtr<FJsonValue> OpVal = (*OP)->TryGetField(TEXT("op"));
					const TSharedPtr<FJsonValue> NameVal = (*OP)->TryGetField(TEXT("name"));
					if (!OpVal.IsValid() || !OpVal->TryGetString(Op) ||
						!NameVal.IsValid() || !NameVal->TryGetString(Name))
					{
						continue;
					}
					const TArray<TSharedPtr<FJsonValue>>* Loc = nullptr;
					const TSharedPtr<FJsonValue> LocVal = (*OP)->TryGetField(TEXT("location_cm"));
					if (!LocVal.IsValid() || !LocVal->TryGetArray(Loc) || Loc == nullptr || Loc->Num() < 2)
					{
						continue;
					}
					double LX = 0.0, LY = 0.0;
					(*Loc)[0]->TryGetNumber(LX);
					(*Loc)[1]->TryGetNumber(LY);

					if (Op == TEXT("player_start"))
					{
						PX = LX; PY = LY;
						bHavePlayerStart = true;
					}
					else if (Op == TEXT("box") && Name.StartsWith(TEXT("WG_obj_")))
					{
						const TArray<TSharedPtr<FJsonValue>>* Size = nullptr;
						const TSharedPtr<FJsonValue> SizeVal = (*OP)->TryGetField(TEXT("size_cm"));
						double Yaw = 0.0;
						const TSharedPtr<FJsonValue> YawVal = (*OP)->TryGetField(TEXT("yaw_deg"));
						YawVal.IsValid() && YawVal->TryGetNumber(Yaw);
						if (SizeVal.IsValid() && SizeVal->TryGetArray(Size) && Size != nullptr && Size->Num() >= 3)
						{
							double S[3] = {0.0, 0.0, 0.0};
							for (int32 i = 0; i < 3; ++i)
							{
								(*Size)[i]->TryGetNumber(S[i]);
							}
							ObjBoxes.Add({Name, LX, LY, Yaw, S[0], S[1]});
						}
					}
				}

				if (!bHavePlayerStart)
				{
					bOk = false;
					Errors.Add(TEXT("plan.ops: no player_start op in the generated plan"));
				}
				else
				{
					const double Margin = FMath::Max(Spec.AgentRadiusCm, 34.0) + 10.0;
					for (const FObjBox& B : ObjBoxes)
					{
						const double Rad = FMath::DegreesToRadians(B.Yaw);
						const double C = FMath::Abs(FMath::Cos(Rad));
						const double S = FMath::Abs(FMath::Sin(Rad));
						const double HX = (B.SX * C + B.SY * S) / 2.0;
						const double HY = (B.SX * S + B.SY * C) / 2.0;
						if (FMath::Abs(PX - B.X) < HX + Margin && FMath::Abs(PY - B.Y) < HY + Margin)
						{
							bOk = false;
							Errors.Add(FString::Printf(
								TEXT("placement: object '%s' at (%.0f, %.0f) blocks the player spawn point (%.0f, %.0f): rotated half extents %.0f x %.0f plus capsule margin %.0f. Move the object clear of the spawn point and of the straight entrance-to-exit walk line."),
								*B.Name, B.X, B.Y, PX, PY, HX, HY, Margin));
						}
					}
				}

				// M2b multi-scene: doorway approach corridor. Observed in a real
				// AI level: a crate 60 cm in front of the west doorway sealed it
				// (doorway width 120, gap 60 < capsule diameter 70) - the door
				// opened but the level was unwinnable. Conservative AABB test:
				// the rotated footprint expanded by the capsule radius must not
				// intersect the 150 cm deep corridor in front of either doorway.
				const double CapR = FMath::Max(Spec.AgentRadiusCm, 34.0);
				const double Depth = 150.0;
				auto CheckCorridor = [&](const FSceneSpecDoor& Door, double RunLen, const FString& Label)
				{
					const double Center = -RunLen / 2.0 + Door.OffsetCm;
					const double Lo = Center - Door.WidthCm / 2.0;
					const double Hi = Center + Door.WidthCm / 2.0;
					const bool bNS = (Door.Wall == TEXT("north") || Door.Wall == TEXT("south"));
					for (const FObjBox& B : ObjBoxes)
					{
						const double Rad = FMath::DegreesToRadians(B.Yaw);
						const double C = FMath::Abs(FMath::Cos(Rad));
						const double S = FMath::Abs(FMath::Sin(Rad));
						const double HX = (B.SX * C + B.SY * S) / 2.0 + CapR + 5.0;
						const double HY = (B.SX * S + B.SY * C) / 2.0 + CapR + 5.0;
						const double BLoX = B.X - HX, BHiX = B.X + HX;
						const double BLoY = B.Y - HY, BHiY = B.Y + HY;
						bool bBlocked = false;
						if (Door.Wall == TEXT("north"))
						{
							bBlocked = BHiX > Spec.RoomWidthCm / 2.0 - Depth && BLoX < Spec.RoomWidthCm / 2.0 && BHiY > Lo && BLoY < Hi;
						}
						else if (Door.Wall == TEXT("south"))
						{
							bBlocked = BHiX > -Spec.RoomWidthCm / 2.0 && BLoX < -Spec.RoomWidthCm / 2.0 + Depth && BHiY > Lo && BLoY < Hi;
						}
						else if (Door.Wall == TEXT("east"))
						{
							bBlocked = BHiY > Spec.RoomLengthCm / 2.0 - Depth && BLoY < Spec.RoomLengthCm / 2.0 && BHiX > Lo && BLoX < Hi;
						}
						else
						{
							bBlocked = BHiY > -Spec.RoomLengthCm / 2.0 && BLoY < -Spec.RoomLengthCm / 2.0 + Depth && BHiX > Lo && BLoX < Hi;
						}
						if (bBlocked)
						{
							bOk = false;
							// Give the model the concrete forbidden rectangle in
							// room coordinates - without it the model repeats its
							// answer verbatim (observed: 3 identical rounds).
							FString Rect;
							if (Door.Wall == TEXT("north"))
							{
								Rect = FString::Printf(TEXT("X in [%.0f, %.0f], Y in [%.0f, %.0f]"), Spec.RoomWidthCm / 2.0 - Depth, Spec.RoomWidthCm / 2.0, Lo, Hi);
							}
							else if (Door.Wall == TEXT("south"))
							{
								Rect = FString::Printf(TEXT("X in [%.0f, %.0f], Y in [%.0f, %.0f]"), -Spec.RoomWidthCm / 2.0, -Spec.RoomWidthCm / 2.0 + Depth, Lo, Hi);
							}
							else if (Door.Wall == TEXT("east"))
							{
								Rect = FString::Printf(TEXT("X in [%.0f, %.0f], Y in [%.0f, %.0f]"), Lo, Hi, Spec.RoomLengthCm / 2.0 - Depth, Spec.RoomLengthCm / 2.0);
							}
							else
							{
								Rect = FString::Printf(TEXT("X in [%.0f, %.0f], Y in [%.0f, %.0f]"), Lo, Hi, -Spec.RoomLengthCm / 2.0, -Spec.RoomLengthCm / 2.0 + Depth);
							}
							bOk = false;
							Errors.Add(FString::Printf(
								TEXT("placement: object '%s' at (%.0f, %.0f) intrudes into the approach corridor of the %s doorway. The forbidden corridor rectangle is %s (room coordinates, before the %.0f cm capsule clearance). Move this object so its whole footprint lies at least %.0f cm away from that rectangle."),
								*B.Name, B.X, B.Y, *Label, *Rect, CapR, CapR));
						}
					}
				};
				auto IsNS = [](const FString& Wall) { return Wall == TEXT("north") || Wall == TEXT("south"); };
				CheckCorridor(Spec.Exit, IsNS(Spec.Exit.Wall) ? Spec.RoomLengthCm : Spec.RoomWidthCm, TEXT("exit"));
				CheckCorridor(Spec.Entrance, IsNS(Spec.Entrance.Wall) ? Spec.RoomLengthCm : Spec.RoomWidthCm, TEXT("entrance"));
			}
		}
	}

	FString Report;
	if (bOk)
	{
		Report = TEXT("VALID\n");
		UE_LOG(LogWorldGenRT, Display, TEXT("[M1cCheck] spec + placement VALID: %s"), *Spec.MakeCanonicalSummary());
	}
	else
	{
		Report = TEXT("INVALID\n");
		for (const FString& Err : Errors)
		{
			Report += Err + TEXT("\n");
		}
		UE_LOG(LogWorldGenRT, Warning, TEXT("[M1cCheck] spec + placement INVALID (%d error(s)): %s"), Errors.Num(), *SpecPath);
	}

	if (!ErrOutPath.IsEmpty())
	{
		if (!FFileHelper::SaveStringToFile(Report, *ErrOutPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			UE_LOG(LogWorldGenRT, Error, TEXT("[M1cCheck] cannot write error file %s"), *ErrOutPath);
			GLog->Flush();
			FPlatformMisc::RequestExitWithStatus(true, 3);
			return;
		}
		UE_LOG(LogWorldGenRT, Display, TEXT("[M1cCheck] result written: %s"), *ErrOutPath);
	}

	GLog->Flush();
	FPlatformMisc::RequestExitWithStatus(true, bOk ? 0 : 1);
}

// ---------------------------------------------------------------- M1b: generated-level playthrough

void AWorldGenGameMode::StartM1bE2E()
{
	int32 DoorCount = 0, PickupCount = 0, ExitCount = 0;
	for (TActorIterator<AWorldGenDoor> It(GetWorld()); It; ++It)
	{
		M1bDoor = *It;
		++DoorCount;
	}
	for (TActorIterator<AWorldGenPickup> It2(GetWorld()); It2; ++It2)
	{
		M1bPickup = *It2;
		++PickupCount;
	}
	for (TActorIterator<AWorldGenExitTrigger> It3(GetWorld()); It3; ++It3)
	{
		M1bExit = *It3;
		++ExitCount;
	}

	AWorldGenGameState* GS = GetWorld()->GetGameState<AWorldGenGameState>();
	AWorldGenCharacter* Player = GetPlayerChar();
	if (DoorCount != 1 || PickupCount != 1 || ExitCount != 1 || !GS || !Player)
	{
		UE_LOG(LogWorldGenRT, Error, TEXT("[M1bTest] level inventory wrong: doors=%d pickups=%d exits=%d gs=%d player=%d"),
			DoorCount, PickupCount, ExitCount, GS ? 1 : 0, Player ? 1 : 0);
		++M1bFailCount;
		FinishM1b();
		return;
	}

	M1bKeyLoc = M1bPickup->GetActorLocation();
	M1bHingeLoc = M1bDoor->GetActorLocation();  // hinge sits ON the wall plane
	M1bExitLoc = M1bExit->GetActorLocation();

	// Outward normal through the exit doorway: door yaw 0 -> panel +Y (north
	// wall, outward +X); yaw 90 -> east wall outward +Y; etc. (M1b convention).
	M1bOutward = M1bDoor->GetActorRotation().RotateVector(FVector(1.0f, 0.0f, 0.0f));

	// Indoor-side counterexample point: 50 cm inside the wall plane and 230 cm
	// along the wall from the doorway center. With the key, standing here must
	// NOT complete the mission (M1c-A acceptance).
	M1bProbeLoc = M1bHingeLoc - M1bOutward * 50.0f +
		M1bDoor->GetActorRotation().RotateVector(FVector(0.0f, 230.0f, 0.0f));

	UE_LOG(LogWorldGenRT, Display, TEXT("[M1bTest] === generated-level playthrough starts at %s (from entrance; steering + injected W/E; no teleports, no direct calls) ==="),
		*Player->GetActorLocation().ToCompactString());
	UE_LOG(LogWorldGenRT, Display, TEXT("[M1bTest] key=(%.1f,%.1f,%.1f) door_hinge=(%.1f,%.1f,%.1f) exit_trigger=(%.1f,%.1f,%.1f)"),
		M1bKeyLoc.X, M1bKeyLoc.Y, M1bKeyLoc.Z,
		M1bHingeLoc.X, M1bHingeLoc.Y, M1bHingeLoc.Z,
		M1bExitLoc.X, M1bExitLoc.Y, M1bExitLoc.Z);
	UE_LOG(LogWorldGenRT, Display, TEXT("[M1bTest] wall plane at hinge; outward=(%.2f,%.2f) indoor_probe=(%.1f,%.1f)"),
		M1bOutward.X, M1bOutward.Y, M1bProbeLoc.X, M1bProbeLoc.Y);

	// Walk target for the door: the middle of the doorway, not the hinge —
	// approaching the hinge would put the capsule inside the panel when it
	// swings open.
	const FVector DoorTarget = M1bHingeLoc +
		M1bDoor->GetActorRotation().RotateVector(FVector(0.0f, 60.0f, 0.0f));
	M1bDoorLoc = DoorTarget;
	UE_LOG(LogWorldGenRT, Display, TEXT("[M1bTest] door approach target (doorway center) = (%.1f,%.1f)"),
		M1bDoorLoc.X, M1bDoorLoc.Y);

	M1bState = EM1bState::SeekKey;
	M1bTicks = 0;
	// Steer BEFORE pressing W: without this the first ~0.2 s of walking uses
	// the stale spawn heading (steering only updates on the timer tick).
	M1bSteerTowards(M1bKeyLoc);
	InjectKey(EKeys::W, IE_Pressed, 1.0f);
	GetWorldTimerManager().SetTimer(TestTimerHandle, this, &AWorldGenGameMode::RunM1bE2ETick, 0.2f, true);
}

void AWorldGenGameMode::M1bSteerTowards(const FVector& Target)
{
	AWorldGenCharacter* Player = GetPlayerChar();
	APlayerController* PC = GetPC();
	if (!Player || !PC)
	{
		return;
	}
	// While a detour waypoint is active, steer at it instead of the target.
	const FVector T = M1bDetourPoint.IsNearlyZero() ? Target : M1bDetourPoint;
	const FVector D = T - Player->GetActorLocation();
	const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X));
	PC->SetControlRotation(FRotator(0.0f, Yaw, 0.0f));
}

void AWorldGenGameMode::M1bUpdateStall(const FVector& Pos, const FVector& Target)
{
	// New steering target: reset the stall state and drop any old detour.
	if (!Target.Equals(M1bStallTarget, 1.0f))
	{
		M1bStallTarget = Target;
		M1bDetourPoint = FVector::ZeroVector;
		M1bStallTicks = 0;
		M1bStallRefPos = Pos;
		return;
	}

	// Detour waypoint reached: resume direct steering.
	if (!M1bDetourPoint.IsNearlyZero() && FVector::Dist2D(Pos, M1bDetourPoint) < 60.0f)
	{
		M1bDetourPoint = FVector::ZeroVector;
		M1bStallTicks = 0;
		M1bStallRefPos = Pos;
		UE_LOG(LogWorldGenRT, Display, TEXT("[M1bTest] detour reached at %s, resuming direct steering"), *Pos.ToCompactString());
		return;
	}

	// Real progress made: reset the stall counter.
	if (FVector::Dist2D(Pos, M1bStallRefPos) > 15.0f)
	{
		M1bStallRefPos = Pos;
		M1bStallTicks = 0;
		return;
	}

	// < 15 cm over 6 ticks (~1.2 s): stuck. Insert a 150 cm perpendicular
	// bypass waypoint, alternating sides on repeated stalls.
	if (++M1bStallTicks < 6)
	{
		return;
	}
	M1bStallTicks = 0;
	const FVector D(Target.X - Pos.X, Target.Y - Pos.Y, 0.0f);
	const FVector Perp = FVector(-D.Y, D.X, 0.0f).GetSafeNormal();
	if (!Perp.IsNearlyZero())
	{
		const float Sign = (M1bStallSide % 2 == 0) ? 1.0f : -1.0f;
		M1bDetourPoint = Pos + Perp * 150.0f * Sign;
		++M1bStallSide;
		UE_LOG(LogWorldGenRT, Display, TEXT("[M1bTest] stuck near %s -> detour via (%.0f,%.0f)"),
			*Pos.ToCompactString(), M1bDetourPoint.X, M1bDetourPoint.Y);
	}
}

void AWorldGenGameMode::RecordM1b(int32 StepIdx, const FString& What, bool bOk)
{
	++M1bChecks;
	if (!bOk)
	{
		++M1bFailCount;
	}
	if (bOk)
	{
		UE_LOG(LogWorldGenRT, Display, TEXT("[M1bTest %d] %s -> PASS"), StepIdx, *What);
	}
	else
	{
		UE_LOG(LogWorldGenRT, Error, TEXT("[M1bTest %d] %s -> FAIL"), StepIdx, *What);
	}
}

void AWorldGenGameMode::M1bAbort(int32 StepIdx, const FString& What)
{
	UE_LOG(LogWorldGenRT, Error, TEXT("[M1bTest] ABORT at step %d: %s"), StepIdx, *What);
	InjectKey(EKeys::W, IE_Released, 0.0f);
	InjectKey(EKeys::E, IE_Released, 0.0f);
	M1bState = EM1bState::Done;
	GetWorldTimerManager().ClearTimer(TestTimerHandle);
	++M1bFailCount;
	FinishM1b();
}

// ---------------------------------------------------------------- M2bs demo capture

void AWorldGenGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (DemoCaptureDir.IsEmpty() || !GEngine || !GetWorld() || !GetWorld()->GetGameViewport())
	{
		return;
	}
	// One request every 2nd frame. With -benchmark the E2E plays out in a few
	// real seconds, so the resulting ~90 shots form a timelapse of the whole
	// run (pickup / door / exit). Requests share one engine pending slot, so
	// some are dropped - acceptable for a demo timelapse.
	if (++DemoFrameCounter % 2 == 0)
	{
		FScreenshotRequest::RequestScreenshot(
			FPaths::Combine(DemoCaptureDir, FString::Printf(TEXT("f_%06d.png"), DemoFrameIdx++)),
			false, false);
	}
}

void AWorldGenGameMode::RunM1bE2ETick()
{
	AWorldGenGameState* GS = GetWorld()->GetGameState<AWorldGenGameState>();
	AWorldGenCharacter* Player = GetPlayerChar();
	if (!Player || !GS || M1bState == EM1bState::Done)
	{
		return;
	}
	const FVector Pos = Player->GetActorLocation();

	// Movement diagnostics every 5 ticks (same pattern as the M0.5 E2E debug).
	if ((M1bTicks % 5) == 0)
	{
		const APlayerController* PC = GetPC();
		const float Yaw = PC ? PC->GetControlRotation().Yaw : -1.0f;
		UE_LOG(LogWorldGenRT, Display, TEXT("[M1b debug] state=%d tick=%d pos=%s vel=%s mode=%d ctrl_yaw=%.1f"),
			static_cast<int32>(M1bState), M1bTicks, *Pos.ToCompactString(),
			*Player->GetVelocity().ToCompactString(),
			static_cast<int32>(Player->GetCharacterMovement()->MovementMode), Yaw);
	}

	switch (M1bState)
	{
	case EM1bState::SeekKey:
	{
		++M1bTicks;
		M1bUpdateStall(Pos, M1bKeyLoc);
		M1bSteerTowards(M1bKeyLoc);
		if (M1bTicks > 90)
		{
			M1bAbort(1, FString::Printf(TEXT("timeout walking to the key, stuck at %s"), *Pos.ToCompactString()));
			return;
		}
		if (FVector::Dist2D(Pos, M1bKeyLoc) < 118.0f)
		{
			InjectKey(EKeys::W, IE_Released, 0.0f);
			InjectKey(EKeys::E, IE_Pressed, 1.0f);
			M1bState = EM1bState::InteractKey;
			M1bTicks = 0;
			UE_LOG(LogWorldGenRT, Display, TEXT("[M1bTest] near the key at %s (dist %.1f cm), pressing E"),
				*Pos.ToCompactString(), FVector::Dist2D(Pos, M1bKeyLoc));
		}
		break;
	}
	case EM1bState::InteractKey:
	{
		++M1bTicks;
		if (M1bTicks >= 2)
		{
			InjectKey(EKeys::E, IE_Released, 0.0f);
			if (!GS->HasKey())
			{
				M1bAbort(1, TEXT("E press near the key did not set the key state"));
				return;
			}
			RecordM1b(1, TEXT("walked from the entrance to the key and picked it up with E"), true);
			M1bState = EM1bState::SeekDoor;
			M1bTicks = 0;
			M1bSteerTowards(M1bDoorLoc); // pre-steer: no stale-heading window
			InjectKey(EKeys::W, IE_Pressed, 1.0f);
			UE_LOG(LogWorldGenRT, Display, TEXT("[M1bTest] key held, walking to the door"));
		}
		break;
	}
	case EM1bState::SeekDoor:
	{
		++M1bTicks;
		M1bUpdateStall(Pos, M1bDoorLoc);
		M1bSteerTowards(M1bDoorLoc);
		if (M1bTicks > 90)
		{
			M1bAbort(2, FString::Printf(TEXT("timeout walking to the door, stuck at %s"), *Pos.ToCompactString()));
			return;
		}
		if (FVector::Dist2D(Pos, M1bDoorLoc) < 110.0f)
		{
			InjectKey(EKeys::W, IE_Released, 0.0f);
			InjectKey(EKeys::E, IE_Pressed, 1.0f);
			M1bState = EM1bState::InteractDoor;
			M1bTicks = 0;
			UE_LOG(LogWorldGenRT, Display, TEXT("[M1bTest] at the doorway %s (dist %.1f cm), pressing E with the key"),
				*Pos.ToCompactString(), FVector::Dist2D(Pos, M1bDoorLoc));
		}
		break;
	}
	case EM1bState::InteractDoor:
	{
		++M1bTicks;
		if (M1bTicks >= 2)
		{
			InjectKey(EKeys::E, IE_Released, 0.0f);
			if (!M1bDoor.IsValid() || !M1bDoor->IsOpen())
			{
				M1bAbort(2, TEXT("E press with the key did not open the door"));
				return;
			}
			RecordM1b(2, TEXT("walked to the door and opened it with E"), true);
			M1bState = EM1bState::ProbeWallIndoor;
			M1bTicks = 0;
			// Pre-steer is CRITICAL here: the character usually stands right at
			// the open doorway after the E press; one stale-heading tick at
			// 400 cm/s walks it straight out through the doorway (ai_gallery_777
			// repro: x=352 outside the wall within 0.2 s).
			M1bSteerTowards(M1bProbeLoc);
			InjectKey(EKeys::W, IE_Pressed, 1.0f);
			UE_LOG(LogWorldGenRT, Display, TEXT("[M1bTest] door open, walking to the INDOOR side of the exit wall first (counterexample probe)"));
		}
		break;
	}
	case EM1bState::ProbeWallIndoor:
	{
		++M1bTicks;
		M1bUpdateStall(Pos, M1bProbeLoc);
		M1bSteerTowards(M1bProbeLoc);
		if (M1bTicks > 90)
		{
			M1bAbort(3, FString::Printf(TEXT("timeout walking to the indoor probe point, stuck at %s"), *Pos.ToCompactString()));
			return;
		}
		if (FVector::Dist2D(Pos, M1bProbeLoc) < 80.0f)
		{
			InjectKey(EKeys::W, IE_Released, 0.0f);
			M1bState = EM1bState::SeekExit;
			M1bTicks = 0;
			// The mission must NOT complete here even though the key is held:
			// the trigger's inner face is 2R+20 beyond the wall's outer face.
			RecordM1b(3, FString::Printf(
				TEXT("standing on the INDOOR side of the exit wall with the key does not complete the mission (pos X/Y=%.1f/%.1f, wall plane at hinge)"),
				Pos.X, Pos.Y),
				!GS->IsMissionComplete());
			M1bSteerTowards(M1bExitLoc); // pre-steer: no stale-heading window
			InjectKey(EKeys::W, IE_Pressed, 1.0f);
			UE_LOG(LogWorldGenRT, Display, TEXT("[M1bTest] indoor probe done at %s, walking through the doorway"),
				*Pos.ToCompactString());
		}
		break;
	}
	case EM1bState::SeekExit:
	{
		++M1bTicks;
		M1bUpdateStall(Pos, M1bExitLoc);
		M1bSteerTowards(M1bExitLoc);
		if (M1bTicks > 90)
		{
			M1bAbort(4, FString::Printf(TEXT("timeout walking to the exit, stuck at %s"), *Pos.ToCompactString()));
			return;
		}
		if (GS->IsMissionComplete())
		{
			InjectKey(EKeys::W, IE_Released, 0.0f);

			// M1c-A: completion alone is not acceptance. The capsule must be
			// FULLY outside the exit wall's outer face: signed distance along
			// the outward normal >= capsule radius.
			AWorldGenCharacter* P = GetPlayerChar();
			const float CapsuleR = (P && P->GetCapsuleComponent())
				? P->GetCapsuleComponent()->GetScaledCapsuleRadius() : 0.0f;
			const FVector FacePoint = M1bHingeLoc + M1bOutward * (M1cWallThicknessCm * 0.5f);
			const float OutsideDist = FVector::DotProduct(Pos - FacePoint, M1bOutward);

			UE_LOG(LogWorldGenRT, Display, TEXT("[M1bTest] completion diagnostics: pos=%s capsule_r=%.1f wall_plane=(%.1f,%.1f) outer_face_dist_along_normal=%.1f outside_dist=%.1f (need >= %.1f)"),
				*Pos.ToCompactString(), CapsuleR,
				M1bHingeLoc.X, M1bHingeLoc.Y, M1cWallThicknessCm * 0.5f, OutsideDist, CapsuleR);

			RecordM1b(4, TEXT("walked through the doorway into the exit trigger; mission complete"), true);
			RecordM1b(5, FString::Printf(
				TEXT("capsule fully outside the exit wall at completion (outside_dist=%.1f >= capsule_r=%.1f, wall outer face %.1f cm along outward)"),
				OutsideDist, CapsuleR, M1cWallThicknessCm * 0.5f),
				OutsideDist >= CapsuleR);
			UE_LOG(LogWorldGenRT, Display, TEXT("[M1bTest] final position %s"), *Pos.ToCompactString());
			M1bState = EM1bState::Done;
			GetWorldTimerManager().ClearTimer(TestTimerHandle);
			FinishM1b();
		}
		break;
	}
	default:
		break;
	}
}

void AWorldGenGameMode::FinishM1b()
{
	UE_LOG(LogWorldGenRT, Display, TEXT("[M1bTest] E2E PLAYTHROUGH: %d checks, %d failures -> %s"),
		M1bChecks, M1bFailCount, M1bFailCount == 0 ? TEXT("ALL PASS") : TEXT("FAILED"));

	if (GetWorld()->IsPlayInEditor())
	{
		AWorldGenGameState::BroadcastMessage(
			FString::Printf(TEXT("M1b playthrough: %d checks, %d failures"),
				M1bChecks, M1bFailCount),
			M1bFailCount == 0 ? FColor::Green : FColor::Red);
		return;
	}

	GLog->Flush();
	FPlatformMisc::RequestExitWithStatus(true, static_cast<uint8>(M1bFailCount));
}
