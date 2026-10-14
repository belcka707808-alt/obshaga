#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ObshagaStory.h"
#include "ObshagaGameMode.generated.h"

class AObshagaCharacter;
class AObshagaPlayerState;
class UObshagaRoundConfig;
class UTaskDirector;
enum class EInterrogationChoice : uint8;
enum class ERoundPhase : uint8;
struct FRevealedPlayer;

/**
 * «Судья» игры. Существует только на сервере: ведёт раунд по фазам, раздаёт роли и задания,
 * рассылает СМС, ведёт допросы, выселяет и подводит итоги.
 */
UCLASS()
class OBSHAGA_API AObshagaGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AObshagaGameMode();

	virtual void BeginPlay() override;
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
	virtual FString InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal = TEXT("")) override;
	virtual void Logout(AController* Exiting) override;

	const UObshagaRoundConfig* GetRoundConfig() const;

	/** Хост нажал «начать»: из лобби — старт раунда, с экрана итогов — реванш. Остальных игнорируем. */
	void RequestStart(APlayerController* By);

	/** Комендант поймал игрока: тот замирает и выбирает, что сказать. false — допрос начать нельзя. */
	bool StartInterrogation(AObshagaCharacter* Suspect, const FVector& EvidenceLocation);
	/** Пойманный выбрал ответ. */
	void SubmitInterrogationChoice(AObshagaPlayerState* PlayerState, EInterrogationChoice Choice);
	/** Игрок рядом подтверждает алиби пойманного. */
	void ConfirmAlibi(AObshagaCharacter* By);
	/** Крыса стучит коменданту на свою жертву. */
	void TipOff(AObshagaPlayerState* Rat);
	/** Игрок называет коменданту вора (задание «расследуй кражу»). */
	void Accuse(AObshagaPlayerState* Accuser, AObshagaCharacter* Suspect);
	/** Игрок стучит на жилую комнату, в которой стоит: комендант придёт её обыскивать. */
	void TipOffRoom(AObshagaPlayerState* By);

	/** Манера Палыча, заданная ночью недели: 0 — строгий, 1 — рассеянный, 2 — мстительный; -1 — любая. */
	int32 GetWeekKomendantStyle() const;

	/** Для телеметрии: кто-то упёрся и был перенесён на точку маршрута. */
	void ReportStuck(bool bKomendant) { ++(bKomendant ? NumKomendantStuck : NumBotStuck); }

	/** Кого комендант ловил в прошлом раунде (для мстительной личности). */
	const TArray<TWeakObjectPtr<APlayerState>>& GetCaughtLastRound() const { return CaughtLastRound; }

protected:
	void StartRound();
	void AdvancePhase();
	void BeginPhase(ERoundPhase NewPhase);
	void EndRound();
	void ResetWorldForRematch();

	void AssignHomeRoom(AController* Controller);

	/** Боты: перед раундом добрать (или убрать лишних) до нужного числа жильцов. Только сервер. */
	void SyncBots();
	/** Сколько ботов нужно сейчас: по выбору хоста в меню (?Bots= — сколько всего жильцов) или по ключу -Bots=N. */
	int32 GetWantedBots(int32 NumHumans) const;
	/** Внешность, которая ни у кого не занята (или встречается реже всех). */
	int32 PickFreeLookSlot(const AObshagaPlayerState* Except) const;
	UPROPERTY()
	TArray<TObjectPtr<class AObshagaBotController>> Bots;
	void AssignRoles(const TArray<AObshagaPlayerState*>& Players);
	void UpdateLiveTaskStatus();
	void SendSms();
	FText MakeSmsFor(const AObshagaPlayerState* Reader, const TArray<AObshagaPlayerState*>& Players) const;
	void Evict(AObshagaPlayerState* PlayerState, AObshagaCharacter* Character);
	void ResolveInterrogation();
	/** Включает игроку клавиши, которые нужны его заданиям (обвинить, настучать на комнату). */
	void GrantTaskAbilities(AObshagaPlayerState* PlayerState) const;
	void BreakDeviceByItself();
	void ScheduleSelfBreakIfNeeded();
	/** Пишет итоги раунда в Saved/Telemetry — для правки баланса после плейтестов. */
	void WriteTelemetry(const TArray<AObshagaPlayerState*>& Players) const;

	TArray<FText> BuildChronicle() const;
	void AssignTitles(TArray<FRevealedPlayer>& Players, const TArray<AObshagaPlayerState*>& States) const;
	TArray<AObshagaPlayerState*> GetObshagaPlayers() const;
	void NotifyPlayer(const APlayerState* PlayerState, const FText& Text) const;
	void NotifyAll(const FText& Text) const;
	/** Манера речи коменданта в этом раунде и реплика от его имени. */
	int32 GetKomendantStyle() const;
	void KomendantSay(EKomendantLine Line) const;
	/** Стенгазета итогов: заголовок по самому громкому событию, герой ночи и заметки. */
	void BuildNewspaper(const TArray<AObshagaPlayerState*>& Players) const;

	/** Настройки раунда; назначаются в BP_ObshagaGameMode. */
	UPROPERTY(EditDefaultsOnly, Category = "Round")
	TObjectPtr<UObshagaRoundConfig> RoundConfig;

	UPROPERTY()
	TObjectPtr<UTaskDirector> TaskDirector;

private:
	FTimerHandle PhaseTimer;
	FTimerHandle LiveStatusTimer;
	FTimerHandle SmsTimer;
	FTimerHandle InterrogationTimer;
	FTimerHandle SelfBreakTimer;
	FTimerHandle IntroTimer;
	bool bIntroShown = false;

	/**
	 * «Неделя в общаге №7»: первый раунд комнаты — понедельник, каждый реванш — следующая ночь, после пятницы — новая неделя.
	 * PrepareNight выбирает ночь один раз перед раундом (ещё до заставки, чтобы она показала карточку ночи).
	 */
	void PrepareNight();
	int32 WeekNight = -1;
	int32 WeekId = 0;
	bool bNightPrepared = false;
	/** Титулы недели по итогам пятницы. */
	TArray<FText> BuildWeekTitles(const TArray<AObshagaPlayerState*>& Players) const;
	FTimerHandle NightEventTimer;
	FTimerHandle NightEventEndTimer;
	FTimerHandle FireAlarmTimer;
	float FireAlarmStartTime = 0.f;

	/** Событие ночи: выбрать, объявить, вести и закончить. */
	void StartNightEvent();
	void EndNightEvent();
	void TickFireAlarm();

	float RoundStartWorldTime = 0.f;
	int32 NumKomendantStuck = 0;
	int32 NumBotStuck = 0;
	/** Частота кадров за раунд (раз в секунду, на хосте): для отчёта о балансе и производительности. */
	FTimerHandle FpsTimer;
	float FpsMin = 0.f;
	float FpsSum = 0.f;
	int32 FpsSamples = 0;
	void SampleFps();
	int32 SmsSent = 0;
	int32 NextPlayerNumber = 1;

	/** Игроки, вылетевшие посреди раунда: их состояние (роль, очки, задания) ждёт возвращения по тому же ключу. */
	UPROPERTY()
	TMap<FString, TObjectPtr<AObshagaPlayerState>> Disconnected;
	/** Возвращает вылетевшему его прежнее состояние. true — это был вернувшийся игрок. */
	bool RestoreDisconnected(APlayerController* NewPlayer);
	void ForgetDisconnected();

	// Идущий допрос (серверная часть; то, что видят игроки, лежит в GameState).
	TWeakObjectPtr<AObshagaCharacter> InterrogatedCharacter;
	/** Краденое, с которым поймали: после допроса Палыч возвращает его на место. */
	TWeakObjectPtr<class AItemActor> ItemToReturn;
	TWeakObjectPtr<AObshagaCharacter> ReturnThief;
	FTimerHandle ReturnTimer;
	/** Палыч «донёс» краденое: предмет снова на своём месте, все узнают об этом. */
	void ReturnStolenItem();
	TArray<TWeakObjectPtr<AObshagaCharacter>> AlibiBy;
	bool bSuspectHadContraband = false;

	// Последний стук Крысы: если жертву поймают до TipExpireTime, Крысе бонус.
	TWeakObjectPtr<AObshagaPlayerState> TipRat;
	TWeakObjectPtr<APlayerState> TipTarget;
	float TipExpireTime = 0.f;

	TArray<TWeakObjectPtr<APlayerState>> CaughtThisRound;
	TArray<TWeakObjectPtr<APlayerState>> CaughtLastRound;
};
