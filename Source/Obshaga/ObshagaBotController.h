#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "ObshagaBotController.generated.h"

class ADeviceActor;
class AHidingSpot;
class AItemActor;
class AKomendantCharacter;
class AKomendantWaypoint;
class AObshagaCharacter;
class AObshagaPlayerState;
class UObshagaRoundConfig;
struct FTaskRow;

UENUM()
enum class EBotState : uint8
{
	/** Стоит и думает (пауза между делами). */
	Idle,
	/** Идёт к цели по точкам маршрута. */
	Walk,
	/** Делает что-то на месте (ломает, чинит, прячет предмет). */
	Act,
	/** Убегает от Палыча. */
	Flee,
	/** Сидит в шкафу или кабинке. */
	Hidden,
	/** Гуляет на улице за ночным выходом. */
	Outside,
	/** Выселен: уходит к ночному выходу и исчезает. */
	Leaving
};

/** Что бот собирается сделать, когда дойдёт до цели. */
UENUM()
enum class EBotGoal : uint8
{
	None,
	Wander,
	PickUp,
	Drop,
	HideItem,
	HideSelf,
	BreakDevice,
	RepairDevice,
	GoOutside,
	ReadNote,
	LeaveForever
};

/**
 * «Мозг» жильца-бота. Существует только на сервере, как и мозг Палыча. У бота своя карточка игрока
 * (AObshagaPlayerState с флагом «бот») и обычный персонаж AObshagaCharacter, поэтому всё, что он делает,
 * видят все игроки тем же путём, что и действия людей. Задания бот выполняет теми же серверными действиями,
 * что и человек: Interact/SecondaryInteract у предметов, тайников и приборов, Drop у переноски.
 */
UCLASS()
class OBSHAGA_API AObshagaBotController : public AAIController
{
	GENERATED_BODY()

public:
	AObshagaBotController();

	virtual void Tick(float DeltaSeconds) override;

	/** Новый раунд: всё забыть. Вызывает AObshagaGameMode. */
	void ResetForRound();

	EBotState GetBotState() const { return State; }

protected:
	virtual void OnPossess(APawn* InPawn) override;

private:
	const UObshagaRoundConfig* GetRoundConfig() const;
	AObshagaPlayerState* GetMyState() const;
	AKomendantCharacter* FindKomendant() const;

	/** Раз в полсекунды: решить, что делать дальше. */
	void Think();
	/** Задания по очереди: основное, потом побочное. true — нашлось дело. */
	bool PlanTask();
	bool PlanTaskRow(const FTaskRow& Row);
	void PlanWander();
	bool TryFleeOrHide(const AKomendantCharacter* Komendant);
	void HandleInterrogation();
	void Arrive();

	void SetState(EBotState NewState);
	/** Идти к точке; Goal — что сделать по приходу. */
	bool WalkTo(const FVector& Location, EBotGoal Goal, float ArriveRadius = 140.f);

	// Навигация по тем же точкам маршрута, что у Палыча (соседей точек при старте находит его мозг).
	bool PlanPathTo(const FVector& Goal);
	AKomendantWaypoint* FindNearestWaypoint(const FVector& Location) const;
	bool FollowPath(float DeltaSeconds);
	bool HasClearLine(const FVector& From, const FVector& To) const;
	/** Пройдёт ли жилец напрямую (шаром его толщины). */
	bool HasClearWay(const FVector& From, const FVector& To) const;
	void OpenDoorsNearby();
	/** Точка маршрута в комнате с таким id (ближайшая к её середине); нулевой вектор — такой нет. */
	FVector RoomPoint(FName RoomId) const;

	AItemActor* FindItem(FName ItemId, bool bContraband, bool bReadable) const;
	AHidingSpot* FindSpotInRoom(FName RoomId, bool bForPlayer) const;
	ADeviceActor* FindDevice(bool bBroken) const;

	UPROPERTY()
	TObjectPtr<AObshagaCharacter> Me;

	UPROPERTY()
	TArray<TObjectPtr<AKomendantWaypoint>> Waypoints;

	EBotState State = EBotState::Idle;
	EBotGoal Goal = EBotGoal::None;
	float StateTime = 0.f;
	float ThinkTimer = 0.f;
	float IdleUntil = 0.f;
	float ActUntil = 0.f;
	float HideUntil = 0.f;
	float OutsideUntil = 0.f;

	FVector GoalLocation = FVector::ZeroVector;
	float GoalRadius = 140.f;
	TArray<FVector> Path;
	int32 PathIndex = 0;
	float PathBestDistance = 0.f;
	float NoProgressTime = 0.f;
	float WalkTime = 0.f;

	TWeakObjectPtr<AItemActor> TargetItem;
	TWeakObjectPtr<AHidingSpot> TargetSpot;
	TWeakObjectPtr<ADeviceActor> TargetDevice;
	TWeakObjectPtr<AHidingSpot> MySpot;
	/** Комната, куда нести предмет (своя или зона задания). */
	FName CarryRoom;

	/** Записки, которые бот уже прочитал в этом раунде. */
	TSet<TWeakObjectPtr<AItemActor>> ReadNotes;

	// Однократные дела раунда.
	bool bRumorPlanted = false;
	bool bCurfewDone = false;
	bool bTipped = false;
	float TipAtTime = -1.f;
	/** Допрос: когда ответить; на какой допрос уже решили про алиби. */
	float AnswerAt = -1.f;
	/** Реплика над головой: не чаще раза в BotLineCooldown. */
	void Say(uint8 Situation);
	float LastLineTime = -100.f;
	float RoundStartTime = 0.f;
	/** На какой допрос соседа уже отреагировали фразой; кричали ли про тревогу. */
	TWeakObjectPtr<APlayerState> ReactedToCaught;
	bool bFireAlarmGrumbled = false;
	/** Сторож телевизора уже позвал Палыча к вору (раз за раунд). */
	bool bCalledKomendant = false;
	/** Чинящий бот видел сломанный прибор. */
	bool bKnowsBroken = false;
	/** Сколько страйков было у бота на прошлой проверке: так он узнаёт, что его поймали. */
	int32 SeenStrikes = 0;
	/** Сторож видит, что его предмет уносят, — зовёт Палыча к вору. */
	void GuardWatch();
	TWeakObjectPtr<APlayerState> AlibiDecidedFor;
	/** Цель, до которой не дошёл: какое-то время её не берём. */
	TWeakObjectPtr<AActor> GaveUpOn;
	float GaveUpUntil = 0.f;
};
