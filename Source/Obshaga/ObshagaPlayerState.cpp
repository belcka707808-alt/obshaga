#include "ObshagaPlayerState.h"

#include "SuspicionComponent.h"
#include "TaskComponent.h"
#include "Net/UnrealNetwork.h"

AObshagaPlayerState::AObshagaPlayerState()
{
	TaskComponent = CreateDefaultSubobject<UTaskComponent>(TEXT("TaskComponent"));
	SuspicionComponent = CreateDefaultSubobject<USuspicionComponent>(TEXT("SuspicionComponent"));

	// По умолчанию PlayerState обновляется раз в секунду — задания и СМС приходили бы с заметным опозданием.
	SetNetUpdateFrequency(10.f);
}

void AObshagaPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AObshagaPlayerState, HomeRoomId);
	DOREPLIFETIME(AObshagaPlayerState, bEvicted);

	// Секреты — только владельцу.
	DOREPLIFETIME_CONDITION(AObshagaPlayerState, VisibleRole, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AObshagaPlayerState, RatIntel, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AObshagaPlayerState, RatTarget, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AObshagaPlayerState, SmsMessages, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AObshagaPlayerState, bUsedTip, COND_OwnerOnly);
	// По этим флагам можно угадать задание — тоже только владельцу.
	DOREPLIFETIME_CONDITION(AObshagaPlayerState, bCanAccuse, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AObshagaPlayerState, bCanTipRoom, COND_OwnerOnly);
}

void AObshagaPlayerState::SetTaskAbilities(bool bNewCanAccuse, bool bNewCanTipRoom)
{
	if (HasAuthority())
	{
		bCanAccuse = bNewCanAccuse;
		bCanTipRoom = bNewCanTipRoom;
		ForceNetUpdate();
	}
}

void AObshagaPlayerState::SetHomeRoomId(FName NewRoomId)
{
	if (HasAuthority())
	{
		HomeRoomId = NewRoomId;
		ForceNetUpdate();
	}
}

void AObshagaPlayerState::SetRole(EPlayerRole NewRole)
{
	if (HasAuthority())
	{
		TrueRole = NewRole;
		// Параноик о своей роли не знает: для него самого он обычный жилец.
		VisibleRole = NewRole == EPlayerRole::Paranoid ? EPlayerRole::Resident : NewRole;
		ForceNetUpdate();
	}
}

void AObshagaPlayerState::SetRatIntel(const FText& Intel, APlayerState* Target)
{
	if (HasAuthority())
	{
		RatIntel = Intel;
		RatTarget = Target;
		ForceNetUpdate();
	}
}

void AObshagaPlayerState::AddSms(const FText& Message)
{
	if (HasAuthority())
	{
		SmsMessages.Add(Message);
		ForceNetUpdate();
	}
}

void AObshagaPlayerState::SetEvicted(bool bNewEvicted)
{
	if (HasAuthority())
	{
		bEvicted = bNewEvicted;
		ForceNetUpdate();
	}
}

void AObshagaPlayerState::MarkTipUsed()
{
	if (HasAuthority())
	{
		bUsedTip = true;
		ForceNetUpdate();
	}
}

int32 AObshagaPlayerState::TakePendingRatBonus()
{
	const int32 Bonus = PendingRatBonus;
	PendingRatBonus = 0;
	return Bonus;
}

void AObshagaPlayerState::ResetForRound()
{
	if (!HasAuthority())
	{
		return;
	}

	SetRole(EPlayerRole::Resident);
	RatIntel = FText::GetEmpty();
	RatTarget = nullptr;
	PendingRatBonus = 0;
	SmsMessages.Reset();
	bUsedTip = false;
	bCanAccuse = false;
	bCanTipRoom = false;
	bEvicted = false;
	TaskComponent->ClearTasks();
	SuspicionComponent->ResetAll();
	ForceNetUpdate();
}
