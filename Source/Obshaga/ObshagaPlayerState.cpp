#include "ObshagaPlayerState.h"

#include "SuspicionComponent.h"
#include "TaskComponent.h"
#include "Net/UnrealNetwork.h"

AObshagaPlayerState::AObshagaPlayerState()
{
	TaskComponent = CreateDefaultSubobject<UTaskComponent>(TEXT("TaskComponent"));
	SuspicionComponent = CreateDefaultSubobject<USuspicionComponent>(TEXT("SuspicionComponent"));
}

void AObshagaPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AObshagaPlayerState, HomeRoomId);
	DOREPLIFETIME(AObshagaPlayerState, bEvicted);

	// Секреты — только владельцу.
	DOREPLIFETIME_CONDITION(AObshagaPlayerState, VisibleRole, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AObshagaPlayerState, RatIntel, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AObshagaPlayerState, SmsMessages, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AObshagaPlayerState, bUsedTip, COND_OwnerOnly);
}

void AObshagaPlayerState::SetHomeRoomId(FName NewRoomId)
{
	if (HasAuthority())
	{
		HomeRoomId = NewRoomId;
	}
}

void AObshagaPlayerState::SetRole(EPlayerRole NewRole)
{
	if (HasAuthority())
	{
		TrueRole = NewRole;
		// Параноик о своей роли не знает: для него самого он обычный жилец.
		VisibleRole = NewRole == EPlayerRole::Paranoid ? EPlayerRole::Resident : NewRole;
	}
}

void AObshagaPlayerState::SetRatIntel(const FText& Intel, APlayerState* Target)
{
	if (HasAuthority())
	{
		RatIntel = Intel;
		RatTarget = Target;
	}
}

void AObshagaPlayerState::AddSms(const FText& Message)
{
	if (HasAuthority())
	{
		SmsMessages.Add(Message);
	}
}

void AObshagaPlayerState::SetEvicted(bool bNewEvicted)
{
	if (HasAuthority())
	{
		bEvicted = bNewEvicted;
	}
}

void AObshagaPlayerState::MarkTipUsed()
{
	if (HasAuthority())
	{
		bUsedTip = true;
	}
}

void AObshagaPlayerState::ResetForRound()
{
	if (!HasAuthority())
	{
		return;
	}

	SetRole(EPlayerRole::Resident);
	RatIntel = FText::GetEmpty();
	RatTarget.Reset();
	SmsMessages.Reset();
	bUsedTip = false;
	bEvicted = false;
	TaskComponent->ClearTasks();
	SuspicionComponent->ResetAll();
}
