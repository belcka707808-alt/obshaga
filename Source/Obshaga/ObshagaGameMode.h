#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ObshagaGameMode.generated.h"

class AObshagaCharacter;
class AObshagaPlayerState;
enum class EInterrogationChoice : uint8;
class UObshagaRoundConfig;
class UTaskDirector;

/** «Судья» игры. Существует только на сервере: запускает раунд, раздаёт задания, подводит итоги. */
UCLASS()
class OBSHAGA_API AObshagaGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AObshagaGameMode();

	virtual void BeginPlay() override;
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;

	const UObshagaRoundConfig* GetRoundConfig() const;

	/** Комендант поймал игрока: тот замирает и выбирает, что сказать. false — допрос начать нельзя. */
	bool StartInterrogation(AObshagaCharacter* Suspect, const FVector& EvidenceLocation);
	/** Пойманный выбрал ответ. */
	void SubmitInterrogationChoice(AObshagaPlayerState* PlayerState, EInterrogationChoice Choice);
	/** Игрок рядом подтверждает алиби пойманного. */
	void ConfirmAlibi(AObshagaCharacter* By);

protected:
	void StartRound();
	void EndRound();
	void UpdateLiveTaskStatus();
	void GiveTask(AObshagaPlayerState* PlayerState);
	void ResolveInterrogation();
	void NotifyPlayer(const APlayerState* PlayerState, const FText& Text) const;

	/** Настройки раунда; назначаются в BP_ObshagaGameMode. */
	UPROPERTY(EditDefaultsOnly, Category = "Round")
	TObjectPtr<UObshagaRoundConfig> RoundConfig;

	UPROPERTY()
	TObjectPtr<UTaskDirector> TaskDirector;

private:
	FTimerHandle RoundTimer;
	FTimerHandle LiveStatusTimer;
	FTimerHandle InterrogationTimer;

	// Идущий допрос (серверная часть; то, что видят игроки, лежит в GameState).
	TWeakObjectPtr<AObshagaCharacter> InterrogatedCharacter;
	TArray<TWeakObjectPtr<AObshagaCharacter>> AlibiBy;
	bool bSuspectHadContraband = false;
};
