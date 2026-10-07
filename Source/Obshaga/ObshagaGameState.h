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

UENUM(BlueprintType)
enum class EInterrogationChoice : uint8
{
	None,
	Confess,
	Lie,
	Silent
};

/** Идущий сейчас допрос. Виден всем: это публичная сцена, и стоящие рядом решают, подтверждать ли алиби. */
USTRUCT(BlueprintType)
struct FInterrogationInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	bool bActive = false;

	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<APlayerState> Suspect = nullptr;

	/** Момент конца допроса по серверным часам. */
	UPROPERTY(BlueprintReadOnly)
	float EndServerTime = 0.f;

	UPROPERTY(BlueprintReadOnly)
	EInterrogationChoice Choice = EInterrogationChoice::None;

	UPROPERTY(BlueprintReadOnly)
	uint8 AlibiCount = 0;
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

	const FInterrogationInfo& GetInterrogation() const { return Interrogation; }
	float GetInterrogationRemainingSeconds() const;
	/** Радиус, в котором можно подтвердить алиби (приходит с сервера из DA_RoundConfig). */
	float GetAlibiRadius() const { return AlibiRadius; }
	/** Только сервер. */
	void SetInterrogation(const FInterrogationInfo& NewInfo);

	// Только сервер. Вызывает AObshagaGameMode.
	void StartRound(float DurationSeconds, float InAlibiRadius);
	void FinishRound(const TArray<FRevealedTask>& Reveal);

protected:
	UPROPERTY(Replicated)
	ERoundState RoundState = ERoundState::WaitingToStart;

	/** Момент конца раунда по серверным часам (GetServerWorldTimeSeconds). */
	UPROPERTY(Replicated)
	float RoundEndServerTime = 0.f;

	UPROPERTY(Replicated)
	TArray<FRevealedTask> RevealedTasks;

	UPROPERTY(Replicated)
	FInterrogationInfo Interrogation;

	UPROPERTY(Replicated)
	float AlibiRadius = 800.f;
};
