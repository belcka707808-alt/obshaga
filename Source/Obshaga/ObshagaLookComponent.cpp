#include "ObshagaLookComponent.h"

#include "CarryComponent.h"
#include "KomendantCharacter.h"
#include "Obshaga.h"
#include "ObshagaCharacter.h"
#include "Animation/AnimSequenceBase.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	constexpr float SpeedSmoothing = 6.f;
	// Сколько разовый клип (взял, жест) ждёт, пока персонаж остановится.
	constexpr float OneShotWaitSeconds = 1.f;
}

const UObshagaLookConfig* UObshagaLookConfig::Get()
{
	// Сильная ссылка и одна попытка загрузки — по тем же причинам, что у UObshagaAudioConfig.
	static const TCHAR* AssetPath = TEXT("/Game/Obshaga/Characters/DA_Looks.DA_Looks");
	static TStrongObjectPtr<const UObshagaLookConfig> Cached;
	static bool bTried = false;
	if (!bTried)
	{
		bTried = true;
		Cached.Reset(LoadObject<UObshagaLookConfig>(nullptr, AssetPath, nullptr, LOAD_NoWarn));
	}
	return Cached.IsValid() ? Cached.Get() : GetDefault<UObshagaLookConfig>();
}

UObshagaLookComponent::UObshagaLookComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UObshagaLookComponent::BeginPlay()
{
	Super::BeginPlay();

	// Комендант один, ему номер модели не нужен; жильцу модель назначит персонаж, когда узнает свой номер.
	if (Cast<AKomendantCharacter>(GetOwner()))
	{
		ApplyLook(0);
	}
}

void UObshagaLookComponent::ApplyLook(int32 Index)
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	const UObshagaLookConfig* Config = UObshagaLookConfig::Get();
	const bool bKomendant = Cast<AKomendantCharacter>(GetOwner()) != nullptr;
	USkeletalMesh* Model = bKomendant ? Config->KomendantMesh.Get()
		: (Config->ResidentMeshes.IsEmpty() ? nullptr : Config->ResidentMeshes[FMath::Abs(Index) % Config->ResidentMeshes.Num()].Get());
	if (!Character || !Model)
	{
		return;
	}

	// Модель ставится ногами на низ капсулы и растягивается до нужного роста.
	USkeletalMeshComponent* Mesh = Character->GetMesh();
	const FBoxSphereBounds Bounds = Model->GetBounds();
	const float ModelHeight = FMath::Max(Bounds.BoxExtent.Z * 2.f, 1.f);
	const float Scale = (bKomendant ? Config->KomendantHeight : Config->ResidentHeight) / ModelHeight;
	const float FeetZ = (Bounds.Origin.Z - Bounds.BoxExtent.Z) * Scale;

	Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Mesh->SetSkeletalMesh(Model);
	Mesh->SetRelativeScale3D(FVector(Scale));
	Mesh->SetRelativeRotation(FRotator(0.f, Config->ModelYaw, 0.f));
	// Высота меша для стоящего персонажа; в приседе капсула ниже, и меш поднимается на ту же разницу.
	StandingMeshZ = -Character->GetDefaultHalfHeight() - FeetZ;
	const float CrouchAdjust = Character->IsCrouched() ? Character->GetDefaultHalfHeight() - Character->GetCharacterMovement()->GetCrouchedHalfHeight() : 0.f;
	Mesh->SetRelativeLocation(FVector(0.f, 0.f, StandingMeshZ + CrouchAdjust));
	// Движок запомнил положение манекена и возвращал бы меш в него при сглаживании сети — запоминаем новое.
	Character->CacheInitialMeshOffset(Mesh->GetRelativeLocation(), Mesh->GetRelativeRotation());

	bLookApplied = true;
	Current = nullptr;
}

void UObshagaLookComponent::PlayOnce(UAnimSequenceBase* Animation)
{
	if (Animation)
	{
		// Клип начнётся, как только персонаж остановится; ждём этого не дольше OneShotWaitSeconds.
		OneShot = Animation;
		bOneShotStarted = false;
		OneShotUntil = GetWorld()->GetTimeSeconds() + OneShotWaitSeconds;
	}
}

UAnimSequenceBase* UObshagaLookComponent::PickAnimation(bool& bOutLoop)
{
	const ACharacter* Character = CastChecked<ACharacter>(GetOwner());
	const AObshagaCharacter* Resident = Cast<AObshagaCharacter>(Character);
	const UObshagaLookConfig* Config = UObshagaLookConfig::Get();
	// Скорость сглажена: комендант идёт рывками от точки к точке, и без этого шаг мигал бы со стойкой.
	const float Speed = SmoothedSpeed;
	bOutLoop = true;

	if (Resident)
	{
		// Разовые анимации: взял или бросил предмет, сказал фразу.
		const bool bCarrying = Resident->GetCarryComponent()->IsCarrying();
		if (bCarrying != bWasCarrying)
		{
			PlayOnce(bCarrying ? Config->PickUp : Config->Throw);
			bWasCarrying = bCarrying;
		}
		const int32 Emote = Resident->GetActiveEmote();
		if (Emote != LastEmote)
		{
			if (Emote != INDEX_NONE && !Config->EmoteGestures.IsEmpty())
			{
				PlayOnce(Config->EmoteGestures[Emote % Config->EmoteGestures.Num()]);
			}
			LastEmote = Emote;
		}

		if (Resident->IsFrozen() && Config->Caught)
		{
			return Config->Caught;
		}
		// Жест на ходу не играем: ноги должны идти. Смотрим на настоящую скорость, а не сглаженную,
		// иначе клип опаздывал бы на полсекунды после остановки и обрезался.
		if (OneShot && GetWorld()->GetTimeSeconds() < OneShotUntil && Character->GetVelocity().Size2D() < Config->MoveSpeedThreshold)
		{
			if (!bOneShotStarted)
			{
				bOneShotStarted = true;
				OneShotUntil = GetWorld()->GetTimeSeconds() + OneShot->GetPlayLength();
			}
			bOutLoop = false;
			return OneShot;
		}
	}

	if (Character->GetCharacterMovement()->IsFalling() && Config->Fall)
	{
		return Config->Fall;
	}
	if (Character->IsCrouched() && Config->Crouch)
	{
		return Config->Crouch;
	}
	if (Resident && Resident->GetCarryComponent()->IsCarrying() && Config->Carry)
	{
		return Config->Carry;
	}
	if (Speed > Config->RunSpeedThreshold && Config->Sprint)
	{
		return Config->Sprint;
	}
	if (Speed > Config->MoveSpeedThreshold && Config->Walk)
	{
		return Config->Walk;
	}
	return Config->Idle;
}

void UObshagaLookComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// На выделенном сервере смотреть некому.
	if (!bLookApplied || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	SmoothedSpeed = FMath::FInterpTo(SmoothedSpeed, GetOwner()->GetVelocity().Size2D(), DeltaTime, SpeedSmoothing);

	bool bLoop = true;
	UAnimSequenceBase* Wanted = PickAnimation(bLoop);
	if (Wanted && Wanted != Current)
	{
		Current = Wanted;
		CastChecked<ACharacter>(GetOwner())->GetMesh()->PlayAnimation(Wanted, bLoop);
		UE_LOG(LogObshaga, Verbose, TEXT("[%s] %s anim: %s"), *GetNameSafe(GetWorld()), *GetOwner()->GetName(), *Wanted->GetName());
	}
}

FString UObshagaLookComponent::GetCurrentAnimName() const
{
	return GetNameSafe(Current);
}
