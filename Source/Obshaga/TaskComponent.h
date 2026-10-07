#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TaskTypes.h"
#include "TaskComponent.generated.h"

class AItemActor;
class AObshagaPlayerState;

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
	/** Обновляет пометку «выполнено прямо сейчас» для телефона. */
	void UpdateLiveStatus();
	/** Конец раунда: фиксирует успех или провал, возвращает заработанные очки. */
	int32 FinalizeTasks();

protected:
	UPROPERTY(Replicated)
	TArray<FTaskState> Tasks;

private:
	AObshagaPlayerState* GetOwnerState() const;
	bool EvaluateCondition(const FTaskRow& Row) const;

	/** Условия заданий — серверная копия, по индексам совпадает с Tasks. */
	TArray<FTaskRow> Rows;
};
