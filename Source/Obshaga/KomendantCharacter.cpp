#include "KomendantCharacter.h"

#include "KomendantAIController.h"
#include "KomendantPersonality.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"

AKomendantCharacter::AKomendantCharacter()
{
	PrimaryActorTick.bCanEverTick = false;

	// Тот же размер, что у жильцов: он должен проходить в те же двери.
	GetCapsuleComponent()->InitCapsuleSize(35.f, 90.f);

	AIControllerClass = AKomendantAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->bOrientRotationToMovement = true;
	Movement->RotationRate = FRotator(0.f, 400.f, 0.f);
}

void AKomendantCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AKomendantCharacter, Alert);
}

const UKomendantPersonality* AKomendantCharacter::PickPersonality() const
{
	TArray<const UKomendantPersonality*> Valid;
	for (const UKomendantPersonality* Personality : Personalities)
	{
		if (Personality)
		{
			Valid.Add(Personality);
		}
	}
	return Valid.IsEmpty() ? GetDefault<UKomendantPersonality>() : Valid[FMath::RandRange(0, Valid.Num() - 1)];
}

void AKomendantCharacter::SetAlert(EKomendantAlert NewAlert)
{
	if (HasAuthority())
	{
		Alert = NewAlert;
	}
}
