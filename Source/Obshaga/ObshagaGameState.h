#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "TaskTypes.h"
#include "ObshagaGameState.generated.h"

UENUM(BlueprintType)
enum class ERoundState : uint8
{
	WaitingToStart,
	InProgress,
	Finished
};

/** «Табло» раунда, которое видят все игроки: идёт ли раунд, сколько осталось, итоги. Фазы — на M5. */
UCLASS()
class OBSHAGA_API AObshagaGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	ERoundState GetRoundState() const { return RoundState; }
	float GetRemainingSeconds() const;
	/** Итоги: кто какое задание имел и чем оно кончилось. Заполняется только после конца раунда. */
	const TArray<FRevealedTask>& GetRevealedTasks() const { return RevealedTasks; }

	// Только сервер. Вызывает AObshagaGameMode.
	void StartRound(float DurationSeconds);
	void FinishRound(const TArray<FRevealedTask>& Reveal);

protected:
	UPROPERTY(Replicated)
	ERoundState RoundState = ERoundState::WaitingToStart;

	/** Момент конца раунда по серверным часам (GetServerWorldTimeSeconds). */
	UPROPERTY(Replicated)
	float RoundEndServerTime = 0.f;

	UPROPERTY(Replicated)
	TArray<FRevealedTask> RevealedTasks;
};
