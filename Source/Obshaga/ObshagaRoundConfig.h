#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ObshagaRoundConfig.generated.h"

class UDataTable;

/** Настройки раунда. Правятся в ассете DA_RoundConfig. Фазы «вечер / ночь / утро» добавятся на M5. */
UCLASS(BlueprintType)
class OBSHAGA_API UObshagaRoundConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Сколько длится раунд. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Round", meta = (ClampMin = "5", Units = "s"))
	float RoundSeconds = 300.f;

	/** Пауза между запуском карты и началом раунда, чтобы все успели войти. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Round", meta = (ClampMin = "0", Units = "s"))
	float StartDelaySeconds = 5.f;

	/** Таблица секретных заданий (строки FTaskRow). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tasks")
	TObjectPtr<UDataTable> TasksTable;
};
