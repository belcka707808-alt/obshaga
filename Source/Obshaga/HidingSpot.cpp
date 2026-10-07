#include "HidingSpot.h"

#include "CarryComponent.h"
#include "GameEventSubsystem.h"
#include "InteractionComponent.h"
#include "ItemActor.h"
#include "Obshaga.h"
#include "ObshagaCharacter.h"
#include "ObshagaItemData.h"
#include "ObshagaPlayerController.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "HidingSpot"

namespace
{
	void Notify(AObshagaCharacter* Character, const FText& Text)
	{
		if (AObshagaPlayerController* Controller = Character ? Cast<AObshagaPlayerController>(Character->GetController()) : nullptr)
		{
			Controller->ClientShowNotice(Text);
		}
	}
}

AHidingSpot::AHidingSpot()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(RootComponent);
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));

	ExitPoint = CreateDefaultSubobject<USceneComponent>(TEXT("ExitPoint"));
	ExitPoint->SetupAttachment(RootComponent);

	// Серый куб движка — заглушка до настоящей мебели.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		Mesh->SetStaticMesh(CubeMesh.Object);
	}

	DisplayName = LOCTEXT("DefaultName", "Шкаф");
	ApplySize();
}

void AHidingSpot::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AHidingSpot, bBusy);
}

void AHidingSpot::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	ApplySize();
}

void AHidingSpot::BeginPlay()
{
	Super::BeginPlay();

	ApplySize();
}

void AHidingSpot::ApplySize()
{
	// Начало координат актора — центр основания; игрок выходит перед «лицом» тайника.
	Mesh->SetRelativeScale3D(BoxSize / 100.f);
	Mesh->SetRelativeLocation(FVector(0.f, 0.f, BoxSize.Z * 0.5f));
	ExitPoint->SetRelativeLocation(FVector(BoxSize.X * 0.5f + 60.f, 0.f, 95.f));
}

FText AHidingSpot::GetInteractionPrompt(const AObshagaCharacter* By) const
{
	if (!By)
	{
		return FText::GetEmpty();
	}
	if (By->GetHidingSpot() == this)
	{
		return LOCTEXT("Exit", "Выйти из укрытия");
	}
	if (bBusy)
	{
		return LOCTEXT("Busy", "Занято... не отходи");
	}
	if (const AItemActor* Item = By->GetCarryComponent()->GetCarriedItem())
	{
		return FText::Format(LOCTEXT("HideItem", "Спрятать сюда: {0}"), Item->GetDisplayName());
	}
	return FText::Format(LOCTEXT("Search", "Обыскать: {0}"), DisplayName);
}

FText AHidingSpot::GetSecondaryPrompt(const AObshagaCharacter* By) const
{
	if (!bCanHidePlayer || !By || bBusy || By->IsHiding() || By->GetCarryComponent()->IsCarrying())
	{
		return FText::GetEmpty();
	}
	return LOCTEXT("HideSelf", "Спрятаться");
}

void AHidingSpot::Interact(AObshagaCharacter* By)
{
	if (!HasAuthority() || !By)
	{
		return;
	}

	if (HiddenPlayer == By)
	{
		EjectHiddenPlayer();
		return;
	}
	if (bBusy || By->IsHiding())
	{
		return;
	}

	StartTimedAction(By, By->GetCarryComponent()->IsCarrying());
}

void AHidingSpot::SecondaryInteract(AObshagaCharacter* By)
{
	if (!HasAuthority() || !By || !bCanHidePlayer || bBusy || By->IsHiding() || By->GetCarryComponent()->IsCarrying())
	{
		return;
	}

	if (HiddenPlayer)
	{
		// Внутри уже кто-то сидит: места нет, а того, кто сидел, теперь нашли.
		Notify(By, LOCTEXT("Occupied", "Там уже кто-то прячется!"));
		EjectHiddenPlayer();
		return;
	}

	HiddenPlayer = By;
	By->EnterHidingSpot(this);
	UGameEventSubsystem::PublishFrom(By, EGameEventType::PlayerHid);
	UE_LOG(LogObshaga, Verbose, TEXT("%s hid in %s"), *By->GetName(), *GetName());
}

void AHidingSpot::StartTimedAction(AObshagaCharacter* By, bool bHideItem)
{
	bBusy = true;
	PendingBy = By;
	bPendingHideItem = bHideItem;

	const float Duration = bHideItem ? HideDuration : SearchDuration;
	if (Duration <= 0.f)
	{
		FinishTimedAction();
		return;
	}
	GetWorldTimerManager().SetTimer(ActionTimer, this, &AHidingSpot::FinishTimedAction, Duration, false);
}

void AHidingSpot::FinishTimedAction()
{
	bBusy = false;

	AObshagaCharacter* By = PendingBy.Get();
	PendingBy.Reset();

	// Отошёл раньше времени — действие сорвалось.
	if (!By || By->IsHiding() || !By->GetInteractionComponent()->IsInRange(this, UInteractionComponent::ServerRangeSlack))
	{
		Notify(By, LOCTEXT("Interrupted", "Не успел: отошёл слишком рано"));
		return;
	}

	if (bPendingHideItem)
	{
		FinishHidingItem(By);
	}
	else
	{
		FinishSearch(By);
	}
}

void AHidingSpot::FinishHidingItem(AObshagaCharacter* By)
{
	UCarryComponent* Carry = By->GetCarryComponent();
	if (!Carry->IsCarrying())
	{
		return;
	}
	if (HiddenItem)
	{
		Notify(By, LOCTEXT("Full", "Здесь уже что-то лежит"));
		return;
	}

	AItemActor* Item = Carry->ReleaseForHiding();
	Item->SetHiddenIn(this, By);
	HiddenItem = Item;
	UGameEventSubsystem::PublishFrom(By, EGameEventType::ItemHidden, Item);
	Notify(By, FText::Format(LOCTEXT("Hid", "Спрятано: {0}"), Item->GetDisplayName()));
	UE_LOG(LogObshaga, Verbose, TEXT("%s hid %s in %s"), *By->GetName(), *Item->GetName(), *GetName());
}

void AHidingSpot::FinishSearch(AObshagaCharacter* By)
{
	if (HiddenPlayer)
	{
		Notify(By, LOCTEXT("FoundPlayer", "Тут кто-то прятался!"));
		UGameEventSubsystem::PublishFrom(By, EGameEventType::PlayerFoundHiding, nullptr, HiddenPlayer);
		EjectHiddenPlayer();
		return;
	}

	if (!HiddenItem)
	{
		Notify(By, LOCTEXT("Empty", "Пусто"));
		return;
	}

	AItemActor* Item = HiddenItem;
	if (By->GetCarryComponent()->PickUp(Item))
	{
		HiddenItem = nullptr;
		UGameEventSubsystem::PublishFrom(By, EGameEventType::ItemFound, Item);
		// Записку игрок сразу читает — её текст важнее, чем «нашёл».
		if (!Item->GetItemData()->bReadable)
		{
			Notify(By, FText::Format(LOCTEXT("Found", "Нашёл: {0}"), Item->GetDisplayName()));
		}
		UE_LOG(LogObshaga, Verbose, TEXT("%s took %s out of %s"), *By->GetName(), *Item->GetName(), *GetName());
	}
}

void AHidingSpot::ResetSpot()
{
	if (HasAuthority())
	{
		GetWorldTimerManager().ClearTimer(ActionTimer);
		PendingBy.Reset();
		bBusy = false;
		HiddenItem = nullptr;
		HiddenPlayer = nullptr;
	}
}

AItemActor* AHidingSpot::KomendantSearch(AObshagaCharacter*& OutFoundPlayer)
{
	OutFoundPlayer = HiddenPlayer;
	if (HiddenPlayer)
	{
		Notify(HiddenPlayer, LOCTEXT("FoundByKomendant", "Комендант нашёл тебя!"));
		EjectHiddenPlayer();
	}

	AItemActor* Contraband = nullptr;
	if (HiddenItem && HiddenItem->GetItemData()->bContraband)
	{
		Contraband = HiddenItem;
		HiddenItem = nullptr;
	}
	return Contraband;
}

void AHidingSpot::EjectHiddenPlayer()
{
	if (AObshagaCharacter* Player = HiddenPlayer)
	{
		HiddenPlayer = nullptr;
		Player->ExitHidingSpot(ExitPoint->GetComponentLocation());
		UGameEventSubsystem::PublishFrom(Player, EGameEventType::PlayerLeftHiding);
		Notify(Player, LOCTEXT("Ejected", "Ты вышел из укрытия"));
		UE_LOG(LogObshaga, Verbose, TEXT("%s left hiding spot %s"), *Player->GetName(), *GetName());
	}
}

#undef LOCTEXT_NAMESPACE
