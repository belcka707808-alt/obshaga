#include "InteractionComponent.h"

#include "Interactable.h"
#include "ObshagaCharacter.h"
#include "ObshagaCharacterConfig.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"

namespace
{
	// Сервер прощает небольшую разницу в позиции из-за лага.
	constexpr float ServerRangeSlack = 75.f;
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
	if (!Controller)
	{
		return nullptr;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);

	const float InteractDistance = Character->GetConfig()->InteractDistance;
	const float CameraToCharacter = FVector::Dist(ViewLocation, Character->GetActorLocation());
	const FVector TraceEnd = ViewLocation + ViewRotation.Vector() * (CameraToCharacter + InteractDistance);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(InteractionTrace), false, Character);
	FHitResult Hit;
	if (!GetWorld()->LineTraceSingleByChannel(Hit, ViewLocation, TraceEnd, ECC_Visibility, Params))
	{
		return nullptr;
	}

	AActor* HitActor = Hit.GetActor();
	const IInteractable* Interactable = Cast<IInteractable>(HitActor);
	if (!Interactable || !IsInRange(HitActor, 0.f) || !Interactable->CanInteract(Character))
	{
		return nullptr;
	}
	return HitActor;
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

void UInteractionComponent::TryInteract()
{
	if (AActor* Target = FocusedActor.Get())
	{
		ServerInteract(Target);
	}
}

void UInteractionComponent::ServerInteract_Implementation(AActor* Target)
{
	// Клиенту не верим: сервер сам перепроверяет объект, дистанцию и условия.
	AObshagaCharacter* Character = GetCharacter();
	IInteractable* Interactable = Cast<IInteractable>(Target);
	if (!Character || !Interactable || !IsInRange(Target, ServerRangeSlack) || !Interactable->CanInteract(Character))
	{
		return;
	}
	Interactable->Interact(Character);
}
