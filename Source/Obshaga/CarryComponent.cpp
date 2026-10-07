#include "CarryComponent.h"

#include "ItemActor.h"
#include "Obshaga.h"
#include "ObshagaCharacter.h"
#include "ObshagaCharacterConfig.h"
#include "ObshagaItemData.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

UCarryComponent::UCarryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UCarryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UCarryComponent, CarriedItem);
}

AObshagaCharacter* UCarryComponent::GetCharacter() const
{
	return Cast<AObshagaCharacter>(GetOwner());
}

bool UCarryComponent::IsCarryingHeavy() const
{
	return CarriedItem && CarriedItem->IsHeavy();
}

void UCarryComponent::SetCarriedItem(AItemActor* NewItem)
{
	CarriedItem = NewItem;
	OnRep_CarriedItem();
}

void UCarryComponent::OnRep_CarriedItem()
{
	if (AObshagaCharacter* Character = GetCharacter())
	{
		Character->OnCarriedItemChanged();
	}
}

void UCarryComponent::TryDrop()
{
	if (CarriedItem)
	{
		ServerDrop();
	}
}

void UCarryComponent::TryThrow()
{
	if (CarriedItem)
	{
		ServerThrow();
	}
}

void UCarryComponent::ServerDrop_Implementation()
{
	Drop();
}

void UCarryComponent::ServerThrow_Implementation()
{
	Throw();
}

bool UCarryComponent::PickUp(AItemActor* Item)
{
	AObshagaCharacter* Character = GetCharacter();
	if (!Character || !Character->HasAuthority() || !Item || CarriedItem || Character->IsHiding()
		|| Item->GetItemState() == EItemState::Carried)
	{
		return false;
	}

	Item->SetCarriedBy(Character);
	SetCarriedItem(Item);
	UE_LOG(LogObshaga, Verbose, TEXT("%s picked up %s"), *Character->GetName(), *Item->GetName());
	return true;
}

void UCarryComponent::Drop()
{
	AObshagaCharacter* Character = GetCharacter();
	if (!Character || !Character->HasAuthority() || !CarriedItem)
	{
		return;
	}

	AItemActor* Item = CarriedItem;
	const UObshagaCharacterConfig* Cfg = Character->GetConfig();
	const FVector Location = FindReleaseLocation(Character->GetActorForwardVector(), Cfg->DropDistance, 0.f);
	SetCarriedItem(nullptr);
	Item->ReleaseToWorld(Location, FVector::ZeroVector);
	UE_LOG(LogObshaga, Verbose, TEXT("%s dropped %s"), *Character->GetName(), *Item->GetName());
}

void UCarryComponent::Throw()
{
	AObshagaCharacter* Character = GetCharacter();
	if (!Character || !Character->HasAuthority() || !CarriedItem)
	{
		return;
	}

	AItemActor* Item = CarriedItem;
	const UObshagaCharacterConfig* Cfg = Character->GetConfig();

	// Бросаем туда, куда смотрит игрок, слегка вверх.
	const FVector AimDirection = Character->GetBaseAimRotation().Vector();
	const FVector ThrowDirection = (AimDirection + FVector::UpVector * Cfg->ThrowUpBias).GetSafeNormal();
	const FVector Velocity = ThrowDirection * Item->GetItemData()->ThrowSpeed + Character->GetVelocity();

	const FVector FlatAim = FVector(AimDirection.X, AimDirection.Y, 0.f).GetSafeNormal();
	const FVector Location = FindReleaseLocation(FlatAim, Cfg->DropDistance, Cfg->ThrowHeight);

	SetCarriedItem(nullptr);
	Item->ReleaseToWorld(Location, Velocity);
	UE_LOG(LogObshaga, Verbose, TEXT("%s threw %s"), *Character->GetName(), *Item->GetName());
}

AItemActor* UCarryComponent::ReleaseForHiding()
{
	AItemActor* Item = CarriedItem;
	if (Item)
	{
		SetCarriedItem(nullptr);
	}
	return Item;
}

FVector UCarryComponent::FindReleaseLocation(const FVector& Direction, float Distance, float Height) const
{
	const AObshagaCharacter* Character = GetCharacter();
	const FVector Start = Character->GetActorLocation() + FVector(0.f, 0.f, Height);
	const FVector End = Start + Direction * Distance;

	// Если перед игроком стена — предмет остаётся с его стороны стены.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ItemRelease), false, Character);
	Params.AddIgnoredActor(CarriedItem);
	FHitResult Hit;
	const FCollisionShape Probe = FCollisionShape::MakeSphere(Character->GetConfig()->ReleaseProbeRadius);
	if (GetWorld()->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, ECC_Visibility, Probe, Params))
	{
		return Hit.Location;
	}
	return End;
}
