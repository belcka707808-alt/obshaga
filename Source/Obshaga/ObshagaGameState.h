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
	AObshagaGameState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;

	/** Насколько сейчас темно: 0 — день, 1 — глубокая ночь. Считается на каждой машине по фазе раунда. */
	float GetDarkness() const { return Darkness; }

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
	/** Настройки ночного света из DA_RoundConfig: множители солнца и неба ночью и скорость перехода. */
	void SetNightLighting(float InSunScale, float InSkyScale, float InFadeSeconds);
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

	UPROPERTY(Replicated)
	float NightSunScale = 0.12f;

	UPROPERTY(Replicated)
	float NightSkyScale = 5.f;

	UPROPERTY(Replicated)
	float NightFadeSeconds = 6.f;

private:
	void ApplyDarkness();

	// Свет сцены и его дневная яркость; находятся один раз на каждой машине.
	TWeakObjectPtr<class UDirectionalLightComponent> Sun;
	TWeakObjectPtr<class USkyLightComponent> Sky;
	TWeakObjectPtr<AActor> DaySkySphere;
	float SunBaseIntensity = 0.f;
	float SkyBaseIntensity = 0.f;
	FLinearColor SunBaseColor = FLinearColor::White;
	bool bLightsFound = false;
	float Darkness = 0.f;
};
