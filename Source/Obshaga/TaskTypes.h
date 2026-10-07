#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "TaskTypes.generated.h"

/** Как проверяется задание. Список — из ТЗ; пока реализованы не все (см. UTaskComponent::IsConditionImplemented). */
UENUM(BlueprintType)
enum class ETaskCondition : uint8
{
	None,
	ItemInRoomAtEnd,
	ItemNotMovedFromZone,
	ItemPlantedInRoomOfPlayer,
	PlayerCaughtWithItem,
	LeftBuildingAndReturnedUnseen,
	AlibiConfirmedSuccessfully,
	ContrabandSurvivedInspection,
	InspectionFoundContrabandInRoom,
	NoteReadByDistinctPlayers,
	DeviceBrokenAndNotSeen,
	DeviceRepaired,
	NoiseLuredKomendantAndUnseen,
	NeverSpottedWholeRound,
	CorrectAccusation
};

UENUM(BlueprintType)
enum class ETaskStatus : uint8
{
	InProgress,
	Completed,
	Failed
};

/** Строка таблицы DT_Tasks: одно секретное задание, описанное данными. Имя строки — id задания. */
USTRUCT(BlueprintType)
struct FTaskRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Task")
	FText Title;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Task")
	FText Description;

	/** Выключенные задания не раздаются. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Task")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Task")
	FName Category;

	/** Фаза, в которой задание активно (Evening / Night / Morning); пусто — весь раунд. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Task")
	FName Phase;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Task", meta = (ClampMin = "1"))
	int32 MinPlayers = 2;

	/** Очки за задание = Difficulty × BaseReward. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reward", meta = (ClampMin = "1"))
	int32 Difficulty = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reward", meta = (ClampMin = "0"))
	int32 BaseReward = 10;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Condition")
	ETaskCondition SuccessCondition = ETaskCondition::None;

	/** Id предмета из его паспорта (UObshagaItemData::ItemId), если условие про предмет. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Condition")
	FName ItemId;

	/** Id комнаты (ARoomVolume::RoomId), если условие про комнату. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Condition")
	FName RoomId;

	/** Вместо RoomId использовать домашнюю комнату игрока. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Condition")
	bool bUseOwnRoom = false;

	/** Число для условий со счётчиком (например, сколько игроков должны прочитать записку). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Condition", meta = (ClampMin = "0"))
	int32 Count = 0;

	/** Задания, которые противоречат этому. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Relations")
	TArray<FName> ConflictsWith;

	/** Задания, которым это помогает. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Relations")
	TArray<FName> Boosts;

	int32 GetReward() const { return Difficulty * BaseReward; }
};

/** То, что о задании знает клиент-владелец: текст и состояние. Условия остаются на сервере. */
USTRUCT(BlueprintType)
struct FTaskState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FName TaskId;

	UPROPERTY(BlueprintReadOnly)
	FText Title;

	UPROPERTY(BlueprintReadOnly)
	FText Description;

	UPROPERTY(BlueprintReadOnly)
	int32 Reward = 0;

	UPROPERTY(BlueprintReadOnly)
	ETaskStatus Status = ETaskStatus::InProgress;

	/** Условие выполнено прямо сейчас (но раунд ещё идёт, и всё может измениться). */
	UPROPERTY(BlueprintReadOnly)
	bool bSatisfiedNow = false;
};

/** Строка экрана итогов: чьё задание и чем кончилось. Раскрывается всем после раунда. */
USTRUCT(BlueprintType)
struct FRevealedTask
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FString PlayerName;

	UPROPERTY(BlueprintReadOnly)
	FText TaskTitle;

	UPROPERTY(BlueprintReadOnly)
	ETaskStatus Status = ETaskStatus::Failed;

	UPROPERTY(BlueprintReadOnly)
	int32 Reward = 0;
};
