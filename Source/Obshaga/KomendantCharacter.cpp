#include "KomendantCharacter.h"

#include "ObshagaStory.h"

#include "KomendantAIController.h"
#include "KomendantPersonality.h"
#include "Obshaga.h"
#include "ObshagaGameMode.h"

namespace
{
	// Комендант не тараторит: между обычными репликами проходит хотя бы столько секунд.
	constexpr float SayCooldownSeconds = 5.f;
}
#include "ObshagaLookComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"

AKomendantCharacter::AKomendantCharacter()
{
	PrimaryActorTick.bCanEverTick = false;

	// Тот же размер, что у жильцов: он должен проходить в те же двери.
	GetCapsuleComponent()->InitCapsuleSize(35.f, 90.f);

	LookComponent = CreateDefaultSubobject<UObshagaLookComponent>(TEXT("LookComponent"));

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

const UKomendantPersonality* AKomendantCharacter::PickPersonality(int32 Style) const
{
	TArray<const UKomendantPersonality*> Valid;
	for (const UKomendantPersonality* Personality : Personalities)
	{
		if (Personality)
		{
			Valid.Add(Personality);
		}
	}
	const TArray<const UKomendantPersonality*> OfStyle = Valid.FilterByPredicate([Style](const UKomendantPersonality* Personality)
	{
		return ObshagaStory::StyleFromPersonalityName(Personality->GetName()) == Style;
	});
	if (Style >= 0 && !OfStyle.IsEmpty())
	{
		Valid = OfStyle;
	}
	return Valid.IsEmpty() ? GetDefault<UKomendantPersonality>() : Valid[FMath::RandRange(0, Valid.Num() - 1)];
}

void AKomendantCharacter::Say(EKomendantLine Situation, int32 Style, bool bForce)
{
	const float Now = GetWorld()->GetTimeSeconds();
	if (!HasAuthority() || (!bForce && Now - LastSayServerTime < SayCooldownSeconds))
	{
		return;
	}
	LastSayServerTime = Now;
	const uint8 Variant = static_cast<uint8>(FMath::RandRange(0, 200));
	// Для автопроверки: что Палыч сказал и когда (номер — EKomendantLine: 1 — «услышал», 4 — «ушёл» после погони и т.д.).
	const AObshagaGameMode* GameMode = GetWorld()->GetAuthGameMode<AObshagaGameMode>();
	UE_LOG(LogObshaga, Verbose, TEXT("Komendant says %d (style %d, forced %d): %s, t=%.1f"), static_cast<int32>(Situation), Style, bForce ? 1 : 0,
		*ObshagaStory::KomendantLine(Situation, Style, Variant).ToString(), GameMode ? GameMode->GetRoundSeconds() : Now);
	MulticastSay(static_cast<uint8>(Situation), static_cast<uint8>(Style), Variant);
}

void AKomendantCharacter::MulticastSay_Implementation(uint8 Situation, uint8 Style, uint8 Variant)
{
	if (Situation < static_cast<uint8>(EKomendantLine::Count))
	{
		SpokenLine = ObshagaStory::KomendantLine(static_cast<EKomendantLine>(Situation), Style, Variant);
		SpokenTime = GetWorld()->GetTimeSeconds();
	}
}

void AKomendantCharacter::SetAlert(EKomendantAlert NewAlert)
{
	if (HasAuthority())
	{
		Alert = NewAlert;
	}
}
