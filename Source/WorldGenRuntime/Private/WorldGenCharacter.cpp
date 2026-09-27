#include "WorldGenCharacter.h"
#include "WorldGenDoor.h"
#include "WorldGenPickup.h"
#include "WorldGenGameState.h"
#include "WorldGenRuntime.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"

AWorldGenCharacter::AWorldGenCharacter()
{
	PrimaryActorTick.bCanEverTick = false;

	bUseControllerRotationYaw = true;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(GetCapsuleComponent());
	Camera->SetRelativeLocation(FVector(0.0f, 0.0f, 60.0f));
	Camera->bUsePawnControlRotation = true;
}

void AWorldGenCharacter::SetNearbyPickup(AWorldGenPickup* Pickup)
{
	NearbyPickup = Pickup;
}

void AWorldGenCharacter::ClearNearbyPickup(AWorldGenPickup* Pickup)
{
	if (NearbyPickup.Get() == Pickup)
	{
		NearbyPickup = nullptr;
	}
}

void AWorldGenCharacter::SetNearbyDoor(AWorldGenDoor* Door)
{
	NearbyDoor = Door;
}

void AWorldGenCharacter::ClearNearbyDoor(AWorldGenDoor* Door)
{
	if (NearbyDoor.Get() == Door)
	{
		NearbyDoor = nullptr;
	}
}

void AWorldGenCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	PlayerInputComponent->BindAxis("MoveForward", this, &AWorldGenCharacter::MoveForward);
	PlayerInputComponent->BindAxis("MoveRight", this, &AWorldGenCharacter::MoveRight);
	PlayerInputComponent->BindAxis("Turn", this, &AWorldGenCharacter::Turn);
	PlayerInputComponent->BindAxis("LookUp", this, &AWorldGenCharacter::LookUp);
	PlayerInputComponent->BindAction("Interact", IE_Pressed, this, &AWorldGenCharacter::OnInteract);

	UE_LOG(LogWorldGenRT, Display, TEXT("WorldGenCharacter: axes bound + Interact action bound (E)"));
}

void AWorldGenCharacter::MoveForward(float Value)
{
	if (Value != 0.0f)
	{
		AddMovementInput(GetActorForwardVector(), Value);
	}
}

void AWorldGenCharacter::MoveRight(float Value)
{
	if (Value != 0.0f)
	{
		AddMovementInput(GetActorRightVector(), Value);
	}
}

void AWorldGenCharacter::Turn(float Value)
{
	AddControllerYawInput(Value);
}

void AWorldGenCharacter::LookUp(float Value)
{
	AddControllerPitchInput(Value);
}

void AWorldGenCharacter::OnInteract()
{
	// Skip pickups that were already taken (their zone may still linger one frame).
	if (AWorldGenPickup* Pickup = NearbyPickup.Get())
	{
		if (!Pickup->IsPickedUp())
		{
			Pickup->PickUp();
			return;
		}
		ClearNearbyPickup(Pickup);
	}
	if (AWorldGenDoor* Door = NearbyDoor.Get())
	{
		Door->Interact();
		return;
	}
	AWorldGenGameState::BroadcastMessage(TEXT("Nothing to interact with nearby"), FColor::White);
}
