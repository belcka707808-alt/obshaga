#include "ObshagaCharacter.h"

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
}

void AObshagaCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(AObshagaCharacter, bIsSprinting, COND_SkipOwner);
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

const UObshagaCharacterConfig* AObshagaCharacter::GetConfig() const
{
	return Config ? Config.Get() : GetDefault<UObshagaCharacterConfig>();
}

void AObshagaCharacter::SetSprinting(bool bNewSprinting)
{
	// Вприсядку не бегаем.
	bNewSprinting = bNewSprinting && !IsCrouched();
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
	bIsSprinting = bNewSprinting && !IsCrouched();
	UpdateMovementSpeed();
}

void AObshagaCharacter::OnRep_IsSprinting()
{
	UpdateMovementSpeed();
}

void AObshagaCharacter::UpdateMovementSpeed()
{
	const UObshagaCharacterConfig* Cfg = GetConfig();
	GetCharacterMovement()->MaxWalkSpeed = bIsSprinting ? Cfg->SprintSpeed : Cfg->WalkSpeed;
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
		UE_LOG(LogObshaga, Verbose, TEXT("[%s] %s entered room %s"), *GetNameSafe(GetWorld()), *GetName(), *Room->RoomId.ToString());
	}
}

void AObshagaCharacter::NotifyActorEndOverlap(AActor* OtherActor)
{
	Super::NotifyActorEndOverlap(OtherActor);

	if (ARoomVolume* Room = Cast<ARoomVolume>(OtherActor))
	{
		OverlappingRooms.Remove(Room);
		UE_LOG(LogObshaga, Verbose, TEXT("[%s] %s left room %s"), *GetNameSafe(GetWorld()), *GetName(), *Room->RoomId.ToString());
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
