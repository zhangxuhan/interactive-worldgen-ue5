#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "WorldGenGameState.generated.h"

/**
 * M0.5 mission state: key possession + completion flag.
 * Also hosts the shared on-screen + log feedback helper used by all gameplay actors.
 */
UCLASS()
class WORLDGENRUNTIME_API AWorldGenGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	bool HasKey() const { return bHasKey; }
	bool IsMissionComplete() const { return bMissionComplete; }

	void SetHasKey(bool bNewHasKey);
	void SetMissionComplete();
	/** Automation helper: clear the completion flag between test phases. */
	void ClearMissionComplete();

	/** Shared feedback: UE_LOG always, on-screen message when a viewport exists. */
	static void BroadcastMessage(const FString& Msg, const FColor& Color);

protected:
	UPROPERTY(VisibleAnywhere, Category = "WorldGen")
	bool bHasKey = false;

	UPROPERTY(VisibleAnywhere, Category = "WorldGen")
	bool bMissionComplete = false;
};
