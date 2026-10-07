#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "TaskDirector.generated.h"

class AObshagaPlayerState;
class UDataTable;

/**
 * «Раздающий» заданий. Живёт на сервере внутри GameMode.
 * Версия M3 простая: старается выдать задание, которое конфликтует с уже выданным,
 * иначе — первое свободное. Умный подбор (доля конфликтов, анти-повтор, веса) — на M5.
 */
UCLASS()
class OBSHAGA_API UTaskDirector : public UObject
{
	GENERATED_BODY()

public:
	void Initialize(UDataTable* InTasksTable);

	/** Новый раунд: забыть, что было роздано. */
	void Reset();

	/** Выдаёт игроку одно задание. Возвращает id задания или NAME_None, если выдать нечего. */
	FName AssignTaskTo(AObshagaPlayerState* PlayerState, int32 NumPlayers);

private:
	UPROPERTY()
	TObjectPtr<UDataTable> TasksTable;

	TArray<FName> DealtTaskIds;
};
