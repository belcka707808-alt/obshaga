#include "ObshagaLookComponent.h"

#include "CarryComponent.h"
#include "KomendantCharacter.h"
#include "Obshaga.h"
#include "ObshagaAnimInstance.h"
#include "ObshagaCharacter.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceBase.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	// Сглаживание скорости для выбора клипа и темпа шагов. Комендант идёт рывками от точки к точке — ему нужно сильное,
	// иначе шаг мигал бы со стойкой. Жильцу — слабое: с сильным ноги ещё полсекунды шагали бы на месте после остановки.
	constexpr float KomendantSpeedSmoothing = 6.f;
	constexpr float ResidentSpeedSmoothing = 20.f;
	// Сколько разовый клип (взял, жест) ждёт, пока персонаж остановится.
	constexpr float OneShotWaitSeconds = 1.f;
	// Пределы ускорения и замедления клипов ходьбы и бега. Нижний — ниже шага с телевизором (165 из 300 см/с), иначе ноги скользят.
	constexpr float MinStepRate = 0.5f;
	constexpr float MaxStepRate = 1.5f;
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

	// Свой аниматор вместо «одного клипа»: он плавно перетекает из клипа в клип.
	Mesh->SetSkeletalMesh(Model);
	Mesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	Mesh->SetAnimInstanceClass(UObshagaAnimInstance::StaticClass());
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
	// Скорость сглажена (см. TickComponent): комендант идёт рывками от точки к точке, и без этого шаг мигал бы со стойкой.
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
		if (Resident->IsStumbling() && Config->Fall)
		{
			return Config->Fall;
		}
		// Клип уже шёл, а игрок пошёл — клип отменяется, а не начинается заново после остановки.
		if (OneShot && bOneShotStarted && Character->GetVelocity().Size2D() >= Config->MoveSpeedThreshold)
		{
			OneShot = nullptr;
		}
		// Вприсядку разовые клипы не играем: они сняты стоя, и персонаж на секунду вставал бы во весь рост.
		// Взял предмет — руки сразу держат его поверх приседа (см. TickComponent).
		if (OneShot && Character->IsCrouched() && Config->Crouch)
		{
			OneShot = nullptr;
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
	// Присед в паке — неподвижная поза, отдельной ходьбы вприсядку нет. Стоя на месте — поза приседа,
	// на ходу — обычные шаги в темпе настоящей скорости (см. TickComponent), иначе персонаж ехал бы по полу без шагов.
	if (Character->IsCrouched() && Config->Crouch)
	{
		return (Speed > Config->MoveSpeedThreshold && Config->Walk) ? Config->Walk : Config->Crouch;
	}
	// Предмет в руках клип не меняет: ноги идут как обычно, а руки поверх них держат предмет (см. TickComponent).
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

	const float Smoothing = Cast<AKomendantCharacter>(GetOwner()) ? KomendantSpeedSmoothing : ResidentSpeedSmoothing;
	SmoothedSpeed = FMath::FInterpTo(SmoothedSpeed, GetOwner()->GetVelocity().Size2D(), DeltaTime, Smoothing);

	bool bLoop = true;
	UAnimSequenceBase* Wanted = PickAnimation(bLoop);

	// Шаги под настоящую скорость: клип ходьбы или бега играется быстрее или медленнее, в разумных пределах.
	const UObshagaLookConfig* LookConfig = UObshagaLookConfig::Get();
	if (UObshagaAnimInstance* Animator = Cast<UObshagaAnimInstance>(CastChecked<ACharacter>(GetOwner())->GetMesh()->GetAnimInstance()))
	{
		float Rate = 1.f;
		if (Wanted && Wanted == LookConfig->Walk)
		{
			Rate = SmoothedSpeed / LookConfig->WalkClipSpeed;
		}
		else if (Wanted && Wanted == LookConfig->Sprint)
		{
			Rate = SmoothedSpeed / LookConfig->SprintClipSpeed;
		}
		Animator->SetPlayRate(FMath::Clamp(Rate, MinStepRate, MaxStepRate));

		// Несёт предмет: руки держат его поверх любого клипа, кроме разовых (взял, бросил, жест — там руки заняты).
		// Тяжёлое едет на макушке (AItemActor::ApplyPlacement) — руки свободны и машут как обычно, а не держат пустоту перед грудью.
		const AObshagaCharacter* Resident = Cast<AObshagaCharacter>(GetOwner());
		const UCarryComponent* Carry = Resident ? Resident->GetCarryComponent() : nullptr;
		UAnimSequence* Hold = (Carry && Carry->IsCarrying() && !Carry->IsCarryingHeavy() && bLoop) ? Cast<UAnimSequence>(LookConfig->Carry) : nullptr;
		Animator->SetOverlay(Hold, LookConfig->CarryBones, LookConfig->BlendSeconds);
		if ((Hold != nullptr) != bHolding)
		{
			bHolding = Hold != nullptr;
			UE_LOG(LogObshaga, Verbose, TEXT("[%s] %s holds item: %d"), *GetNameSafe(GetWorld()), *GetOwner()->GetName(), bHolding ? 1 : 0);
		}
	}
	// Тот же клип, но теперь по кругу (жест «нет» сменился допросом с тем же клипом) — тоже передаём аниматору,
	// иначе разовый клип застыл бы на последнем кадре.
	if (Wanted && (Wanted != Current || bLoop != bCurrentLoops))
	{
		USkeletalMeshComponent* Mesh = CastChecked<ACharacter>(GetOwner())->GetMesh();
		UObshagaAnimInstance* Animator = Cast<UObshagaAnimInstance>(Mesh->GetAnimInstance());
		UAnimSequence* Sequence = Cast<UAnimSequence>(Wanted);
		if (!Animator || !Sequence)
		{
			// Аниматор ещё не создан (модель только что сменили) — попробуем на следующем кадре.
			return;
		}
		Current = Wanted;
		bCurrentLoops = bLoop;
		Animator->Play(Sequence, bLoop, UObshagaLookConfig::Get()->BlendSeconds);
		UE_LOG(LogObshaga, Verbose, TEXT("[%s] %s anim: %s"), *GetNameSafe(GetWorld()), *GetOwner()->GetName(), *Wanted->GetName());
	}
}

FString UObshagaLookComponent::GetCurrentAnimName() const
{
	return bHolding ? GetNameSafe(Current) + TEXT("+holding") : GetNameSafe(Current);
}
