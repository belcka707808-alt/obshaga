#include "SuspicionComponent.h"

#include "Net/UnrealNetwork.h"

USuspicionComponent::USuspicionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void USuspicionComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(USuspicionComponent, SuspicionPercent, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(USuspicionComponent, Strikes, COND_OwnerOnly);
}

void USuspicionComponent::AddSuspicion(float Delta)
{
	SetSuspicion(Suspicion + Delta);
}

void USuspicionComponent::SetSuspicion(float NewValue)
{
	if (GetOwner()->HasAuthority())
	{
		Suspicion = FMath::Clamp(NewValue, 0.f, 100.f);
		SuspicionPercent = static_cast<uint8>(FMath::RoundToInt32(Suspicion));
	}
}

void USuspicionComponent::ResetAll()
{
	if (GetOwner()->HasAuthority())
	{
		Strikes = 0;
		SetSuspicion(0.f);
	}
}

void USuspicionComponent::AddStrike()
{
	if (GetOwner()->HasAuthority() && Strikes < 255)
	{
		++Strikes;
	}
}
