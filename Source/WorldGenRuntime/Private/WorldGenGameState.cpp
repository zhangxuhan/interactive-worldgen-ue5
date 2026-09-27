#include "WorldGenGameState.h"
#include "WorldGenRuntime.h"
#include "Engine/Engine.h"

void AWorldGenGameState::SetHasKey(bool bNewHasKey)
{
	if (bHasKey == bNewHasKey)
	{
		return;
	}
	bHasKey = bNewHasKey;
	BroadcastMessage(bNewHasKey
		? TEXT("KEY ACQUIRED - you can open the door now")
		: TEXT("KEY LOST"), FColor::Yellow);
}

void AWorldGenGameState::SetMissionComplete()
{
	if (bMissionComplete)
	{
		return;
	}
	bMissionComplete = true;
	BroadcastMessage(TEXT("MISSION COMPLETE - key collected, door opened, exit reached"), FColor::Green);
}

void AWorldGenGameState::ClearMissionComplete()
{
	bMissionComplete = false;
}

void AWorldGenGameState::BroadcastMessage(const FString& Msg, const FColor& Color)
{
	UE_LOG(LogWorldGenRT, Display, TEXT("[M05] %s"), *Msg);
	if (GEngine)
	{
		static int32 MessageKey = 0;
		GEngine->AddOnScreenDebugMessage(MessageKey++, 6.0f, Color, Msg);
	}
}
