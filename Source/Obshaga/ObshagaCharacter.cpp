#include "ObshagaCharacter.h"

#include "CarryComponent.h"
#include "DoorActor.h"
#include "EngineUtils.h"
#include "GameEventSubsystem.h"
#include "HidingSpot.h"
#include "InteractionComponent.h"
#include "Obshaga.h"
#include "ObshagaCharacterConfig.h"
#include "RoomVolume.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Net/UnrealNetwork.h"

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
}

void AObshagaCharacter::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	const UObshagaCharacterConfig* Cfg = GetConfig();
	CameraBoom->TargetArmLength = Cfg->CameraArmLength;
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
	// Вприсядку и с тяжёлым предметом не бегаем.
	bNewSprinting = bNewSprinting && !IsCrouched() && !CarryComponent->IsCarryingHeavy();
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
	bIsSprinting = bNewSprinting && !IsCrouched() && !CarryComponent->IsCarryingHeavy();
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
	GetCharacterMovement()->MaxWalkSpeed = Speed;
}

void AObshagaCharacter::OnCarriedItemChanged()
{
	if (HasAuthority() && CarryComponent->IsCarryingHeavy())
	{
		bIsSprinting = false;
	}
	UpdateMovementSpeed();
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

void AObshagaCharacter::ExitHidingSpot(const FVector& ExitLocation)
{
	check(HasAuthority());

	HidingSpot = nullptr;
	SetActorLocation(ExitLocation, false, nullptr, ETeleportType::TeleportPhysics);
	ApplyHiding();
	ForceNetUpdate();
}

void AObshagaCharacter::SetFrozen(bool bNewFrozen)
{
	check(HasAuthority());

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
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}
	else if (Movement->MovementMode == MOVE_None)
	{
		Movement->SetMovementMode(MOVE_Walking);
	}
	UpdateMovementSpeed();
}

void AObshagaCharacter::OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnStartCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);

	if (bIsSprinting && (HasAuthority() || IsLocallyControlled()))
	{
		SetSprinting(false);
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
