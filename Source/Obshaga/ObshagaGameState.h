#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "TaskTypes.h"
#include "ObshagaGameState.generated.h"

UENUM(BlueprintType)
enum class ERoundState : uint8
{
	/** Лобби: все ходят по общаге и ждут, пока хост начнёт. */
	WaitingToStart,
	InProgress,
	/** Раунд окончен, на экране итоги. */
	Finished
};

UENUM(BlueprintType)
enum class ERoundPhase : uint8
{
	Evening,
	Night,
	Morning
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

/** «Табло» раунда, которое видят все игроки: идёт ли раунд, фаза, таймер, допрос, итоги. */
UCLASS()
class OBSHAGA_API AObshagaGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	ERoundState GetRoundState() const { return RoundState; }
	ERoundPhase GetPhase() const { return Phase; }
	/** Сколько осталось до конца текущей фазы. */
	float GetPhaseRemainingSeconds() const;

	// Итоги; заполняются только после конца раунда.
	const TArray<FRevealedTask>& GetRevealedTasks() const { return RevealedTasks; }
	const TArray<FRevealedPlayer>& GetRevealedPlayers() const { return RevealedPlayers; }
	const TArray<FText>& GetChronicle() const { return Chronicle; }

	const FInterrogationInfo& GetInterrogation() const { return Interrogation; }
	float GetInterrogationRemainingSeconds() const;
	/** Радиус, в котором можно подтвердить алиби (приходит с сервера из DA_RoundConfig). */
	float GetAlibiRadius() const { return AlibiRadius; }
	/** С какого расстояния можно показать коменданту на вора (тоже из DA_RoundConfig). */
	float GetAccuseDistance() const { return AccuseDistance; }

	// Только сервер. Вызывает AObshagaGameMode.
	void StartRound(float InAlibiRadius, float InAccuseDistance);
	void SetPhase(ERoundPhase NewPhase, float DurationSeconds);
	void FinishRound(const TArray<FRevealedTask>& Tasks, const TArray<FRevealedPlayer>& Players, const TArray<FText>& InChronicle);
	void SetInterrogation(const FInterrogationInfo& NewInfo);

protected:
	UPROPERTY(Replicated)
	ERoundState RoundState = ERoundState::WaitingToStart;

	UPROPERTY(Replicated)
	ERoundPhase Phase = ERoundPhase::Evening;

	/** Момент конца фазы по серверным часам (GetServerWorldTimeSeconds). */
	UPROPERTY(Replicated)
	float PhaseEndServerTime = 0.f;

	UPROPERTY(Replicated)
	TArray<FRevealedTask> RevealedTasks;

	UPROPERTY(Replicated)
	TArray<FRevealedPlayer> RevealedPlayers;

	UPROPERTY(Replicated)
	TArray<FText> Chronicle;

	UPROPERTY(Replicated)
	FInterrogationInfo Interrogation;

	UPROPERTY(Replicated)
	float AlibiRadius = 800.f;

	UPROPERTY(Replicated)
	float AccuseDistance = 1200.f;
};
