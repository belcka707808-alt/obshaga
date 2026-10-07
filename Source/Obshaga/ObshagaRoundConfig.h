#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ObshagaRoundConfig.generated.h"

class UDataTable;

/** Настройки раунда. Правятся в ассете DA_RoundConfig. */
UCLASS(BlueprintType)
class OBSHAGA_API UObshagaRoundConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	// --- Фазы раунда ---

	/** Вечер: комендант расслаблен. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Round", meta = (ClampMin = "5", Units = "s"))
	float EveningSeconds = 240.f;

	/** Ночь: отбой, комендант строже. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Round", meta = (ClampMin = "5", Units = "s"))
	float NightSeconds = 300.f;

	/** Утро: комендант проверяет жилые комнаты. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Round", meta = (ClampMin = "5", Units = "s"))
	float MorningSeconds = 180.f;

	/** Для разработки: начинать раунд самому, не дожидаясь Enter от хоста. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Round")
	bool bAutoStart = false;

	/** Пауза перед автостартом, чтобы все успели войти. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Round", meta = (ClampMin = "0", Units = "s"))
	float StartDelaySeconds = 5.f;

	/** Сколько раз за раунд приходят СМС-слухи. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Round", meta = (ClampMin = "0"))
	int32 SmsCount = 3;

	/** Сколько строк хроники показывать на экране итогов. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Round", meta = (ClampMin = "1"))
	int32 MaxChronicleLines = 8;

	// --- Роли ---

	/** С какого числа игроков появляются Крыса и Параноик. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roles", meta = (ClampMin = "2"))
	int32 MinPlayersForRoles = 4;

	/** Начиная с этого числа игроков Параноик есть всегда; при меньшем — с шансом 50%. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roles", meta = (ClampMin = "2"))
	int32 PlayersForSureParanoid = 6;

	/** Бонус Крысе, если после её стука жертву поймали. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roles", meta = (ClampMin = "0"))
	int32 RatTipBonus = 15;

	/** В течение какого времени после стука поимка засчитывается Крысе. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roles", meta = (ClampMin = "0", Units = "s"))
	float RatTipWindowSeconds = 60.f;

	// --- Ночь ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Night", meta = (ClampMin = "0.1"))
	float NightSightMultiplier = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Night", meta = (ClampMin = "0.1"))
	float NightHearingMultiplier = 1.3f;

	/** Во сколько раз двери шумнее ночью. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Night", meta = (ClampMin = "0.1"))
	float NightDoorNoiseMultiplier = 2.f;

	/** Рост подозрения в секунду, если ночью комендант видит игрока в запретной зоне. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Night", meta = (ClampMin = "0"))
	float CurfewSuspicionPerSecond = 15.f;

	// --- Выселение ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eviction", meta = (ClampMin = "1"))
	int32 EvictionStrikes = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eviction", meta = (ClampMin = "0"))
	int32 EvictionPenalty = 50;

	/** Стартовое подозрение у тех, кого мстительный комендант ловил в прошлом раунде. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eviction", meta = (ClampMin = "0", ClampMax = "100"))
	float GrudgeSuspicion = 60.f;

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

	// --- Задания ---

	/** Сколько секунд до и после поступка игрока не должен видеть комендант, чтобы считалось «незаметно». */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tasks", meta = (ClampMin = "0", Units = "s"))
	float UnseenWindowSeconds = 10.f;

	/** Сколько нужно пробыть на улице, чтобы ночная вылазка засчиталась. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tasks", meta = (ClampMin = "0", Units = "s"))
	float CurfewOutsideSeconds = 10.f;

	/** Подозрение вору, которого верно назвали коменданту. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tasks", meta = (ClampMin = "0"))
	float AccusationSuspicion = 40.f;

	/** Подозрение тому, кто обвинил невиновного. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tasks", meta = (ClampMin = "0"))
	float FalseAccusationSuspicion = 25.f;

	/** С какого расстояния можно указать коменданту на вора. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tasks", meta = (ClampMin = "0", Units = "cm"))
	float AccuseDistance = 1200.f;

	/** Если чинить некому нечего: через сколько секунд от старта прибор ломается сам (от и до). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tasks", meta = (ClampMin = "0", Units = "s"))
	float SelfBreakMinSeconds = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tasks", meta = (ClampMin = "0", Units = "s"))
	float SelfBreakMaxSeconds = 90.f;

	/** Для разработки: основные задания по порядку игрокам (первому — первое и т.д.). Пусто — обычная раздача. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tasks")
	TArray<FName> DebugMainTasks;

	float GetTotalSeconds() const { return EveningSeconds + NightSeconds + MorningSeconds; }

	/** Таблица секретных заданий (строки FTaskRow). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tasks")
	TObjectPtr<UDataTable> TasksTable;
};
