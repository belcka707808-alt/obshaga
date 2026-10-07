#include "TaskDirector.h"

#include "Obshaga.h"
#include "ObshagaPlayerState.h"
#include "TaskComponent.h"
#include "TaskTypes.h"
#include "Engine/DataTable.h"

void UTaskDirector::Initialize(UDataTable* InTasksTable)
{
	TasksTable = InTasksTable;
	Reset();
}

void UTaskDirector::Reset()
{
	DealtTaskIds.Reset();
}

FName UTaskDirector::AssignTaskTo(AObshagaPlayerState* PlayerState, int32 NumPlayers)
{
	if (!TasksTable || !PlayerState)
	{
		UE_LOG(LogObshaga, Warning, TEXT("TaskDirector: no tasks table or player"));
		return NAME_None;
	}

	// Кандидаты: включённые задания, которые сервер умеет проверять и которым хватает игроков.
	TArray<FName> Candidates;
	for (const FName& RowName : TasksTable->GetRowNames())
	{
		const FTaskRow* Row = TasksTable->FindRow<FTaskRow>(RowName, TEXT("TaskDirector"));
		if (Row && Row->bEnabled && Row->MinPlayers <= NumPlayers && UTaskComponent::IsConditionImplemented(Row->SuccessCondition)
			&& !PlayerState->GetTaskComponent()->HasTask(RowName))
		{
			Candidates.Add(RowName);
		}
	}
	if (Candidates.IsEmpty())
	{
		return NAME_None;
	}

	auto ConflictsWithDealt = [this](const FName& RowName)
	{
		const FTaskRow* Row = TasksTable->FindRow<FTaskRow>(RowName, TEXT("TaskDirector"));
		return Row && Row->ConflictsWith.ContainsByPredicate([this](const FName& Other) { return DealtTaskIds.Contains(Other); });
	};

	// 1) ещё не выданное задание, которое конфликтует с чьим-то; 2) любое не выданное; 3) что угодно.
	const FName* Chosen = Candidates.FindByPredicate([&](const FName& RowName) { return !DealtTaskIds.Contains(RowName) && ConflictsWithDealt(RowName); });
	if (!Chosen)
	{
		Chosen = Candidates.FindByPredicate([this](const FName& RowName) { return !DealtTaskIds.Contains(RowName); });
	}
	const FName TaskId = Chosen ? *Chosen : Candidates[FMath::RandRange(0, Candidates.Num() - 1)];

	PlayerState->GetTaskComponent()->AssignTask(TaskId, *TasksTable->FindRow<FTaskRow>(TaskId, TEXT("TaskDirector")));
	DealtTaskIds.AddUnique(TaskId);
	return TaskId;
}
