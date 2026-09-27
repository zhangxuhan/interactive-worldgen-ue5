#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WorldGenExitTrigger.generated.h"

class UBoxComponent;

/**
 * M0.5 exit zone: box trigger outside the room, beyond the door.
 * Entering with the key completes the mission; entering without it is only
 * possible through level bugs, and reports a locked exit instead.
 */
UCLASS()
class WORLDGENRUNTIME_API AWorldGenExitTrigger : public AActor
{
	GENERATED_BODY()

public:
	AWorldGenExitTrigger();

protected:
	UPROPERTY(VisibleAnywhere, Category = "WorldGen")
	UBoxComponent* ExitZone;

private:
	UFUNCTION()
	void OnExitZoneBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);
};
