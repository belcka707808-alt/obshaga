#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Perception/AIPerceptionTypes.h"
#include "KomendantAIController.generated.h"

class ADoorActor;
class AHidingSpot;
class AItemActor;
class AKomendantCharacter;
class AKomendantWaypoint;
class AObshagaCharacter;
class APlayerState;
class UAISenseConfig_Hearing;
class UAISenseConfig_Sight;
class UKomendantPersonality;
enum class ENightEvent : uint8;
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
	/** false — коменданту некогда (погоня, допрос), и он никуда не пошёл. */
	bool InvestigateTip(const FVector& Location);
	/** Может ли комендант прямо сейчас отвлечься на донос. */
	bool CanTakeTip() const;
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

public:
	/** Манера речи по личности этого раунда: 0 — строгий, 1 — рассеянный, 2 — мстительный. */
	int32 GetSpeechStyle() const;
	/** Пересчитать зрение и слух: началось или кончилось событие ночи. */
	void RefreshSenses() { ApplyPersonality(); }
	/** Отправить коменданта в точку и держать его там заданное время (чинит трубу). false — он занят погоней или допросом. */
	bool HoldAt(const FVector& Location, float Seconds);
	/** Только для проверок: начать погоню за этим игроком прямо сейчас (дальше всё как в игре). */
	void DebugChase(AObshagaCharacter* Target) { StartChase(Target); }
	/** Идёт ли сейчас это событие ночи. */
	bool IsNightEvent(ENightEvent Event) const;
	bool IsDeaneryMorning() const;

	/** Сколько секунд он осматривается на месте, куда пришёл по шуму или по делу. */
	float InvestigateHoldSeconds = 5.f;

private:
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
	/** bGoalNeedsSight — закончить путь в точке, откуда цель видно (не за стеной от неё). */
	bool PlanPathTo(const FVector& Goal, bool bGoalNeedsSight = false);
	AKomendantWaypoint* FindNearestWaypoint(const FVector& Location, bool bNeedSight = false) const;
	/** Нет ли стены или перекрытия между двумя точками. */
	bool HasClearLine(const FVector& From, const FVector& To) const;
	AKomendantWaypoint* PickPatrolTarget() const;
	AHidingSpot* PickSpotNear(const FVector& Location) const;
	/** Точка тайника на высоте, на которой ходит комендант. */
	FVector GetSpotStandLocation(const AHidingSpot* Spot) const;
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
	/** Сколько раз подряд он упёрся и не перенёсся: дорогу загораживал жилец (сквозь людей не переносится) или у точки не было места. */
	int32 PawnBlockedCount = 0;

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

	// Погоня для журнала автопроверки (tools/verify_round.py): сколько длилась, сколько он видел цель, сколько пробежал,
	// через какие комнаты она шла и сколько раз его переносило. Начало пишет StartChase, конец — LogChaseEnd.
	float ChaseStartTime = 0.f;
	float ChaseRun = 0.f;
	float ChaseSeen = 0.f;
	int32 ChaseRoomChanges = 0;
	int32 ChaseStuck = 0;
	FName ChaseStartRoom;
	FName ChaseLastRoom;
	FVector ChasePrevLocation = FVector::ZeroVector;
	/** Item — что было в руках у цели (при поимке вещь уже отобрана, поэтому её передают отдельно). */
	void LogChaseEnd(const AObshagaCharacter* Target, const TCHAR* Reason, const AHidingSpot* Spot, const AItemActor* Item) const;
	/** Секунды от начала раунда — те же, что в телеметрии. */
	float GetRoundSeconds() const;

	// -TestKomendantStuck=N (не в Shipping): раз за раунд на N-й секунде он «упирается» посреди пути, чтобы сработал перенос на точку.
	float DebugStuckAtSeconds = 0.f;
	float DebugStuckUntil = 0.f;
	bool bDebugStuckDone = false;

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
	TMap<TWeakObjectPtr<AObshagaCharacter>, float> LastSuspiciousEventTime;

	/** Утренняя проверка: тайники жилых комнат, которые осталось обыскать. */
	TArray<TWeakObjectPtr<AHidingSpot>> InspectionQueue;
	TWeakObjectPtr<AHidingSpot> QueuedSpot;
	/** Тайник из списка, который он обыскивает прямо сейчас; если отвлекут — вернётся в список. */
	TWeakObjectPtr<AHidingSpot> InterruptedQueuedSpot;

	FTransform SpawnTransform;
	uint8 LastPhase = 255;

	float SlowTimer = 0.f;
	float DoorTimer = 0.f;
};
