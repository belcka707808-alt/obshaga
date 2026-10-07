#include "TaskDirector.h"

#include "DeviceActor.h"
#include "ItemActor.h"
#include "NightExitDoor.h"
#include "Obshaga.h"
#include "ObshagaItemData.h"
#include "ObshagaPlayerState.h"
#include "TaskComponent.h"
#include "TaskTypes.h"
#include "Engine/DataTable.h"
#include "EngineUtils.h"

void UTaskDirector::Initialize(UDataTable* InTasksTable)
{
	TasksTable = InTasksTable;
	Reset();
}

void UTaskDirector::Reset()
{
	DealtMainIds.Reset();
	NumDealt = 0;
}

bool UTaskDirector::IsFeasible(const UWorld* World, const FTaskRow& Row)
{
	// Задание раздаётся, только если на карте есть то, без чего его не выполнить.
	switch (Row.SuccessCondition)
	{
	case ETaskCondition::DeviceBrokenAndNotSeen:
	case ETaskCondition::DeviceRepaired:
		return static_cast<bool>(TActorIterator<ADeviceActor>(World));

	case ETaskCondition::LeftBuildingAndReturnedUnseen:
		return static_cast<bool>(TActorIterator<ANightExitDoor>(World));

	case ETaskCondition::NoteReadByDistinctPlayers:
		for (TActorIterator<AItemActor> It(World); It; ++It)
		{
			if (It->GetItemData()->bReadable)
			{
				return true;
			}
		}
		return false;

	default:
		return true;
	}
}

const FTaskRow* UTaskDirector::FindRow(FName TaskId) const
{
	return TasksTable ? TasksTable->FindRow<FTaskRow>(TaskId, TEXT("TaskDirector")) : nullptr;
}

bool UTaskDirector::AreInConflict(FName A, FName B) const
{
	const FTaskRow* RowA = FindRow(A);
	const FTaskRow* RowB = FindRow(B);
	return (RowA && RowA->ConflictsWith.Contains(B)) || (RowB && RowB->ConflictsWith.Contains(A));
}

void UTaskDirector::AssignTasksTo(AObshagaPlayerState* PlayerState, int32 NumPlayers)
{
	if (!TasksTable || !PlayerState)
	{
		UE_LOG(LogObshaga, Warning, TEXT("TaskDirector: no tasks table or player"));
		return;
	}

	// Кандидаты: включённые задания, которые сервер умеет проверять и которым хватает игроков.
	TArray<FName> Candidates;
	for (const FName& RowName : TasksTable->GetRowNames())
	{
		const FTaskRow* Row = FindRow(RowName);
		if (Row && Row->bEnabled && Row->MinPlayers <= NumPlayers && UTaskComponent::IsConditionImplemented(Row->SuccessCondition)
			&& IsFeasible(PlayerState->GetWorld(), *Row))
		{
			Candidates.Add(RowName);
		}
	}

	// Основное: 1) ещё не выданное, которое конфликтует с чьим-то основным; 2) любое не выданное; 3) случайное.
	TArray<FName> Mains = Candidates.FilterByPredicate([this](const FName& Id) { return FindRow(Id)->bCanBeMain; });
	if (Mains.IsEmpty())
	{
		return;
	}

	auto ConflictsWithDealt = [this](const FName& Id)
	{
		return DealtMainIds.ContainsByPredicate([this, Id](const FName& Other) { return AreInConflict(Id, Other); });
	};
	const TArray<FName> FreshConflicting = Mains.FilterByPredicate([&](const FName& Id) { return !DealtMainIds.Contains(Id) && ConflictsWithDealt(Id); });
	TArray<FName> Fresh = Mains.FilterByPredicate([this](const FName& Id) { return !DealtMainIds.Contains(Id); });
	if (DealtMainIds.IsEmpty())
	{
		// Первому игроку — задание, у которого вообще есть противник, чтобы второму досталась конфликтующая пара.
		const TArray<FName> WithRivals = Fresh.FilterByPredicate([this](const FName& Id) { return !FindRow(Id)->ConflictsWith.IsEmpty(); });
		if (!WithRivals.IsEmpty())
		{
			Fresh = WithRivals;
		}
	}
	const TArray<FName>& MainPool = !FreshConflicting.IsEmpty() ? FreshConflicting : (!Fresh.IsEmpty() ? Fresh : Mains);
	FName MainId = MainPool[FMath::RandRange(0, MainPool.Num() - 1)];

	// Для разработки: основные задания по списку, по порядку игроков.
	if (DebugMainTasks.IsValidIndex(NumDealt) && FindRow(DebugMainTasks[NumDealt]))
	{
		MainId = DebugMainTasks[NumDealt];
	}
	++NumDealt;

	PlayerState->GetTaskComponent()->AssignTask(MainId, *FindRow(MainId), true);
	DealtMainIds.Add(MainId);

	// Побочное: любое другое, которое не противоречит собственному основному.
	const TArray<FName> Sides = Candidates.FilterByPredicate([&](const FName& Id) { return Id != MainId && !AreInConflict(Id, MainId); });
	if (!Sides.IsEmpty())
	{
		const FName SideId = Sides[FMath::RandRange(0, Sides.Num() - 1)];
		PlayerState->GetTaskComponent()->AssignTask(SideId, *FindRow(SideId), false);
	}
}
