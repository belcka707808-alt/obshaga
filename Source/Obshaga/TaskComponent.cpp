#include "TaskComponent.h"

#include "GameEventSubsystem.h"
#include "ItemActor.h"
#include "Obshaga.h"
#include "ObshagaItemData.h"
#include "ObshagaPlayerState.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

UTaskComponent::UTaskComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UTaskComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Чужие задания по сети не уходят вообще — прочитать их читом нельзя.
	DOREPLIFETIME_CONDITION(UTaskComponent, Tasks, COND_OwnerOnly);
}

AObshagaPlayerState* UTaskComponent::GetOwnerState() const
{
	return Cast<AObshagaPlayerState>(GetOwner());
}

bool UTaskComponent::IsConditionImplemented(ETaskCondition Condition)
{
	switch (Condition)
	{
	case ETaskCondition::ItemInRoomAtEnd:
	case ETaskCondition::ItemNotMovedFromZone:
	case ETaskCondition::ContrabandSurvivedInspection:
	case ETaskCondition::AlibiConfirmedSuccessfully:
	case ETaskCondition::NeverSpottedWholeRound:
	case ETaskCondition::NoiseLuredKomendantAndUnseen:
		return true;
	default:
		return false;
	}
}

bool UTaskComponent::HasTask(FName TaskId) const
{
	return Tasks.ContainsByPredicate([TaskId](const FTaskState& Task) { return Task.TaskId == TaskId; });
}

void UTaskComponent::AssignTask(FName TaskId, const FTaskRow& Row, bool bMain)
{
	if (!GetOwner()->HasAuthority() || HasTask(TaskId))
	{
		return;
	}

	FTaskState& State = Tasks.AddDefaulted_GetRef();
	State.TaskId = TaskId;
	State.Title = Row.Title;
	State.Description = Row.Description;
	State.Reward = Row.GetReward();
	State.bMain = bMain;
	Rows.Add(Row);

	UE_LOG(LogObshaga, Verbose, TEXT("Task %s (%s) assigned to %s"), *TaskId.ToString(), bMain ? TEXT("main") : TEXT("side"), *GetOwnerState()->GetPlayerName());
}

void UTaskComponent::ClearTasks()
{
	if (GetOwner()->HasAuthority())
	{
		Tasks.Reset();
		Rows.Reset();
	}
}

const FTaskState* UTaskComponent::GetMainTask() const
{
	return Tasks.FindByPredicate([](const FTaskState& Task) { return Task.bMain; });
}

void UTaskComponent::UpdateLiveStatus()
{
	for (int32 Index = 0; Index < Tasks.Num(); ++Index)
	{
		if (Tasks[Index].Status == ETaskStatus::InProgress)
		{
			Tasks[Index].bSatisfiedNow = EvaluateCondition(Rows[Index]);
		}
	}
}

int32 UTaskComponent::FinalizeTasks()
{
	int32 Earned = 0;
	for (int32 Index = 0; Index < Tasks.Num(); ++Index)
	{
		FTaskState& State = Tasks[Index];
		if (State.Status != ETaskStatus::InProgress)
		{
			continue;
		}

		State.bSatisfiedNow = EvaluateCondition(Rows[Index]);
		State.Status = State.bSatisfiedNow ? ETaskStatus::Completed : ETaskStatus::Failed;
		if (State.bSatisfiedNow)
		{
			Earned += State.Reward;
		}
		UE_LOG(LogObshaga, Verbose, TEXT("Task %s of %s: %s"), *State.TaskId.ToString(), *GetOwnerState()->GetPlayerName(),
			State.bSatisfiedNow ? TEXT("COMPLETED") : TEXT("FAILED"));
	}
	return Earned;
}

bool UTaskComponent::EvaluateCondition(const FTaskRow& Row) const
{
	const AObshagaPlayerState* OwnerState = GetOwnerState();
	const UWorld* World = GetWorld();
	if (!OwnerState || !World)
	{
		return false;
	}

	// Условия «по журналу» смотрят, что этот игрок делал с начала раунда.
	const UGameEventSubsystem* Bus = UGameEventSubsystem::Get(this);
	static const TArray<FGameEvent> NoEvents;
	const TArray<FGameEvent>& Events = Bus ? Bus->GetEventLog() : NoEvents;
	auto FindOwnEvent = [&Events, OwnerState](EGameEventType Type, float AfterTime = -1.f) -> const FGameEvent*
	{
		return Events.FindByPredicate([=](const FGameEvent& Event) { return Event.Type == Type && Event.Instigator == OwnerState && Event.Time > AfterTime; });
	};

	switch (Row.SuccessCondition)
	{
	case ETaskCondition::ItemInRoomAtEnd:
	case ETaskCondition::ItemNotMovedFromZone:
	{
		// «Предмет в нужной комнате»: лежит там, спрятан там или его там держат в руках.
		const FName TargetRoom = Row.bUseOwnRoom ? OwnerState->GetHomeRoomId() : Row.RoomId;
		if (TargetRoom.IsNone())
		{
			return false;
		}
		for (TActorIterator<AItemActor> It(World); It; ++It)
		{
			if (It->GetItemData()->ItemId == Row.ItemId && It->GetCurrentRoomId() == TargetRoom)
			{
				return true;
			}
		}
		return false;
	}

	case ETaskCondition::ContrabandSurvivedInspection:
	{
		// Запрещёнка лежит в тайнике, и спрятал её именно этот игрок. Если комендант её нашёл — она уже не в тайнике.
		for (TActorIterator<AItemActor> It(World); It; ++It)
		{
			const UObshagaItemData* Data = It->GetItemData();
			const bool bMatches = Row.ItemId.IsNone() ? Data->bContraband : (Data->ItemId == Row.ItemId);
			if (bMatches && It->GetItemState() == EItemState::Hidden && It->GetLastHiddenBy() == OwnerState)
			{
				return true;
			}
		}
		return false;
	}

	case ETaskCondition::AlibiConfirmedSuccessfully:
		return FindOwnEvent(EGameEventType::AlibiConfirmed) != nullptr;

	case ETaskCondition::NeverSpottedWholeRound:
		return FindOwnEvent(EGameEventType::PlayerSpotted) == nullptr;

	case ETaskCondition::NoiseLuredKomendantAndUnseen:
	{
		// Комендант пошёл на шум этого игрока, и после этого игрока не поймали.
		const FGameEvent* Lure = FindOwnEvent(EGameEventType::KomendantAlerted);
		return Lure && FindOwnEvent(EGameEventType::PlayerCaught, Lure->Time) == nullptr;
	}

	default:
		return false;
	}
}
