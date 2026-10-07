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

	// --- Подозрение ---

	/** Рост подозрения в секунду, пока комендант видит игрока с обычным предметом в руках. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Suspicion", meta = (ClampMin = "0"))
	float CarrySuspicionPerSecond = 12.f;

	/** То же для тяжёлого предмета (телевизор). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Suspicion", meta = (ClampMin = "0"))
	float HeavyCarrySuspicionPerSecond = 30.f;

	/** То же для запрещёнки: фактически мгновенно. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Suspicion", meta = (ClampMin = "0"))
	float ContrabandSuspicionPerSecond = 400.f;

	/** Спад подозрения в секунду, пока комендант игрока не видит. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Suspicion", meta = (ClampMin = "0"))
	float SuspicionDecayPerSecond = 1.5f;

	/** Сколько подозрения получает тот, чью запрещёнку комендант нашёл в тайнике. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Suspicion", meta = (ClampMin = "0"))
	float HiddenContrabandSuspicion = 60.f;

	/** Подозрение после допроса или удачного побега. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Suspicion", meta = (ClampMin = "0", ClampMax = "100"))
	float SuspicionAfterStrike = 40.f;

	// --- Комендант ---

	/** Шум тише этого комендант не идёт проверять. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Komendant", meta = (ClampMin = "0", ClampMax = "1"))
	float KomendantMinNoise = 0.3f;

	/** С какого расстояния комендант ловит игрока. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Komendant", meta = (ClampMin = "50", Units = "cm"))
	float KomendantCatchDistance = 160.f;

	/** Сколько он осматривается на месте шума. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Komendant", meta = (ClampMin = "0", Units = "s"))
	float InvestigateSeconds = 5.f;

	/** Сколько он роется в одном тайнике. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Komendant", meta = (ClampMin = "0", Units = "s"))
	float InspectSeconds = 2.5f;

	/** Сколько секунд после допроса комендант игрока не трогает. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Komendant", meta = (ClampMin = "0", Units = "s"))
	float ImmunitySeconds = 10.f;

	/** Комната, куда комендант уносит изъятое. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Komendant")
	FName EvidenceRoomId = TEXT("Vahta");

	// --- Допрос ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interrogation", meta = (ClampMin = "1", Units = "s"))
	float InterrogationSeconds = 8.f;

	/** В каком радиусе другой игрок может подтвердить алиби. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interrogation", meta = (ClampMin = "0", Units = "cm"))
	float AlibiRadius = 800.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interrogation", meta = (ClampMin = "0"))
	int32 ConfessPenalty = 10;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interrogation", meta = (ClampMin = "0"))
	int32 SilentPenalty = 20;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interrogation", meta = (ClampMin = "0"))
	int32 FailedLiePenalty = 30;

	/** Шанс, что ложь пройдёт без чьей-либо помощи. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interrogation", meta = (ClampMin = "0", ClampMax = "1"))
	float LieBaseChance = 0.35f;

	/** Прибавка к шансу за каждого, кто подтвердил алиби. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interrogation", meta = (ClampMin = "0", ClampMax = "1"))
	float AlibiBonus = 0.3f;

	/** Насколько труднее соврать, если поймали с запрещёнкой в руках. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interrogation", meta = (ClampMin = "0", ClampMax = "1"))
	float ContrabandLiePenalty = 0.25f;


	/** Таблица секретных заданий (строки FTaskRow). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tasks")
	TObjectPtr<UDataTable> TasksTable;
};
