#pragma once

#include "CoreMinimal.h"
#include "GameFramework/DefaultPawn.h"
#include "WorldGenPlayerPawn.generated.h"

/**
 * M0 baseline player pawn.
 *
 * Deliberately binds legacy axis input (MoveForward/MoveRight/Turn/LookUp) so the
 * project does not depend on engine-default Enhanced Input mapping contexts.
 * Axis -> key assignments live in Config/DefaultInput.ini.
 *
 * -M0AutoMoveTest: automated movement proof. 1 s after BeginPlay the pawn starts
 * applying forward movement input every tick; 1 s later it stops and logs the
 * travelled distance with an explicit PASS/FAIL marker.
 */
UCLASS()
class WORLDGENRUNTIME_API AWorldGenPlayerPawn : public ADefaultPawn
{
	GENERATED_BODY()

public:
	AWorldGenPlayerPawn();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

private:
	void MoveForward(float Value);
	void MoveRight(float Value);
	void Turn(float Value);
	void LookUp(float Value);

	void M0AutoMoveStart();
	void M0AutoMoveStop();

	FTimerHandle M0AutoMoveStartHandle;
	FTimerHandle M0AutoMoveStopHandle;

	bool bM0AutoMoveActive = false;
	FVector M0AutoMoveStartLocation;
};
