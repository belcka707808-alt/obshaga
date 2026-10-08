#include "ItemActor.h"

#include "CarryComponent.h"
#include "GameEventSubsystem.h"
#include "HidingSpot.h"
#include "NoiseStatics.h"
#include "Obshaga.h"
#include "ObshagaCharacter.h"
#include "ObshagaItemData.h"
#include "ObshagaPlayerController.h"
#include "ObshagaVisuals.h"
#include "RoomVolume.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "ItemActor"

namespace
{
	// Предметы укладываются на пол при старте уровня — это не шум.
	constexpr float SettleGraceSeconds = 2.f;
	constexpr float NoiseCooldownSeconds = 0.3f;

	// Спрятанные предметы сервер убирает под карту, чтобы их положение не выдавало тайник.
	const FVector HiddenItemsLocation(0.f, 0.f, -5000.f);
}

AItemActor::AItemActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(true);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	RootComponent = Mesh;

	// Предмет бьётся о стены и пол, но не толкает игроков и не мешает камере.
	Mesh->SetCollisionProfileName(TEXT("PhysicsActor"));
	Mesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	Mesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Mesh->SetSimulatePhysics(true);
	Mesh->SetNotifyRigidBodyCollision(true);

	// Серый куб движка — заглушка до настоящих моделей.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		Mesh->SetStaticMesh(CubeMesh.Object);
	}
}

void AItemActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AItemActor, Placement);
}

void AItemActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	ApplyItemData();
}

void AItemActor::BeginPlay()
{
	Super::BeginPlay();

	ApplyItemData();
	ApplyPlacement();
	ObshagaVisuals::Tint(Mesh, GetItemData()->Color);

	if (HasAuthority())
	{
		InitialTransform = GetActorTransform();
		Mesh->OnComponentHit.AddDynamic(this, &AItemActor::OnMeshHit);
	}
}

const UObshagaItemData* AItemActor::GetItemData() const
{
	return ItemData ? ItemData.Get() : GetDefault<UObshagaItemData>();
}

FText AItemActor::GetDisplayName() const
{
	const FText Name = GetItemData()->DisplayName;
	return Name.IsEmpty() ? LOCTEXT("Unnamed", "Предмет") : Name;
}

bool AItemActor::IsHeavy() const
{
	return GetItemData()->bHeavy;
}

void AItemActor::ApplyItemData()
{
	const UObshagaItemData* Data = GetItemData();
	Mesh->SetWorldScale3D(Data->BoxSize / 100.f);
	Mesh->SetMassOverrideInKg(NAME_None, Data->Weight, true);
}

FText AItemActor::GetInteractionPrompt(const AObshagaCharacter* By) const
{
	if (Placement.State != EItemState::World || !By || By->GetCarryComponent()->IsCarrying())
	{
		return FText::GetEmpty();
	}
	return FText::Format(LOCTEXT("PickUp", "Взять: {0}"), GetDisplayName());
}

void AItemActor::Interact(AObshagaCharacter* By)
{
	if (HasAuthority() && By && Placement.State == EItemState::World)
	{
		By->GetCarryComponent()->PickUp(this);
	}
}

FText AItemActor::GetSecondaryPrompt(const AObshagaCharacter* By) const
{
	if (Placement.State != EItemState::World || !By || !GetItemData()->bReadable)
	{
		return FText::GetEmpty();
	}
	return FText::Format(LOCTEXT("Read", "Прочитать: {0}"), GetDisplayName());
}

void AItemActor::SecondaryInteract(AObshagaCharacter* By)
{
	if (Placement.State == EItemState::World)
	{
		ReadBy(By);
	}
}

void AItemActor::ReadBy(AObshagaCharacter* By)
{
	if (!HasAuthority() || !By || !GetItemData()->bReadable)
	{
		return;
	}

	if (NoteText.IsEmpty())
	{
		// Слух — про случайного жильца, кроме самого читателя.
		const FText Templates[] = {
			LOCTEXT("Rumor1", "Говорят, {0} ворует еду из холодильника"),
			LOCTEXT("Rumor2", "{0} стучит коменданту. Проверено"),
			LOCTEXT("Rumor3", "Это {0} сломал душ на первом этаже"),
			LOCTEXT("Rumor4", "{0} прячет под кроватью что-то запрещённое"),
			LOCTEXT("Rumor5", "{0} по ночам выходит из общаги. Куда — никто не знает"),
			LOCTEXT("Rumor6", "{0} врёт. Всегда. Просто не верьте")
		};

		TArray<APlayerState*> Others;
		if (const AGameStateBase* GameState = GetWorld()->GetGameState())
		{
			for (APlayerState* Other : GameState->PlayerArray)
			{
				if (Other && Other != By->GetPlayerState())
				{
					Others.Add(Other);
				}
			}
		}
		const FText Subject = Others.IsEmpty() ? LOCTEXT("RumorSomeone", "кое-кто")
			: FText::FromString(Others[FMath::RandRange(0, Others.Num() - 1)]->GetPlayerName());
		NoteText = FText::Format(Templates[FMath::RandRange(0, UE_ARRAY_COUNT(Templates) - 1)], Subject);
	}

	if (AObshagaPlayerController* Controller = Cast<AObshagaPlayerController>(By->GetController()))
	{
		Controller->ClientShowNotice(FText::Format(LOCTEXT("NoteNotice", "В записке: «{0}»"), NoteText));
	}
	UGameEventSubsystem::PublishFrom(By, EGameEventType::NoteRead, this);
}

void AItemActor::SetCarriedBy(AObshagaCharacter* Carrier)
{
	check(HasAuthority());

	LastCarrier = Carrier;
	HidingSpot = nullptr;
	Placement.State = EItemState::Carried;
	Placement.Holder = Carrier;
	ApplyPlacement();
	ForceNetUpdate();
}

void AItemActor::ReleaseToWorld(const FVector& Location, const FVector& Velocity)
{
	check(HasAuthority());

	Placement.State = EItemState::World;
	Placement.Holder = nullptr;
	ApplyPlacement();

	SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
	Mesh->SetPhysicsLinearVelocity(Velocity);
	ForceNetUpdate();
}

void AItemActor::SetHiddenIn(AHidingSpot* Spot, AObshagaCharacter* By)
{
	check(HasAuthority());

	LastHiddenBy = By ? By->GetPlayerState() : nullptr;
	// Клиентам не сообщаем, в каком тайнике предмет: ни ссылкой, ни координатами.
	HidingSpot = Spot;
	Placement.State = EItemState::Hidden;
	Placement.Holder = nullptr;
	ApplyPlacement();
	SetActorLocation(HiddenItemsLocation, false, nullptr, ETeleportType::TeleportPhysics);
	ForceNetUpdate();
}

void AItemActor::ForgetCarrier()
{
	LastCarrier.Reset();
	// Пока изъятое укладывается на место, оно не шумит.
	LastNoiseTime = GetWorld()->GetTimeSeconds() + SettleGraceSeconds;
}

void AItemActor::ResetToInitial()
{
	check(HasAuthority());

	LastCarrier.Reset();
	LastHiddenBy.Reset();
	NoteText = FText::GetEmpty();
	HidingSpot = nullptr;
	Placement.State = EItemState::World;
	Placement.Holder = nullptr;
	ApplyPlacement();

	SetActorLocationAndRotation(InitialTransform.GetLocation(), InitialTransform.GetRotation(), false, nullptr, ETeleportType::TeleportPhysics);
	Mesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
	Mesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	// После возврата на место предмет ещё чуть оседает — это не шум.
	LastNoiseTime = GetWorld()->GetTimeSeconds() + 2.f;
	ForceNetUpdate();
}

FName AItemActor::GetCurrentRoomId() const
{
	const ARoomVolume* Room = nullptr;
	switch (Placement.State)
	{
	case EItemState::Carried:
		if (const AObshagaCharacter* Carrier = Cast<AObshagaCharacter>(Placement.Holder))
		{
			Room = Carrier->GetCurrentRoom();
		}
		break;
	case EItemState::Hidden:
		if (HidingSpot)
		{
			Room = ARoomVolume::FindRoomAt(this, HidingSpot->GetActorLocation() + FVector(0.f, 0.f, 50.f));
		}
		break;
	default:
		Room = ARoomVolume::FindRoomAt(this, GetActorLocation());
		break;
	}
	return Room ? Room->RoomId : NAME_None;
}

void AItemActor::OnRep_Placement()
{
	ApplyPlacement();
}

void AItemActor::ApplyPlacement()
{
	const FAttachmentTransformRules SnapRules = FAttachmentTransformRules::SnapToTargetNotIncludingScale;

	switch (Placement.State)
	{
	case EItemState::World:
		DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		SetActorHiddenInGame(false);
		Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Mesh->SetSimulatePhysics(true);
		break;

	case EItemState::Carried:
		Mesh->SetSimulatePhysics(false);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		SetActorHiddenInGame(false);
		if (const AObshagaCharacter* Carrier = Cast<AObshagaCharacter>(Placement.Holder))
		{
			AttachToComponent(Carrier->GetCarryPoint(), SnapRules);
		}
		break;

	case EItemState::Hidden:
		Mesh->SetSimulatePhysics(false);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		SetActorHiddenInGame(true);
		break;
	}
}

void AItemActor::OnMeshHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	if (Placement.State != EItemState::World)
	{
		return;
	}

	const float Now = GetWorld()->GetTimeSeconds();
	if (Now < SettleGraceSeconds || Now - LastNoiseTime < NoiseCooldownSeconds)
	{
		return;
	}

	const UObshagaItemData* Data = GetItemData();
	const float ImpactSpeed = NormalImpulse.Size() / FMath::Max(Mesh->GetMass(), KINDA_SMALL_NUMBER);
	if (ImpactSpeed < Data->MinImpactSpeed)
	{
		return;
	}

	LastNoiseTime = Now;
	const float Loudness = FMath::Clamp(ImpactSpeed / Data->LoudImpactSpeed, 0.f, 1.f) * Data->NoiseFactor;
	UNoiseStatics::MakeGameNoise(this, Hit.ImpactPoint, Loudness, LastCarrier.Get());
}

#undef LOCTEXT_NAMESPACE
