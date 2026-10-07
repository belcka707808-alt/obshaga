#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
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

	/** Кого комендант ловил в прошлом раунде (для мстительной личности). */
	const TArray<TWeakObjectPtr<APlayerState>>& GetCaughtLastRound() const { return CaughtLastRound; }

protected:
	void StartRound();
	void AdvancePhase();
	void BeginPhase(ERoundPhase NewPhase);
	void EndRound();
	void ResetWorldForRematch();

	void AssignHomeRoom(APlayerController* Controller);
	void AssignRoles(const TArray<AObshagaPlayerState*>& Players);
	void UpdateLiveTaskStatus();
	void SendSms();
	FText MakeSmsFor(const AObshagaPlayerState* Reader, const TArray<AObshagaPlayerState*>& Players) const;
	void Evict(AObshagaPlayerState* PlayerState, AObshagaCharacter* Character);
	void ResolveInterrogation();

	TArray<FText> BuildChronicle() const;
	void AssignTitles(TArray<FRevealedPlayer>& Players, const TArray<AObshagaPlayerState*>& States) const;
	TArray<AObshagaPlayerState*> GetObshagaPlayers() const;
	void NotifyPlayer(const APlayerState* PlayerState, const FText& Text) const;
	void NotifyAll(const FText& Text) const;

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

	float RoundStartWorldTime = 0.f;
	int32 SmsSent = 0;
	int32 NextPlayerNumber = 1;

	// Идущий допрос (серверная часть; то, что видят игроки, лежит в GameState).
	TWeakObjectPtr<AObshagaCharacter> InterrogatedCharacter;
	TArray<TWeakObjectPtr<AObshagaCharacter>> AlibiBy;
	bool bSuspectHadContraband = false;

	// Последний стук Крысы: если жертву поймают до TipExpireTime, Крысе бонус.
	TWeakObjectPtr<AObshagaPlayerState> TipRat;
	TWeakObjectPtr<APlayerState> TipTarget;
	float TipExpireTime = 0.f;

	TArray<TWeakObjectPtr<APlayerState>> CaughtThisRound;
	TArray<TWeakObjectPtr<APlayerState>> CaughtLastRound;
};
