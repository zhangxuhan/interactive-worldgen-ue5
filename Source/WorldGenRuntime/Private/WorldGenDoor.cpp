#include "WorldGenDoor.h"
#include "WorldGenCharacter.h"
#include "WorldGenGameState.h"
#include "WorldGenRuntime.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

AWorldGenDoor::AWorldGenDoor()
{
	PrimaryActorTick.bCanEverTick = false;

	// Pivot = hinge. Neutral root AT the hinge; the panel offset lives on a
	// CHILD component. SpawnActor overwrites the ROOT component's relative
	// transform with the actor spawn transform, so the original design (panel
	// offset on the root DoorMesh) was silently discarded: the panel centered
	// itself on the hinge - half of it inside the wall, half of the doorway
	// open, bottom sunk into the floor (found via the M3a preview render).
	// Child relative transforms survive spawn (same mechanism as the pickup's
	// floating key mesh, which always rendered correctly).
	DoorRoot = CreateDefaultSubobject<USceneComponent>(TEXT("DoorRoot"));
	RootComponent = DoorRoot;

	// Panel (120 x 10 x 220 cm) offset so the hinge edge sits at the actor origin.
	DoorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DoorMesh"));
	DoorMesh->SetupAttachment(RootComponent);
	DoorMesh->SetRelativeLocation(FVector(0.0f, 60.0f, 110.0f));
	DoorMesh->SetRelativeScale3D(FVector(0.1f, 1.2f, 2.2f));

	ConstructorHelpers::FObjectFinder<UStaticMesh> CubeVisual(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeVisual.Succeeded())
	{
		DoorMesh->SetStaticMesh(CubeVisual.Object);
	}

	InteractZone = CreateDefaultSubobject<USphereComponent>(TEXT("InteractZone"));
	InteractZone->SetupAttachment(RootComponent);
	InteractZone->SetSphereRadius(180.0f);
	// The root mesh is non-uniformly scaled (0.1, 1.2, 2.2). Without this the
	// trigger inherits the 0.1 X scale and shrinks to 18 cm, where the player
	// can never reach it (E2E test caught exactly that).
	InteractZone->SetWorldScale3D(FVector::OneVector);
	InteractZone->SetCollisionProfileName(TEXT("Trigger"));

	InteractZone->OnComponentBeginOverlap.AddDynamic(this, &AWorldGenDoor::OnInteractZoneBeginOverlap);
	InteractZone->OnComponentEndOverlap.AddDynamic(this, &AWorldGenDoor::OnInteractZoneEndOverlap);
}

void AWorldGenDoor::BeginPlay()
{
	Super::BeginPlay();
	InitialYaw = GetActorRotation().Yaw;
	// The root mesh is non-uniformly scaled (0.1, 1.2, 2.2); without this the
	// trigger inherits the 0.1 X scale and shrinks to 18 cm, where the player
	// can never reach it (E2E test caught exactly that). Must run in BeginPlay:
	// SetWorldScale3D is a no-op during the constructor.
	InteractZone->SetWorldScale3D(FVector::OneVector);
	UE_LOG(LogWorldGenRT, Display, TEXT("WorldGenDoor ready at %s (closed), interact zone world radius %.1f"),
		*GetActorLocation().ToCompactString(), InteractZone->GetScaledSphereRadius());
}

void AWorldGenDoor::Interact()
{
	AWorldGenGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWorldGenGameState>() : nullptr;

	if (bIsOpen)
	{
		AWorldGenGameState::BroadcastMessage(TEXT("Door is already open"), FColor::White);
		return;
	}

	if (!GS || !GS->HasKey())
	{
		AWorldGenGameState::BroadcastMessage(TEXT("DOOR LOCKED - find the key first"), FColor::Red);
		UE_LOG(LogWorldGenRT, Display, TEXT("[M05] door open rejected: key=%s"),
			GS ? (GS->HasKey() ? TEXT("true") : TEXT("false")) : TEXT("no-gamestate"));
		return;
	}

	bIsOpen = true;
	SetActorRotation(FRotator(0.0f, InitialYaw + 90.0f, 0.0f));
	AWorldGenGameState::BroadcastMessage(TEXT("DOOR OPENED"), FColor::Green);
	UE_LOG(LogWorldGenRT, Display, TEXT("[M05] door opened with key"));
	// Hinge evidence: the panel origin (hinge) stays put, the panel sweeps aside.
	UE_LOG(LogWorldGenRT, Display, TEXT("[M05] door hinge at %s, panel now at %s rot=%s"),
		*GetActorLocation().ToCompactString(),
		*DoorMesh->GetComponentLocation().ToCompactString(),
		*DoorMesh->GetComponentRotation().ToCompactString());
}

void AWorldGenDoor::SetClosed()
{
	bIsOpen = false;
	SetActorRotation(FRotator(0.0f, InitialYaw, 0.0f));
}

void AWorldGenDoor::OnInteractZoneBeginOverlap(UPrimitiveComponent*, AActor* OtherActor,
	UPrimitiveComponent*, int32, bool, const FHitResult&)
{
	if (AWorldGenCharacter* Player = Cast<AWorldGenCharacter>(OtherActor))
	{
		Player->SetNearbyDoor(this);
		AWorldGenGameState::BroadcastMessage(TEXT("Press E to use the door"), FColor::Cyan);
		UE_LOG(LogWorldGenRT, Display, TEXT("[M05] door interact zone ENTER by %s at %s (zone world pos %s)"),
			*OtherActor->GetName(), *Player->GetActorLocation().ToCompactString(),
			*InteractZone->GetComponentLocation().ToCompactString());
	}
}

void AWorldGenDoor::OnInteractZoneEndOverlap(UPrimitiveComponent*, AActor* OtherActor,
	UPrimitiveComponent*, int32)
{
	if (AWorldGenCharacter* Player = Cast<AWorldGenCharacter>(OtherActor))
	{
		Player->ClearNearbyDoor(this);
		UE_LOG(LogWorldGenRT, Display, TEXT("[M05] door interact zone EXIT by %s at %s"),
			*OtherActor->GetName(), *Player->GetActorLocation().ToCompactString());
	}
}
