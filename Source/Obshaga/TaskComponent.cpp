#include "TaskComponent.h"

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
		return true;
	default:
		return false;
	}
}

bool UTaskComponent::HasTask(FName TaskId) const
{
	return Tasks.ContainsByPredicate([TaskId](const FTaskState& Task) { return Task.TaskId == TaskId; });
}

void UTaskComponent::AssignTask(FName TaskId, const FTaskRow& Row)
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
	Rows.Add(Row);

	UE_LOG(LogObshaga, Verbose, TEXT("Task %s assigned to %s"), *TaskId.ToString(), *GetOwnerState()->GetPlayerName());
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
		// Пока коменданта нет: запрещёнка лежит в тайнике, и спрятал её именно этот игрок.
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

	default:
		return false;
	}
}
