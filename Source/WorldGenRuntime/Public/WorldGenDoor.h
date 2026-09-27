#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WorldGenDoor.generated.h"

class UBoxComponent;
class USceneComponent;
class UStaticMeshComponent;
class USphereComponent;

/**
 * M0.5 door: closed by default, blocks the doorway with real collision.
 * Interact() is the single gameplay entry (player E key and automated test both
 * call it). Opens only when the game state says the key was collected.
 * The actor pivot sits at the door hinge so opening rotates the panel away
 * from the doorway.
 */
UCLASS()
class WORLDGENRUNTIME_API AWorldGenDoor : public AActor
{
	GENERATED_BODY()

public:
	AWorldGenDoor();

	/** Real gameplay entry: checks key state, opens or reports locked. */
	void Interact();

	bool IsOpen() const { return bIsOpen; }

	/** Automation helper: restore the closed pose (used between test phases). */
	void SetClosed();

	/** Automation/diagnostics access to the interaction trigger. */
	const USphereComponent* GetInteractZone() const { return InteractZone; }

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "WorldGen")
	USceneComponent* DoorRoot;

	UPROPERTY(VisibleAnywhere, Category = "WorldGen")
	UStaticMeshComponent* DoorMesh;

	UPROPERTY(VisibleAnywhere, Category = "WorldGen")
	USphereComponent* InteractZone;

	UPROPERTY(VisibleAnywhere, Category = "WorldGen")
	bool bIsOpen = false;

private:
	float InitialYaw = 0.0f;

	UFUNCTION()
	void OnInteractZoneBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnInteractZoneEndOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);
};
