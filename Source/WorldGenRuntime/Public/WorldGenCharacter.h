#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "WorldGenCharacter.generated.h"

class AWorldGenDoor;
class AWorldGenPickup;
class UCameraComponent;

/**
 * M0.5 walking player: capsule collision + CharacterMovement (walking, gravity,
 * blocked by walls/door). WASD + mouse + E interaction, replacing the M0 floating
 * pawn as the game mode default pawn.
 */
UCLASS()
class WORLDGENRUNTIME_API AWorldGenCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AWorldGenCharacter();

	void SetNearbyPickup(AWorldGenPickup* Pickup);
	void ClearNearbyPickup(AWorldGenPickup* Pickup);
	void SetNearbyDoor(AWorldGenDoor* Door);
	void ClearNearbyDoor(AWorldGenDoor* Door);

protected:
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

private:
	void MoveForward(float Value);
	void MoveRight(float Value);
	void Turn(float Value);
	void LookUp(float Value);
	void OnInteract();

	UPROPERTY(VisibleAnywhere, Category = "WorldGen")
	UCameraComponent* Camera;

	TWeakObjectPtr<AWorldGenPickup> NearbyPickup;
	TWeakObjectPtr<AWorldGenDoor> NearbyDoor;
};
