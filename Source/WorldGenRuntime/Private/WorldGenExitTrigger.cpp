#include "WorldGenExitTrigger.h"
#include "WorldGenCharacter.h"
#include "WorldGenGameState.h"
#include "WorldGenRuntime.h"
#include "Components/BoxComponent.h"

AWorldGenExitTrigger::AWorldGenExitTrigger()
{
	PrimaryActorTick.bCanEverTick = false;

	ExitZone = CreateDefaultSubobject<UBoxComponent>(TEXT("ExitZone"));
	RootComponent = ExitZone;
	ExitZone->SetBoxExtent(FVector(200.0f, 150.0f, 150.0f)); // 400 x 300 x 300 cm
	ExitZone->SetCollisionProfileName(TEXT("Trigger"));
	ExitZone->SetRelativeLocation(FVector(0.0f, 0.0f, 150.0f));

	ExitZone->OnComponentBeginOverlap.AddDynamic(this, &AWorldGenExitTrigger::OnExitZoneBeginOverlap);
}

void AWorldGenExitTrigger::OnExitZoneBeginOverlap(UPrimitiveComponent*, AActor* OtherActor,
	UPrimitiveComponent*, int32, bool, const FHitResult&)
{
	AWorldGenCharacter* Player = Cast<AWorldGenCharacter>(OtherActor);
	if (!Player)
	{
		return;
	}

	AWorldGenGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWorldGenGameState>() : nullptr;
	if (GS && GS->HasKey())
	{
		GS->SetMissionComplete();
		UE_LOG(LogWorldGenRT, Display, TEXT("[M05] exit reached with key at %s"), *Player->GetActorLocation().ToCompactString());
	}
	else
	{
		AWorldGenGameState::BroadcastMessage(TEXT("EXIT LOCKED - you should not be here without the key"), FColor::Red);
		UE_LOG(LogWorldGenRT, Warning, TEXT("[M05] exit overlap without key at %s"),
			*Player->GetActorLocation().ToCompactString());
	}
}
