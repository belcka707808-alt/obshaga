#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "TaskDirector.generated.h"

class AObshagaPlayerState;
class UDataTable;
struct FTaskRow;

/**
 * «Раздающий» заданий. Живёт на сервере внутри GameMode.
 * Каждому — основное и побочное задание. Основные по возможности не повторяются и конфликтуют
 * друг с другом; побочное не противоречит собственному основному.
 */
UCLASS()
class OBSHAGA_API UTaskDirector : public UObject
{
	GENERATED_BODY()

public:
	void Initialize(UDataTable* InTasksTable);

	/** Новый раунд: забыть, что было роздано. */
	void Reset();

	/** Выдаёт игроку основное и побочное задания. */
	void AssignTasksTo(AObshagaPlayerState* PlayerState, int32 NumPlayers);

private:
	UPROPERTY()
	TObjectPtr<UDataTable> TasksTable;

	const FTaskRow* FindRow(FName TaskId) const;
	bool AreInConflict(FName A, FName B) const;

	TArray<FName> DealtMainIds;
};
