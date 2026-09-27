#include "WorldGenPlayerPawn.h"
#include "WorldGenRuntime.h"
#include "Components/InputComponent.h"
#include "TimerManager.h"
#include "Misc/Parse.h"

AWorldGenPlayerPawn::AWorldGenPlayerPawn()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AWorldGenPlayerPawn::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogWorldGenRT, Display, TEXT("WorldGenPlayerPawn spawned at %s"),
		*GetActorLocation().ToCompactString());

	if (FParse::Param(FCommandLine::Get(), TEXT("M0AutoMoveTest")))
	{
		UE_LOG(LogWorldGenRT, Display, TEXT("M0AutoMoveTest: armed (start in 1s, measure at 2s)"));
		GetWorldTimerManager().SetTimer(M0AutoMoveStartHandle, this, &AWorldGenPlayerPawn::M0AutoMoveStart, 1.0f, false);
		GetWorldTimerManager().SetTimer(M0AutoMoveStopHandle, this, &AWorldGenPlayerPawn::M0AutoMoveStop, 2.0f, false);
	}
}

void AWorldGenPlayerPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	PlayerInputComponent->BindAxis("MoveForward", this, &AWorldGenPlayerPawn::MoveForward);
	PlayerInputComponent->BindAxis("MoveRight", this, &AWorldGenPlayerPawn::MoveRight);
	PlayerInputComponent->BindAxis("Turn", this, &AWorldGenPlayerPawn::Turn);
	PlayerInputComponent->BindAxis("LookUp", this, &AWorldGenPlayerPawn::LookUp);

	UE_LOG(LogWorldGenRT, Display, TEXT("WorldGenPlayerPawn: legacy axes bound (MoveForward/MoveRight/Turn/LookUp)"));
}

void AWorldGenPlayerPawn::MoveForward(float Value)
{
	if (Value != 0.0f)
	{
		AddMovementInput(GetActorForwardVector(), Value);
	}
}

void AWorldGenPlayerPawn::MoveRight(float Value)
{
	if (Value != 0.0f)
	{
		AddMovementInput(GetActorRightVector(), Value);
	}
}

void AWorldGenPlayerPawn::Turn(float Value)
{
	AddControllerYawInput(Value);
}

void AWorldGenPlayerPawn::LookUp(float Value)
{
	AddControllerPitchInput(Value);
}

void AWorldGenPlayerPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Real held-key input fires the axis event every frame; mirror that here so the
	// movement component sees continuous input (a slow timer leaves most frames
	// input-less and FloatingPawnMovement decelerates immediately).
	if (bM0AutoMoveActive)
	{
		AddMovementInput(GetActorForwardVector(), 1.0f);
	}
}

void AWorldGenPlayerPawn::M0AutoMoveStart()
{
	M0AutoMoveStartLocation = GetActorLocation();
	bM0AutoMoveActive = true;
	UE_LOG(LogWorldGenRT, Display, TEXT("M0AutoMove: start at %s"), *M0AutoMoveStartLocation.ToCompactString());
}

void AWorldGenPlayerPawn::M0AutoMoveStop()
{
	bM0AutoMoveActive = false;

	const FVector EndLocation = GetActorLocation();
	const float Travelled = FVector::Dist2D(EndLocation, M0AutoMoveStartLocation);
	UE_LOG(LogWorldGenRT, Display, TEXT("M0AutoMove: end at %s, travelled %.1f cm -> %s"),
		*EndLocation.ToCompactString(), Travelled, Travelled > 10.0f ? TEXT("PASS") : TEXT("FAIL"));
}
