#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Perception/AIPerceptionTypes.h"
#include "KomendantAIController.generated.h"

class ADoorActor;
class AHidingSpot;
class AKomendantCharacter;
class AKomendantWaypoint;
class AObshagaCharacter;
class APlayerState;
class UAISenseConfig_Hearing;
class UAISenseConfig_Sight;
class UKomendantPersonality;
class UObshagaRoundConfig;

UENUM()
enum class EKomendantState : uint8
{
	/** Обход точек; чаще туда, где недавно шумели. */
	Patrol,
	/** Идёт на шум и осматривается. */
	Investigate,
	/** Гонится за нарушителем. */
	Chase,
	/** Обыскивает тайник. */
	Inspect,
	/** Ложная тревога: остановился и оглядывается. */
	Bluff,
	/** Ведёт допрос пойманного. */
	Interrogate
};

/**
 * «Мозг» коменданта. Существует только на сервере.
 * Видит и слышит через AI Perception (сквозь стены не видит), ходит по точкам AKomendantWaypoint.
 */
UCLASS()
class OBSHAGA_API AKomendantAIController : public AAIController
{
	GENERATED_BODY()

public:
	AKomendantAIController();

	virtual void Tick(float DeltaSeconds) override;

	EKomendantState GetState() const { return State; }

	/** Новый раунд: вернуться на вахту, выбрать личность, всё забыть. Вызывает AObshagaGameMode. */
	void ResetForRound();
	/** Крыса настучала: сходить и посмотреть, что происходит в этой точке. */
	void InvestigateTip(const FVector& Location);
	/** На комнату настучали: при первой возможности обыскать в ней все тайники. */
	void InspectRoom(FName RoomId);

protected:
	virtual void OnPossess(APawn* InPawn) override;

	UFUNCTION()
	void OnPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);

	UPROPERTY(VisibleAnywhere, Category = "Komendant")
	TObjectPtr<UAIPerceptionComponent> Perception;

	UPROPERTY()
	TObjectPtr<UAISenseConfig_Sight> SightConfig;

	UPROPERTY()
	TObjectPtr<UAISenseConfig_Hearing> HearingConfig;

private:
	void BuildGraph();
	void ApplyPersonality();
	void OnPhaseChanged();
	const UObshagaRoundConfig* GetRoundConfig() const;
	bool IsRoundInProgress() const;

	void SetState(EKomendantState NewState);
	void TickPatrol(float DeltaSeconds);
	void TickInvestigate(float DeltaSeconds);
	void TickChase(float DeltaSeconds);
	void TickInspect(float DeltaSeconds);
	void TickBluff(float DeltaSeconds);
	void TickInterrogate(float DeltaSeconds);

	void UpdateVision(float DeltaSeconds);
	void UpdateSlowTimers();
	void StartChase(AObshagaCharacter* Target);
	void StartInvestigate(const FVector& Location);
	void SearchSpot(AHidingSpot* Spot);

	/** Шаг к цели по прямой. true — дошёл. */
	bool MoveToward(const FVector& Goal, float Speed);
	/** Шаг по построенному пути. true — путь пройден (или его нет). */
	bool FollowPath(float Speed, float DeltaSeconds);
	bool PlanPathTo(const FVector& Goal);
	AKomendantWaypoint* FindNearestWaypoint(const FVector& Location) const;
	AKomendantWaypoint* PickPatrolTarget() const;
	AHidingSpot* PickSpotNear(const FVector& Location) const;
	void OpenDoorsNearby();
	void LookAround();
	void FaceLocation(const FVector& Location);
	FVector GetEvidenceLocation() const;

	UPROPERTY()
	TObjectPtr<AKomendantCharacter> Komendant;

	UPROPERTY()
	TObjectPtr<const UKomendantPersonality> Personality;

	UPROPERTY()
	TArray<TObjectPtr<AKomendantWaypoint>> Waypoints;

	UPROPERTY()
	TArray<TObjectPtr<ADoorActor>> Doors;

	UPROPERTY()
	TArray<TObjectPtr<AHidingSpot>> HidingSpots;

	EKomendantState State = EKomendantState::Patrol;
	float StateTime = 0.f;
	bool bGraphReady = false;

	// Путь по точкам.
	TArray<FVector> Path;
	int32 PathIndex = 0;
	float BestDistanceToPoint = 0.f;
	float NoProgressTime = 0.f;

	// Обход.
	float WaitTime = 0.f;
	float BluffDuration = 0.f;
	float LookBaseYaw = 0.f;

	// Проверка шума.
	FVector InvestigateLocation = FVector::ZeroVector;
	bool bInvestigateArrived = false;
	float InvestigateLookTime = 0.f;

	// Погоня.
	TWeakObjectPtr<AObshagaCharacter> ChaseTarget;
	FVector LastKnownLocation = FVector::ZeroVector;
	float LastSeenTime = 0.f;
	float ReplanTime = 0.f;

	// Обыск.
	TWeakObjectPtr<AHidingSpot> InspectSpot;
	float SearchTime = 0.f;

	/** Кого комендант видит в этом кадре (жильцы не в укрытии). */
	TArray<AObshagaCharacter*> SeenPlayers;

	/** «Жар» комнат: где недавно шумели, туда он ходит чаще. */
	TMap<FName, float> RoomHeat;
	/** До какого времени игрока не трогаем (сразу после допроса). */
	TMap<TWeakObjectPtr<AObshagaCharacter>, float> ImmuneUntil;
	TMap<TWeakObjectPtr<AObshagaCharacter>, float> LastSpottedEventTime;

	/** Утренняя проверка: тайники жилых комнат, которые осталось обыскать. */
	TArray<TWeakObjectPtr<AHidingSpot>> InspectionQueue;
	TWeakObjectPtr<AHidingSpot> QueuedSpot;

	FTransform SpawnTransform;
	uint8 LastPhase = 255;

	float SlowTimer = 0.f;
	float DoorTimer = 0.f;
};
