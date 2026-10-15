#include "ObshagaCharacter.h"

#include "CarryComponent.h"
#include "DoorActor.h"
#include "EngineUtils.h"
#include "GameEventSubsystem.h"
#include "HidingSpot.h"
#include "InteractionComponent.h"
#include "NightExitDoor.h"
#include "ObshagaLookComponent.h"
#include "ObshagaStory.h"
#include "ObshagaPlayerState.h"
#include "GameFramework/PlayerState.h"
#include "Obshaga.h"
#include "ObshagaCharacterConfig.h"
#include "RoomVolume.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Net/UnrealNetwork.h"

namespace
{
	// Поймали в прыжке: насколько глубоко под ногами ищем пол, чтобы поставить на него до допроса.
	constexpr float FrozenFloorSearchDepth = 400.f;
	// Вышел из укрытия: как часто проверяем, что персонаж уже снаружи, и сколько самое большее ждём, чтобы вернуть камеру.
	constexpr float PeekRestoreCheckSeconds = 0.05f;
	constexpr float PeekRestoreMaxWait = 1.f;
}

AObshagaCharacter::AObshagaCharacter()
{
	PrimaryActorTick.bCanEverTick = false;

	GetCapsuleComponent()->InitCapsuleSize(35.f, 90.f);

	// Камера крутится мышью, а тело поворачивается туда, куда идёт.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->bOrientRotationToMovement = true;
	Movement->RotationRate = FRotator(0.f, 540.f, 0.f);
	Movement->NavAgentProps.bCanCrouch = true;
	Movement->SetCrouchedHalfHeight(60.f);

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->SetRelativeLocation(FVector(0.f, 0.f, 50.f));
	CameraBoom->bUsePawnControlRotation = true;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	InteractionComponent = CreateDefaultSubobject<UInteractionComponent>(TEXT("InteractionComponent"));
	CarryComponent = CreateDefaultSubobject<UCarryComponent>(TEXT("CarryComponent"));
	LookComponent = CreateDefaultSubobject<UObshagaLookComponent>(TEXT("LookComponent"));

	CarryPoint = CreateDefaultSubobject<USceneComponent>(TEXT("CarryPoint"));
	CarryPoint->SetupAttachment(RootComponent);
	CarryPoint->SetRelativeLocation(FVector(65.f, 0.f, 10.f));
}

void AObshagaCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(AObshagaCharacter, bIsSprinting, COND_SkipOwner);
	DOREPLIFETIME(AObshagaCharacter, HidingSpot);
	DOREPLIFETIME(AObshagaCharacter, bIsFrozen);
	DOREPLIFETIME(AObshagaCharacter, bIsGhost);
	DOREPLIFETIME(AObshagaCharacter, LookIndex);
}

void AObshagaCharacter::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	const UObshagaCharacterConfig* Cfg = GetConfig();
	CameraBoom->TargetArmLength = Cfg->CameraArmLength;

	// Камера не подстраивается под темноту сама — иначе ночной полумрак она превратила бы обратно в день.
	if (Cfg->FixedExposureBrightness > 0.f)
	{
		FPostProcessSettings& PostProcess = FollowCamera->PostProcessSettings;
		PostProcess.bOverride_AutoExposureMinBrightness = true;
		PostProcess.bOverride_AutoExposureMaxBrightness = true;
		PostProcess.AutoExposureMinBrightness = Cfg->FixedExposureBrightness;
		PostProcess.AutoExposureMaxBrightness = Cfg->FixedExposureBrightness;
	}
	GetCharacterMovement()->MaxWalkSpeedCrouched = Cfg->CrouchSpeed;
	UpdateMovementSpeed();
}

void AObshagaCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Если персонаж появился уже внутри зоны, движок запоминает пересечение молча,
	// без события входа. Поэтому стартовые зоны забираем сами.
	UpdateOverlaps(false);

	TArray<AActor*> StartRooms;
	GetOverlappingActors(StartRooms, ARoomVolume::StaticClass());
	for (AActor* RoomActor : StartRooms)
	{
		ARoomVolume* Room = CastChecked<ARoomVolume>(RoomActor);
		OverlappingRooms.AddUnique(Room);
		UE_LOG(LogObshaga, Verbose, TEXT("[%s] %s starts in room %s"), *GetNameSafe(GetWorld()), *GetName(), *Room->RoomId.ToString());
	}
}

void AObshagaCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	// Модель — по «слоту внешности» игрока: он выдаётся при входе и не меняется от раунда к раунду.
	if (const AObshagaPlayerState* State = GetPlayerState<AObshagaPlayerState>())
	{
		LookIndex = static_cast<uint8>(State->GetLookSlot() + 1);
		OnRep_LookIndex();
	}
}

void AObshagaCharacter::NotifyControllerChanged()
{
	Super::NotifyControllerChanged();

	// Укрытие могло прийти по сети раньше, чем стало известно, что это наш персонаж.
	UpdateHidingCamera();
}

void AObshagaCharacter::OnRep_LookIndex()
{
	if (LookIndex > 0)
	{
		LookComponent->ApplyLook(LookIndex - 1);
	}
}

void AObshagaCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Игрок вышел из игры: то, что он нёс, падает на пол, а его укрытие освобождается.
	if (HasAuthority() && EndPlayReason == EEndPlayReason::Destroyed)
	{
		CarryComponent->Drop();
		if (HidingSpot)
		{
			HidingSpot->ForgetHiddenPlayer(this);
		}
	}

	Super::EndPlay(EndPlayReason);
}

const UObshagaCharacterConfig* AObshagaCharacter::GetConfig() const
{
	return Config ? Config.Get() : GetDefault<UObshagaCharacterConfig>();
}

void AObshagaCharacter::SetSprinting(bool bNewSprinting)
{
	// Просьбу помним: кончится запрет (тяжёлое в руках, укрытие, допрос) — бег включится сам, если Shift ещё зажат.
	bSprintHeld = bNewSprinting;

	// Вприсядку, с тяжёлым предметом, в укрытии и на допросе не бегаем.
	bNewSprinting = bNewSprinting && !IsCrouched() && !CarryComponent->IsCarryingHeavy() && !IsHiding() && !bIsFrozen;
	if (bIsSprinting == bNewSprinting)
	{
		return;
	}

	bIsSprinting = bNewSprinting;
	UpdateMovementSpeed();

	if (!HasAuthority())
	{
		ServerSetSprinting(bNewSprinting);
	}
}

void AObshagaCharacter::ServerSetSprinting_Implementation(bool bNewSprinting)
{
	// Просьба могла прийти, когда его уже поймали или он уже в шкафу: тогда не включаем — иначе после допроса
	// сервер считал бы, что он бежит, а его машина (сбросившая бег у себя) — что идёт.
	bIsSprinting = bNewSprinting && !IsCrouched() && !CarryComponent->IsCarryingHeavy() && !IsHiding() && !bIsFrozen;
	UpdateMovementSpeed();
}

void AObshagaCharacter::OnRep_IsSprinting()
{
	UpdateMovementSpeed();
}

void AObshagaCharacter::UpdateMovementSpeed()
{
	const UObshagaCharacterConfig* Cfg = GetConfig();
	float Speed = bIsSprinting ? Cfg->SprintSpeed : Cfg->WalkSpeed;
	if (CarryComponent->IsCarryingHeavy())
	{
		Speed = Cfg->WalkSpeed * Cfg->HeavyCarrySpeedMultiplier;
	}
	// Упавший после броска тяжёлого лежит, пока не выйдет время.
	GetCharacterMovement()->MaxWalkSpeed = IsStumbling() ? 1.f : Speed;
}

void AObshagaCharacter::OnCarriedItemChanged()
{
	// С тяжёлым не бегают. Сбрасываем и на сервере, и у самого игрока: владельцу сервер свой флаг бега не шлёт,
	// и без этого его машина после броска телевизора считала бы, что он бежит, а сервер — что идёт (персонажа дёргало).
	const bool bHeavy = CarryComponent->IsCarryingHeavy();
	if (bHeavy && (HasAuthority() || IsLocallyControlled()))
	{
		bIsSprinting = false;
	}
	UpdateMovementSpeed();

	if (!bHeavy)
	{
		ResumeSprintIfHeld();
	}
}

bool AObshagaCharacter::IsLocalHuman() const
{
	const APlayerController* PlayerController = Cast<APlayerController>(GetController());
	return PlayerController && PlayerController->IsLocalController();
}

void AObshagaCharacter::ResumeSprintIfHeld()
{
	// Только у самого игрока: боты включают бег сами, а за чужого игрока решает его машина.
	if (bSprintHeld && !bIsSprinting && !IsHiding() && !bIsFrozen && IsLocalHuman())
	{
		SetSprinting(true);
	}
}

void AObshagaCharacter::TryEmote(int32 Index)
{
	if (GetConfig()->Emotes.IsValidIndex(Index) && !IsGhost() && !IsHiding())
	{
		ServerEmote(static_cast<uint8>(Index));
	}
}

void AObshagaCharacter::ServerEmote_Implementation(uint8 Index)
{
	// Из укрытия и призраком не говорят; чаще раза в секунду — тоже.
	const float Now = GetWorld()->GetTimeSeconds();
	if (!GetConfig()->Emotes.IsValidIndex(Index) || IsGhost() || IsHiding() || Now - LastEmoteServerTime < GetConfig()->EmoteCooldown)
	{
		return;
	}
	LastEmoteServerTime = Now;
	MulticastEmote(Index);
}

void AObshagaCharacter::Stumble(float Seconds)
{
	if (HasAuthority() && Seconds > 0.f && !IsStumbling())
	{
		MulticastStumble(Seconds);
	}
}

bool AObshagaCharacter::IsStumbling() const
{
	return GetWorld()->GetTimeSeconds() < StumbleEndTime;
}

void AObshagaCharacter::MulticastStumble_Implementation(float Seconds)
{
	// Скорость ходьбы обнуляется и на сервере, и у самого игрока — так его предсказание движения не спорит с сервером.
	StumbleEndTime = GetWorld()->GetTimeSeconds() + Seconds;
	UpdateMovementSpeed();
	GetWorldTimerManager().SetTimer(StumbleTimer, this, &AObshagaCharacter::UpdateMovementSpeed, Seconds + 0.05f, false);
}

void AObshagaCharacter::MulticastEmote_Implementation(uint8 Index)
{
	BotSpeech = FText::GetEmpty();
	EmoteIndex = Index;
	EmoteStartTime = GetWorld()->GetTimeSeconds();
}

void AObshagaCharacter::SayBotLine(uint8 Situation, uint8 Variant)
{
	if (HasAuthority() && !IsGhost() && !IsHiding())
	{
		MulticastBotLine(Situation, Variant);
	}
}

void AObshagaCharacter::MulticastBotLine_Implementation(uint8 Situation, uint8 Variant)
{
	// По сети идут только номера: текст каждая машина берёт из DA_Story сама.
	BotSpeech = ObshagaStory::BotLine(static_cast<ObshagaStory::EBotLine>(Situation), Variant);
	EmoteIndex = INDEX_NONE;
	EmoteStartTime = GetWorld()->GetTimeSeconds();
	UE_LOG(LogObshaga, Verbose, TEXT("%s says: %s"), *GetNameSafe(GetPlayerState()), *BotSpeech.ToString());
}

FText AObshagaCharacter::GetActiveSpeech() const
{
	if (GetWorld()->GetTimeSeconds() - EmoteStartTime >= GetConfig()->EmoteSeconds)
	{
		return FText::GetEmpty();
	}
	return GetConfig()->Emotes.IsValidIndex(EmoteIndex) ? GetConfig()->Emotes[EmoteIndex] : BotSpeech;
}

int32 AObshagaCharacter::GetActiveEmote() const
{
	const bool bActive = GetWorld()->GetTimeSeconds() - EmoteStartTime < GetConfig()->EmoteSeconds;
	return (bActive && GetConfig()->Emotes.IsValidIndex(EmoteIndex)) ? EmoteIndex : INDEX_NONE;
}

void AObshagaCharacter::SetCameraShakeOffset(const FVector& Offset)
{
	FollowCamera->SetRelativeLocation(Offset);
}

void AObshagaCharacter::EnterHidingSpot(AHidingSpot* Spot)
{
	check(HasAuthority());

	UnCrouch();
	bIsSprinting = false;
	HidingSpot = Spot;

	const float HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	SetActorLocation(Spot->GetHiddenPlayerLocation(HalfHeight), false, nullptr, ETeleportType::TeleportPhysics);
	ApplyHiding();
	ForceNetUpdate();
}

void AObshagaCharacter::ExitHidingSpot(const FVector& ExitLocation, const FVector& SideStep)
{
	check(HasAuthority());

	// Сначала вернуть движение: перенос ниже сам находит пол под ногами.
	HidingSpot = nullptr;
	ApplyHiding();

	// Перед дверцей обычно стоит тот, кто её открыл. TeleportTo не ставит внутрь чужой капсулы или мебели,
	// а выталкивает рядом; не вышло — пробуем встать сбоку от дверцы, и только в крайнем случае ставим как есть.
	const FRotator Rotation = GetActorRotation();
	const bool bPlaced = TeleportTo(ExitLocation, Rotation)
		|| (!SideStep.IsNearlyZero() && (TeleportTo(ExitLocation + SideStep, Rotation) || TeleportTo(ExitLocation - SideStep, Rotation)));
	if (!bPlaced)
	{
		UE_LOG(LogObshaga, Verbose, TEXT("%s: no free room to leave hiding at %s, placing as is"), *GetName(), *ExitLocation.ToCompactString());
		SetActorLocation(ExitLocation, false, nullptr, ETeleportType::TeleportPhysics);
	}
	ForceNetUpdate();
}

void AObshagaCharacter::SetFrozen(bool bNewFrozen)
{
	check(HasAuthority());

	// Поймали в прыжке: без движения персонаж не падает и провисел бы в воздухе весь допрос. Сначала ставим на пол.
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (bNewFrozen && Movement->IsFalling())
	{
		const UCapsuleComponent* Capsule = GetCapsuleComponent();
		FCollisionQueryParams Params(SCENE_QUERY_STAT(FrozenFloor), false, this);
		FCollisionResponseParams ResponseParams;
		Capsule->InitSweepCollisionParams(Params, ResponseParams);
		// Чуть уже капсулы: у стены широкая капсула упёрлась бы в неё с самого начала.
		const FCollisionShape Shape = FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius() * 0.9f, Capsule->GetScaledCapsuleHalfHeight());
		const FVector From = GetActorLocation();
		FHitResult Floor;
		if (GetWorld()->SweepSingleByChannel(Floor, From, From - FVector(0.f, 0.f, FrozenFloorSearchDepth), FQuat::Identity, Capsule->GetCollisionObjectType(), Shape, Params, ResponseParams)
			&& !Floor.bStartPenetrating)
		{
			SetActorLocation(Floor.Location + FVector(0.f, 0.f, 2.f), false, nullptr, ETeleportType::TeleportPhysics);
			UE_LOG(LogObshaga, Verbose, TEXT("%s caught mid-air, put down %.0f cm"), *GetName(), From.Z - Floor.Location.Z);
		}
	}

	bIsFrozen = bNewFrozen;
	if (bNewFrozen)
	{
		bIsSprinting = false;
	}
	ApplyHiding();
	ForceNetUpdate();
}

void AObshagaCharacter::SetGhost(bool bNewGhost)
{
	check(HasAuthority());

	if (bNewGhost)
	{
		// Призрак ничего не несёт.
		CarryComponent->Drop();
	}
	bIsGhost = bNewGhost;
	ApplyHiding();
	ForceNetUpdate();
}

void AObshagaCharacter::OnRep_IsGhost()
{
	ApplyHiding();
}

void AObshagaCharacter::OnRep_IsFrozen()
{
	ApplyHiding();
}

void AObshagaCharacter::OnRep_HidingSpot()
{
	ApplyHiding();
}

void AObshagaCharacter::ApplyHiding()
{
	const bool bHide = IsHiding();
	SetActorHiddenInGame(bHide || bIsGhost);

	// Призрак проходит сквозь людей и не мешает им.
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, bIsGhost ? ECR_Ignore : ECR_Block);

	// И сквозь двери: открыть их он не может, а запереть выселенного в комнате было бы нечестно.
	for (TActorIterator<ADoorActor> It(GetWorld()); It; ++It)
	{
		GetCapsuleComponent()->IgnoreActorWhenMoving(*It, bIsGhost);
	}

	// В укрытии персонаж стоит на месте; столкновения не трогаем, чтобы он оставался «в комнате».
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (bHide || bIsFrozen)
	{
		// Сервер бег уже сбросил, но владельцу этот флаг не шлёт — сбрасываем и у него, иначе после выхода
		// его машина считала бы, что персонаж бежит, а сервер — что идёт.
		if (IsLocallyControlled())
		{
			bIsSprinting = false;
		}
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}
	else if (Movement->MovementMode == MOVE_None)
	{
		Movement->SetMovementMode(MOVE_Walking);
	}
	UpdateMovementSpeed();
	UpdateHidingCamera();

	if (!bHide && !bIsFrozen)
	{
		ResumeSprintIfHeld();
	}
}

void AObshagaCharacter::UpdateHidingCamera()
{
	// Только у того, кто сам прячется. На ночном выходе камера остаётся обычной: там она стоит за дверью на улице.
	AHidingSpot* Spot = HidingSpot;
	if (Spot && !Spot->IsA<ANightExitDoor>() && IsLocalHuman())
	{
		GetWorldTimerManager().ClearTimer(PeekRestoreTimer);
		if (!bPeekCamera)
		{
			// Запоминаем обычную руку камеры, чтобы на выходе вернуть ровно её.
			bPeekCamera = true;
			SavedBoomLocation = CameraBoom->GetRelativeLocation();
			SavedBoomArmLength = CameraBoom->TargetArmLength;
			bSavedBoomCollisionTest = CameraBoom->bDoCollisionTest;
			// Забегая в шкаф, игрок смотрел на его дверцу — сразу разворачиваем взгляд наружу.
			GetController()->SetControlRotation(Spot->GetPeekRotation());
		}
		PeekSpot = Spot;
		// Изнутри мебели камера видела бы всё насквозь. Ставим её к самой дверце, как будто смотрим в щель:
		// рука нулевой длины, мебель её не отталкивает, а сама она стоит в мире, а не за персонажем.
		CameraBoom->SetUsingAbsoluteLocation(true);
		CameraBoom->SetWorldLocation(Spot->GetPeekLocation());
		CameraBoom->TargetArmLength = 0.f;
		CameraBoom->bDoCollisionTest = false;
		UE_LOG(LogObshaga, Verbose, TEXT("%s: hiding camera at %s"), *GetName(), *Spot->GetPeekLocation().ToCompactString());
		return;
	}
	if (!bPeekCamera)
	{
		return;
	}

	// Что игрок вышел, его машина узнаёт чуть раньше, чем новое место (у гостя — на время пинга). Пока персонаж
	// ещё сидит внутри мебели, камера стоит у дверцы: иначе на мгновение снова показала бы шкаф изнутри.
	const float Now = GetWorld()->GetTimeSeconds();
	if (!PeekRestoreTimer.IsValid())
	{
		PeekRestoreDeadline = Now + PeekRestoreMaxWait;
	}
	const AHidingSpot* OldSpot = PeekSpot.Get();
	if (IsLocalHuman() && OldSpot && Now < PeekRestoreDeadline && OldSpot->GetComponentsBoundingBox().IsInsideOrOn(GetActorLocation()))
	{
		GetWorldTimerManager().SetTimer(PeekRestoreTimer, this, &AObshagaCharacter::UpdateHidingCamera, PeekRestoreCheckSeconds, false);
		return;
	}

	GetWorldTimerManager().ClearTimer(PeekRestoreTimer);
	bPeekCamera = false;
	PeekSpot.Reset();
	CameraBoom->SetUsingAbsoluteLocation(false);
	CameraBoom->SetRelativeLocation(SavedBoomLocation);
	CameraBoom->TargetArmLength = SavedBoomArmLength;
	CameraBoom->bDoCollisionTest = bSavedBoomCollisionTest;
	CameraBoom->UpdateComponentToWorld();
	UE_LOG(LogObshaga, Verbose, TEXT("%s: hiding camera restored"), *GetName());
}

void AObshagaCharacter::OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnStartCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
	FixLookMeshHeight(HalfHeightAdjust);

	if (bIsSprinting && (HasAuthority() || IsLocallyControlled()))
	{
		SetSprinting(false);
	}
}

void AObshagaCharacter::OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnEndCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
	FixLookMeshHeight(0.f);
}

void AObshagaCharacter::FixLookMeshHeight(float CrouchAdjust)
{
	float StandingZ = 0.f;
	if (LookComponent->GetStandingMeshZ(StandingZ))
	{
		FVector Location = GetMesh()->GetRelativeLocation();
		Location.Z = StandingZ + CrouchAdjust;
		GetMesh()->SetRelativeLocation(Location);
		BaseTranslationOffset.Z = Location.Z;
	}
}

void AObshagaCharacter::NotifyActorBeginOverlap(AActor* OtherActor)
{
	Super::NotifyActorBeginOverlap(OtherActor);

	if (ARoomVolume* Room = Cast<ARoomVolume>(OtherActor))
	{
		OverlappingRooms.AddUnique(Room);
		PublishRoomEvent(EGameEventType::RoomEntered, Room);
	}
}

void AObshagaCharacter::NotifyActorEndOverlap(AActor* OtherActor)
{
	Super::NotifyActorEndOverlap(OtherActor);

	if (ARoomVolume* Room = Cast<ARoomVolume>(OtherActor))
	{
		OverlappingRooms.Remove(Room);
		PublishRoomEvent(EGameEventType::RoomLeft, Room);
	}
}

void AObshagaCharacter::PublishRoomEvent(EGameEventType Type, const ARoomVolume* Room)
{
	if (!HasAuthority())
	{
		return;
	}

	if (UGameEventSubsystem* Bus = UGameEventSubsystem::Get(this))
	{
		FGameEvent Event;
		Event.Type = Type;
		Event.Instigator = GetPlayerState();
		Event.RoomId = Room->RoomId;
		Bus->Publish(Event);
	}
}

ARoomVolume* AObshagaCharacter::GetCurrentRoom() const
{
	// На стыке двух зон считаем, что персонаж в той, куда вошёл последней.
	for (int32 Index = OverlappingRooms.Num() - 1; Index >= 0; --Index)
	{
		if (ARoomVolume* Room = OverlappingRooms[Index].Get())
		{
			return Room;
		}
	}
	return nullptr;
}
