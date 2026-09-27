#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WorldGenPickup.generated.h"

class USphereComponent;
class UStaticMeshComponent;

/**
 * M0.5 key pickup: floats and spins, overlap announces availability, PickUp()
 * is the real gameplay entry (player E key and automated test both call it).
 * Sets the key flag on the game state and destroys itself.
 */
UCLASS()
class WORLDGENRUNTIME_API AWorldGenPickup : public AActor
{
	GENERATED_BODY()

public:
	AWorldGenPickup();

	/** Real gameplay entry: grants the key and removes the pickup from play. */
	void PickUp();

	/** Automation helper: restore the pickup between test phases. */
	void ResetForRetest();

	bool IsPickedUp() const { return bPickedUp; }

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	UPROPERTY(VisibleAnywhere, Category = "WorldGen")
	USphereComponent* InteractZone;

	UPROPERTY(VisibleAnywhere, Category = "WorldGen")
	UStaticMeshComponent* KeyMesh;

private:
	bool bPickedUp = false;

	UFUNCTION()
	void OnInteractZoneBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnInteractZoneEndOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);
};
