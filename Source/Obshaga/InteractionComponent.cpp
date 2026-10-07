#include "InteractionComponent.h"

#include "HidingSpot.h"
#include "Interactable.h"
#include "ItemActor.h"
#include "ObshagaCharacter.h"
#include "ObshagaCharacterConfig.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"

namespace
{
	// Подбор предмета у ног: насколько вниз надо смотреть (Z направления взгляда), размер и сдвиг области поиска.
	constexpr float FeetLookDownZ = -0.4f;
	constexpr float FeetRadius = 80.f;
	constexpr float FeetForwardOffset = 40.f;
}

UInteractionComponent::UInteractionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.05f;
}

AObshagaCharacter* UInteractionComponent::GetCharacter() const
{
	return Cast<AObshagaCharacter>(GetOwner());
}

void UInteractionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Подсказка нужна только тому, кто управляет этим персонажем.
	const AObshagaCharacter* Character = GetCharacter();
	if (Character && Character->IsLocallyControlled())
	{
		FocusedActor = FindFocusedActor();
	}
}

AActor* UInteractionComponent::FindFocusedActor() const
{
	const AObshagaCharacter* Character = GetCharacter();
	const AController* Controller = Character ? Character->GetController() : nullptr;
	if (!Controller || Character->IsGhost() || Character->IsFrozen())
	{
		return nullptr;
	}

	// Из укрытия доступно только само укрытие: выйти.
	if (AHidingSpot* Spot = Character->GetHidingSpot())
	{
		return Spot;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);

	const float InteractDistance = Character->GetConfig()->InteractDistance;
	const float CameraToCharacter = FVector::Dist(ViewLocation, Character->GetActorLocation());
	const FVector TraceEnd = ViewLocation + ViewRotation.Vector() * (CameraToCharacter + InteractDistance);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(InteractionTrace), false, Character);
	FHitResult Hit;
	const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, ViewLocation, TraceEnd, ECC_Visibility, Params);

	// Помощь в прицеливании: в мелкий предмет трудно попасть лучом. Поэтому из всего, что лежит
	// рядом с точкой взгляда, выбираем самый маленький объект: ноутбук на тумбочке важнее самой тумбочки.
	AActor* Best = nullptr;
	double BestSizeSquared = TNumericLimits<double>::Max();
	auto Consider = [this, Character, &Best, &BestSizeSquared](AActor* Candidate, bool bCheckSight)
	{
		const IInteractable* Interactable = Cast<IInteractable>(Candidate);
		if (Candidate == Best || !Interactable || !IsInRange(Candidate, 0.f) || (bCheckSight && !HasLineOfSight(Candidate)))
		{
			return;
		}

		// Объект, с которым сейчас нечего делать, не должен заслонять соседний.
		if (Interactable->GetInteractionPrompt(Character).IsEmpty() && Interactable->GetSecondaryPrompt(Character).IsEmpty())
		{
			return;
		}

		const double SizeSquared = Candidate->GetComponentsBoundingBox().GetSize().SizeSquared();
		if (SizeSquared < BestSizeSquared)
		{
			BestSizeSquared = SizeSquared;
			Best = Candidate;
		}
	};

	if (bHit)
	{
		Consider(Hit.GetActor(), false);
	}

	const FVector AimPoint = bHit ? Hit.ImpactPoint : TraceEnd;
	TArray<FOverlapResult> Overlaps;
	const FCollisionShape AssistSphere = FCollisionShape::MakeSphere(Character->GetConfig()->AimAssistRadius);
	GetWorld()->OverlapMultiByChannel(Overlaps, AimPoint, FQuat::Identity, ECC_Visibility, AssistSphere, Params);
	for (const FOverlapResult& Overlap : Overlaps)
	{
		Consider(Overlap.GetActor(), true);
	}

	// Предмет у самых ног лучом не поймать: взгляд упирается в пол далеко впереди. Поэтому, если игрок
	// смотрит вниз и ничего не нашёл, ищем предметы прямо под ним, чуть впереди по взгляду.
	const FVector ViewDirection = ViewRotation.Vector();
	if (!Best && ViewDirection.Z < FeetLookDownZ)
	{
		const float HalfHeight = Character->GetSimpleCollisionHalfHeight();
		const FVector Forward = FVector(ViewDirection.X, ViewDirection.Y, 0.f).GetSafeNormal();
		const FVector FeetPoint = Character->GetActorLocation() - FVector(0.f, 0.f, HalfHeight) + Forward * FeetForwardOffset;

		Overlaps.Reset();
		GetWorld()->OverlapMultiByChannel(Overlaps, FeetPoint, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(FeetRadius), Params);
		for (const FOverlapResult& Overlap : Overlaps)
		{
			if (Cast<AItemActor>(Overlap.GetActor()))
			{
				Consider(Overlap.GetActor(), true);
			}
		}
	}
	return Best;
}

bool UInteractionComponent::HasLineOfSight(const AActor* Target) const
{
	const AObshagaCharacter* Character = GetCharacter();
	if (!Character || !Target)
	{
		return false;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(InteractionSight), false, Character);
	Params.AddIgnoredActor(Target);
	// Целимся в ближайшую к глазам точку объекта: у открытой двери центр может оказаться за косяком.
	const FVector EyeLocation = Character->GetPawnViewLocation();
	const FVector TargetPoint = Target->GetComponentsBoundingBox().GetClosestPointTo(EyeLocation);
	FHitResult Hit;
	return !GetWorld()->LineTraceSingleByChannel(Hit, EyeLocation, TargetPoint, ECC_Visibility, Params);
}

bool UInteractionComponent::IsInRange(const AActor* Target, float Slack) const
{
	const AObshagaCharacter* Character = GetCharacter();
	if (!Character || !Target)
	{
		return false;
	}

	const FBox Bounds = Target->GetComponentsBoundingBox();
	const float MaxDistance = Character->GetConfig()->InteractDistance + Slack;
	return Bounds.ComputeSquaredDistanceToPoint(Character->GetActorLocation()) <= FMath::Square(MaxDistance);
}

FText UInteractionComponent::GetFocusedPrompt() const
{
	const IInteractable* Interactable = Cast<IInteractable>(FocusedActor.Get());
	return Interactable ? Interactable->GetInteractionPrompt(GetCharacter()) : FText::GetEmpty();
}

FText UInteractionComponent::GetFocusedSecondaryPrompt() const
{
	const IInteractable* Interactable = Cast<IInteractable>(FocusedActor.Get());
	return Interactable ? Interactable->GetSecondaryPrompt(GetCharacter()) : FText::GetEmpty();
}

void UInteractionComponent::TryInteract(bool bSecondary)
{
	if (AActor* Target = FocusedActor.Get())
	{
		ServerInteract(Target, bSecondary);
	}
}

void UInteractionComponent::ServerInteract_Implementation(AActor* Target, bool bSecondary)
{
	// Клиенту не верим: сервер сам перепроверяет объект и дистанцию, а условия действия проверяет сам объект.
	AObshagaCharacter* Character = GetCharacter();
	IInteractable* Interactable = Cast<IInteractable>(Target);
	if (!Character || !Interactable || Character->IsGhost() || Character->IsFrozen())
	{
		return;
	}

	const AHidingSpot* CurrentSpot = Character->GetHidingSpot();
	// Сквозь стену взаимодействовать нельзя, даже если клиент уверяет, что можно.
	const bool bAllowed = CurrentSpot ? (Target == CurrentSpot) : (IsInRange(Target, ServerRangeSlack) && HasLineOfSight(Target));
	if (!bAllowed)
	{
		return;
	}

	if (bSecondary)
	{
		Interactable->SecondaryInteract(Character);
	}
	else
	{
		Interactable->Interact(Character);
	}
}
