#include "TaskDirector.h"

#include "DeviceActor.h"
#include "ItemActor.h"
#include "NightExitDoor.h"
#include "Obshaga.h"
#include "ObshagaGameState.h"
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

bool UTaskDirector::IsFeasible(const AObshagaPlayerState* PlayerState, const FTaskRow& Row, ERoundPhase CurrentPhase)
{
	const UWorld* World = PlayerState->GetWorld();

	// Фаза задания уже прошла (например, ночная вылазка для вошедшего утром) — не раздаём.
	const UEnum* PhaseEnum = StaticEnum<ERoundPhase>();
	const int64 TaskPhase = Row.Phase.IsNone() ? INDEX_NONE : PhaseEnum->GetValueByNameString(Row.Phase.ToString());
	if (TaskPhase != INDEX_NONE && TaskPhase < static_cast<int64>(CurrentPhase))
	{
		return false;
	}

	// Задание раздаётся, только если на карте есть то, без чего его не выполнить.
	switch (Row.SuccessCondition)
	{
	case ETaskCondition::ItemPlantedInRoomOfPlayer:
	case ETaskCondition::PlayerCaughtWithItem:
	{
		// Подставить можно только того, кто живёт в другой комнате.
		const AGameStateBase* GameState = World->GetGameState();
		return GameState && GameState->PlayerArray.ContainsByPredicate([PlayerState](const APlayerState* Other)
		{
			const AObshagaPlayerState* OtherState = Cast<AObshagaPlayerState>(Other);
			return OtherState && OtherState != PlayerState && !OtherState->GetHomeRoomId().IsNone()
				&& OtherState->GetHomeRoomId() != PlayerState->GetHomeRoomId();
		});
	}

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

void UTaskDirector::AssignTasksTo(AObshagaPlayerState* PlayerState, int32 NumPlayers, ERoundPhase CurrentPhase)
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
			&& IsFeasible(PlayerState, *Row, CurrentPhase))
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
		// Противник должен быть таким, которого реально можно выдать основным заданием.
		const TArray<FName> WithRivals = Fresh.FilterByPredicate([&](const FName& Id)
		{
			return Mains.ContainsByPredicate([&](const FName& Other) { return Other != Id && AreInConflict(Id, Other); });
		});
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
