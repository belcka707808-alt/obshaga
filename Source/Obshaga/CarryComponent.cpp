#include "CarryComponent.h"

#include "GameEventSubsystem.h"
#include "ItemActor.h"
#include "Obshaga.h"
#include "ObshagaCharacter.h"
#include "ObshagaCharacterConfig.h"
#include "ObshagaItemData.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

namespace
{
	// Кладём предмет: как глубоко под точкой ищем пол и какой пол считаем ровным (не пандус лестницы).
	constexpr float MaxFloorCheckDepth = 200.f;
	constexpr float FlatFloorNormalZ = 0.95f;
}

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
	if (!Character || !Character->HasAuthority() || !Item || CarriedItem || Character->IsHiding() || Character->IsGhost()
		|| Item->GetItemState() == EItemState::Carried)
	{
		return false;
	}

	Item->SetCarriedBy(Character);
	SetCarriedItem(Item);
	UE_LOG(LogObshaga, Verbose, TEXT("%s picked up %s"), *Character->GetName(), *Item->GetName());
	UGameEventSubsystem::PublishFrom(Character, EGameEventType::ItemPickedUp, Item);
	// Взял записку в руки — заодно и прочитал.
	Item->ReadBy(Character);
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
	// Ставим ровно и широкой стороной к камере положившего (от взгляда, а не от тела): шагнул вбок и положил —
	// иначе телевизор встал бы к нему торцом, тонкой палкой.
	const FRotator Rotation = FindReleaseRotation(Item, Location);
	SetCarriedItem(nullptr);
	Item->ReleaseToWorld(Location, FVector::ZeroVector, Rotation);
	UE_LOG(LogObshaga, Verbose, TEXT("%s dropped %s at %s, yaw %.0f"), *Character->GetName(), *Item->GetName(), *Location.ToCompactString(), Rotation.Yaw);
	UGameEventSubsystem::PublishFrom(Character, EGameEventType::ItemDropped, Item);
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

	const bool bHeavy = Item->GetItemData()->bHeavy;
	// Летит широкой стороной вперёд: плоское падает плашмя или встаёт лицом к бросившему, но не торцом.
	const FRotator Rotation = FindReleaseRotation(Item, Location);
	SetCarriedItem(nullptr);
	Item->ReleaseToWorld(Location, Velocity, Rotation);
	if (bHeavy)
	{
		// Швырнул телевизор — сам не удержался на ногах.
		Character->Stumble(Cfg->HeavyThrowStumbleSeconds);
	}
	UE_LOG(LogObshaga, Verbose, TEXT("%s threw %s%s"), *Character->GetName(), *Item->GetName(), bHeavy ? TEXT(" and stumbled") : TEXT(""));
	UGameEventSubsystem::PublishFrom(Character, EGameEventType::ItemThrown, Item);
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

FRotator UCarryComponent::FindReleaseRotation(const AItemActor* Item, const FVector& Location) const
{
	const AObshagaCharacter* Character = GetCharacter();
	FRotator Rotation(0.f, Character->GetBaseAimRotation().Yaw + Item->GetCarryYaw(), 0.f);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(ItemReleaseFit), false, Character);
	Params.AddIgnoredActor(Item);
	FHitResult Floor;
	const bool bSlope = GetWorld()->LineTraceSingleByChannel(Floor, Location, Location - FVector(0.f, 0.f, MaxFloorCheckDepth), ECC_Visibility, Params)
		&& Floor.ImpactNormal.Z < FlatFloorNormalZ;
	const FCollisionShape Box = FCollisionShape::MakeBox(Item->GetItemData()->BoxSize * 0.5f);
	if (bSlope || GetWorld()->OverlapBlockingTestByChannel(Location, Rotation.Quaternion(), ECC_PhysicsBody, Box, Params))
	{
		// На пандусе лестницы поперёк склона телевизор опрокинулся бы и с грохотом покатился вниз, у стены — встал бы в неё.
		Rotation.Yaw += 90.f;
	}
	return Rotation;
}
