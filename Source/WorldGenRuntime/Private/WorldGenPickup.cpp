#include "WorldGenPickup.h"
#include "WorldGenCharacter.h"
#include "WorldGenGameState.h"
#include "WorldGenRuntime.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "EngineUtils.h"

AWorldGenPickup::AWorldGenPickup()
{
	PrimaryActorTick.bCanEverTick = true;

	InteractZone = CreateDefaultSubobject<USphereComponent>(TEXT("InteractZone"));
	RootComponent = InteractZone;
	InteractZone->SetSphereRadius(120.0f);
	InteractZone->SetCollisionProfileName(TEXT("Trigger"));

	KeyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("KeyMesh"));
	KeyMesh->SetupAttachment(RootComponent);
	KeyMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 50.0f));
	KeyMesh->SetRelativeScale3D(FVector(0.3f, 0.3f, 0.3f));
	// Visual only: the floating key model must not block the walking player
	// (default static mesh collision stopped the capsule dead in the room).
	KeyMesh->SetCollisionProfileName(TEXT("NoCollision"));

	// M3a: use the procedural key prop (Tools/m3a_make_key.py -> imported as
	// /Game/Generated/Assets/SM_Key_Prop) so the quest item reads as a KEY in
	// previews instead of a white sphere. Sphere stays as a fallback so the
	// gameplay keeps working even if the asset is missing.
	ConstructorHelpers::FObjectFinder<UStaticMesh> KeyVisual(
		TEXT("/Game/Generated/Assets/SM_Key_Prop.SM_Key_Prop"));
	if (KeyVisual.Succeeded())
	{
		KeyMesh->SetStaticMesh(KeyVisual.Object);
		// obj is authored at final size (~26 cm long), pivot at ring centre
		KeyMesh->SetRelativeScale3D(FVector(1.35f, 1.35f, 1.35f));
		KeyMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 55.0f));
	}
	else
	{
		ConstructorHelpers::FObjectFinder<UStaticMesh> SphereVisual(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		if (SphereVisual.Succeeded())
		{
			KeyMesh->SetStaticMesh(SphereVisual.Object);
		}
	}

	InteractZone->OnComponentBeginOverlap.AddDynamic(this, &AWorldGenPickup::OnInteractZoneBeginOverlap);
	InteractZone->OnComponentEndOverlap.AddDynamic(this, &AWorldGenPickup::OnInteractZoneEndOverlap);
}

void AWorldGenPickup::BeginPlay()
{
	Super::BeginPlay();
	UE_LOG(LogWorldGenRT, Display, TEXT("WorldGenPickup ready at %s"), *GetActorLocation().ToCompactString());
}

void AWorldGenPickup::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	KeyMesh->AddLocalRotation(FRotator(0.0f, DeltaTime * 90.0f, 0.0f));

	// M2b multi-scene robustness: the interaction state relies on the
	// edge-triggered BeginOverlap. A character that spawns already inside the
	// zone (seen in 2 of 5 real AI levels: desk under the spawn point) never
	// produces an edge, so NearbyPickup stays null and E does nothing. Poll
	// the overlap state every tick as a safety net (idempotent), with a
	// geometric fallback in case the overlap registration was missed too.
	if (!bPickedUp)
	{
		TArray<AActor*> Overlappers;
		InteractZone->GetOverlappingActors(Overlappers, AWorldGenCharacter::StaticClass());
		for (AActor* O : Overlappers)
		{
			if (AWorldGenCharacter* P = Cast<AWorldGenCharacter>(O))
			{
				P->SetNearbyPickup(this);
			}
		}
		if (Overlappers.Num() == 0)
		{
			constexpr float ZoneRadius = 120.0f;
			constexpr float CapsuleReach = 90.0f; // vertical capsule extent
			for (TActorIterator<AWorldGenCharacter> It(GetWorld()); It; ++It)
			{
				const FVector D = It->GetActorLocation() - GetActorLocation();
				const float Dist2D = FVector::Dist2D(D, FVector::ZeroVector);
				if (Dist2D < ZoneRadius + 35.0f && FMath::Abs(D.Z) < ZoneRadius + CapsuleReach)
				{
					It->SetNearbyPickup(this);
				}
			}
		}
	}
}

void AWorldGenPickup::PickUp()
{
	if (bPickedUp)
	{
		return;
	}
	AWorldGenGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWorldGenGameState>() : nullptr;
	if (GS)
	{
		GS->SetHasKey(true);
	}
	else
	{
		UE_LOG(LogWorldGenRT, Warning, TEXT("[M05] pickup: no game state, key flag NOT set"));
	}
	UE_LOG(LogWorldGenRT, Display, TEXT("[M05] key picked up at %s"), *GetActorLocation().ToCompactString());
	// Clear any registered "nearby pickup" references BEFORE disabling collision:
	// a disabled component never fires EndOverlap, so the character would keep
	// routing E presses to this dead pickup instead of the door.
	TArray<AActor*> Overlappers;
	InteractZone->GetOverlappingActors(Overlappers, AWorldGenCharacter::StaticClass());
	for (AActor* Overlapper : Overlappers)
	{
		if (AWorldGenCharacter* Player = Cast<AWorldGenCharacter>(Overlapper))
		{
			Player->ClearNearbyPickup(this);
		}
	}
	// Removed from play: hidden, non-ticking, interaction disabled (restorable
	// via ResetForRetest for the automation chains).
	bPickedUp = true;
	KeyMesh->SetVisibility(false);
	InteractZone->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetActorTickEnabled(false);
}

void AWorldGenPickup::ResetForRetest()
{
	bPickedUp = false;
	KeyMesh->SetVisibility(true);
	InteractZone->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SetActorTickEnabled(true);
}

void AWorldGenPickup::OnInteractZoneBeginOverlap(UPrimitiveComponent*, AActor* OtherActor,
	UPrimitiveComponent*, int32, bool, const FHitResult&)
{
	if (AWorldGenCharacter* Player = Cast<AWorldGenCharacter>(OtherActor))
	{
		Player->SetNearbyPickup(this);
		AWorldGenGameState::BroadcastMessage(TEXT("Press E to pick up the key"), FColor::Yellow);
	}
}

void AWorldGenPickup::OnInteractZoneEndOverlap(UPrimitiveComponent*, AActor* OtherActor,
	UPrimitiveComponent*, int32)
{
	if (AWorldGenCharacter* Player = Cast<AWorldGenCharacter>(OtherActor))
	{
		Player->ClearNearbyPickup(this);
	}
}
