#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "WorldGenGameMode.generated.h"

class AWorldGenCharacter;
class AWorldGenDoor;
class AWorldGenExitTrigger;
class AWorldGenPickup;

/**
 * M0.5 game mode: spawns the walking AWorldGenCharacter by default and hosts
 * two automated test suites:
 *
 *   1. STATE TEST (-M0_5AutoTest phase 1): direct real-logic calls
 *      (Door::Interact / Pickup::PickUp / real exit overlap), including the
 *      input-pipeline check that drives the character via PlayerController::InputKey.
 *
 *   2. E2E PLAYTHROUGH (phase 2, after a state reset): no direct gameplay calls
 *      and no teleports through walls. The character walks from the spawn point
 *      with injected W key input and interacts with injected E presses:
 *      probe the locked door -> confirm the closed panel blocks -> walk back,
 *      pick the key up with E -> open the door with E -> walk through the
 *      doorway to the exit.
 *
 * Command line switches:
 *   -M0AutoQuit     : exit engine 8 s after BeginPlay (M0 standalone runs)
 *   -M0_5AutoTest   : run both suites; standalone exits with code = total
 *                     failure count (0 = all pass)
 *   -M1aValidate    : run the SceneSpec sample validation harness (no gameplay
 *                     actors touched); exit code = failure count
 *   -M1bGenerate    : validate the SceneSpec (-M1bSpec=<path>, default
 *                     Data/Scenes/valid/valid_workroom_001.json) and write a
 *                     deterministic build plan to Data/Reports/<scene_id>.plan.json.
 *                     Invalid spec -> NO plan written, exit code 2.
 *   -M1bE2E         : full playthrough of the GENERATED level (run with the
 *                     map argument /Game/Generated/<scene_id>): per-tick
 *                     steering + injected W/E, no teleports, no direct calls.
 *                     Exit code = failure count.
 *   -M1cValidateSpec=<path> [-M1cErrorOut=<path>]
 *                     : validate ONE SceneSpec file with the M1a validator and
 *                     write "VALID" or the error list to ErrorOut (UTF-8).
 *                     Exit 0 = valid, 1 = invalid. Used by the M1c AI planner
 *                     feedback loop; no level or plan is produced here.
 *   -M1cCheckSpec=<path> [-M1cErrorOut=<path>]
 *                     : M2b combined gate in ONE launch - schema validation,
 *                     plan realization (FLevelPlanBuilder) and placement
 *                     checks against the SERIALIZED plan coordinates (single
 *                     source of truth, no Python mirror). Detects e.g. object
 *                     footprints blocking the player spawn point.
 *                     Exit 0 = valid, 1 = invalid.
 *
 * The M0 auto-move test lives on the old AWorldGenPlayerPawn, which is no
 * longer spawned by this mode but kept for the M0 baseline record.
 */
UCLASS()
class WORLDGENRUNTIME_API AWorldGenGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AWorldGenGameMode();

protected:
	virtual void BeginPlay() override;

private:
	// --- shared helpers ---
	APlayerController* GetPC() const;
	AWorldGenCharacter* GetPlayerChar() const;
	void InjectKey(const FKey& Key, EInputEvent Event, float AmountDepressed);
	void RecordState(int32 StepIdx, const FString& What, bool bOk);
	void RecordE2E(int32 StepIdx, const FString& What, bool bOk);
	void FinishAllTests();
	void E2EAbort(int32 StepIdx, const FString& What);

	// --- phase 1: state test ---
	void ScheduleStateStep(float Delay);
	void RunStateStep();
	void StartResetForE2E();

	// --- phase 2: end-to-end playthrough ---
	void StartE2E();
	void RunE2ETick();

	// --- M1b: plan generation + generated-level playthrough ---
	void RunM1bGenerate();
	void RunM1cValidate();
	void RunM1cCheck();
	void StartM1bE2E();
	void RunM1bE2ETick();
	void M1bSteerTowards(const FVector& Target);
	void M1bUpdateStall(const FVector& Pos, const FVector& Target);
	void RecordM1b(int32 StepIdx, const FString& What, bool bOk);
	void M1bAbort(int32 StepIdx, const FString& What);
	void FinishM1b();

	// M2bs demo package: optional PNG frame capture of the REAL E2E playthrough
	// (-DemoCapture=<dir>, together with -M1bE2E). Visual-only: screenshots are
	// requested while the validated E2E machinery runs untouched; no input,
	// state, timing or assertion is modified. Empty dir = capture disabled.
	virtual void Tick(float DeltaSeconds) override;
	FString DemoCaptureDir;
	int32 DemoFrameCounter = 0;
	int32 DemoFrameIdx = 0;

	FTimerHandle AutoQuitTimerHandle;
	FTimerHandle TestTimerHandle;

	TWeakObjectPtr<AWorldGenDoor> TestDoor;
	TWeakObjectPtr<AWorldGenPickup> TestPickup;
	TWeakObjectPtr<AWorldGenExitTrigger> TestExit;

	// M1b generated-level E2E
	enum class EM1bState : uint8
	{
		Inactive, SeekKey, InteractKey, SeekDoor, InteractDoor, ProbeWallIndoor, SeekExit, Done
	};
	EM1bState M1bState = EM1bState::Inactive;
	int32 M1bTicks = 0;
	int32 M1bChecks = 0;
	int32 M1bFailCount = 0;
	TWeakObjectPtr<AWorldGenDoor> M1bDoor;
	TWeakObjectPtr<AWorldGenPickup> M1bPickup;
	TWeakObjectPtr<AWorldGenExitTrigger> M1bExit;
	FVector M1bKeyLoc = FVector::ZeroVector;
	FVector M1bDoorLoc = FVector::ZeroVector;   // doorway-center approach target
	FVector M1bExitLoc = FVector::ZeroVector;
	FVector M1bHingeLoc = FVector::ZeroVector;  // door actor loc = hinge on the wall plane
	FVector M1bOutward = FVector::ZeroVector;   // outward normal through the exit doorway
	FVector M1bProbeLoc = FVector::ZeroVector;  // indoor-side counterexample point

	// M1c-A: matches LevelPlanBuilder's wall thickness T (single source: plan
	// JSON wall_thickness_cm); used to compute the wall's outer face in E2E.
	static constexpr float M1cWallThicknessCm = 20.0f;

	// M1c stuck-detection / detour steering (straight-line steering deadlocks
	// on obstacle corners - observed on ai_gallery_777 desk west face).
	FVector M1bDetourPoint = FVector::ZeroVector; // active bypass waypoint
	FVector M1bStallTarget = FVector::ZeroVector; // target the stall state belongs to
	FVector M1bStallRefPos = FVector::ZeroVector; // last position with real progress
	int32 M1bStallTicks = 0;
	int32 M1bStallSide = 0;                       // alternate detour sides

	// state-test phase
	int32 StateStep = 0;
	int32 StateChecks = 0;
	int32 StateFailCount = 0;
	FVector M05InputTestStartLocation = FVector::ZeroVector;
	bool bActorsValid = false;

	// e2e phase
	enum class EE2EState : uint8
	{
		Inactive, SeekFirstDoor, ProbeInteractLocked, PushClosedDoor, CheckClosedBlock,
		ReturnToKey, InteractKey, ReturnToDoor, InteractDoorOpen, WalkToExit, WaitMission, Done
	};
	EE2EState E2EState = EE2EState::Inactive;
	int32 E2ETicks = 0;
	int32 E2EChecks = 0;
	int32 E2EFailCount = 0;
	FVector DoorLoc = FVector::ZeroVector;
	FVector KeyLoc = FVector::ZeroVector;
	FVector ExitLoc = FVector::ZeroVector;
};
