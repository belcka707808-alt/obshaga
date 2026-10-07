#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ObshagaGameMode.generated.h"

class AObshagaPlayerState;
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

protected:
	void StartRound();
	void EndRound();
	void UpdateLiveTaskStatus();
	void GiveTask(AObshagaPlayerState* PlayerState);

	/** Настройки раунда; назначаются в BP_ObshagaGameMode. */
	UPROPERTY(EditDefaultsOnly, Category = "Round")
	TObjectPtr<UObshagaRoundConfig> RoundConfig;

	UPROPERTY()
	TObjectPtr<UTaskDirector> TaskDirector;

private:
	FTimerHandle RoundTimer;
	FTimerHandle LiveStatusTimer;
};
