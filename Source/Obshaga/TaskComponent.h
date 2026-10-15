#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TaskTypes.h"
#include "TaskComponent.generated.h"

class AItemActor;
class AObshagaPlayerState;

/** Сервер: что владелец уже узнал о задании из сообщений «сейчас выполнено / больше не выполнено». По сети не ходит. */
struct FTaskLiveNotice
{
	/** Первая проверка после выдачи прошла: её итог — точка отсчёта, о нём не сообщаем. */
	bool bKnown = false;
	/** Какое состояние игрок знает из последнего сообщения. */
	bool bAnnounced = false;
	/** Когда ушло последнее сообщение про это задание. */
	float Time = -100.f;
};

/**
 * «Карман с заданиями» игрока. Висит на PlayerState.
 * Условия проверяет только сервер; владельцу реплицируются текст и состояние, остальным — ничего.
 */
UCLASS(ClassGroup = (Obshaga))
class OBSHAGA_API UTaskComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTaskComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	const TArray<FTaskState>& GetTasks() const { return Tasks; }
	/** Сервер: условия заданий (по индексам совпадают с GetTasks). Нужны мозгу бота. */
	const TArray<FTaskRow>& GetRows() const { return Rows; }

	/** Умеет ли сервер уже проверять такое условие. Нереализованные задания не раздаются. */
	static bool IsConditionImplemented(ETaskCondition Condition);

	// Только сервер.
	void AssignTask(FName TaskId, const FTaskRow& Row, bool bMain);
	void ClearTasks();
	const FTaskState* GetMainTask() const;
	bool HasTask(FName TaskId) const;
	/** Строка задания игрока с таким условием; nullptr, если такого задания у него нет. */
	const FTaskRow* FindRowByCondition(ETaskCondition Condition) const;
	/** Предмет задания, который игрок вынес из его «родной» комнаты; nullptr, если он ничего не крал. */
	static AItemActor* FindStolenItem(const UObject* WorldContextObject, const APlayerState* Who, const FTaskRow& Row);
	/** Задание про записку со слухом (в строке указан предмет, который можно прочитать)? Тогда ищут не вора, а автора слуха. */
	static bool IsRumorRow(const UObject* WorldContextObject, const FTaskRow& Row);
	/** Записка, которую подкинул Who, если слух — его задание; nullptr — он не автор слуха. */
	static AItemActor* FindRumorNote(const UObject* WorldContextObject, const APlayerState* Who);
	/** Обновляет пометку «выполнено прямо сейчас» для телефона и сообщает владельцу, если она поменялась. */
	void UpdateLiveStatus();
	/** Конец раунда: фиксирует успех или провал, возвращает заработанные очки. */
	int32 FinalizeTasks();

protected:
	UPROPERTY(Replicated)
	TArray<FTaskState> Tasks;

private:
	AObshagaPlayerState* GetOwnerState() const;
	bool EvaluateCondition(const FTaskRow& Row) const;
	/** Сверяет пометку «выполнено сейчас» задания Index с тем, что знает владелец; поменялась — сообщение ему (не чаще раза в несколько секунд). */
	void NotifyLiveChange(int32 Index);

	/** Условия заданий — серверная копия, по индексам совпадает с Tasks. */
	TArray<FTaskRow> Rows;

	/** Что владелец знает о живом состоянии заданий; по индексам совпадает с Tasks. */
	TArray<FTaskLiveNotice> LiveNotices;
};
