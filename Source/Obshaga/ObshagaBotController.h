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
struct FGameEvent;
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
	SearchSpot,
	LeaveForever,
	/** Подойти к подозреваемому и назвать его коменданту. */
	Accuse,
	/** Зайти в чужую комнату и настучать на неё. */
	TipRoom,
	/** Встать подальше от Палыча и бросить вещь, чтобы он пошёл на шум. */
	Lure,
	/** Бежать к допросу подтвердить алиби. */
	Alibi
};

/** Что бот видел своими глазами (не по слухам). */
enum class EBotSeen : uint8
{
	PickedUp,
	Dropped,
	Thrown,
	Hidden,
	/** Видел, как вещь несли через эту комнату. */
	Carrying
};

/** Насколько бот уверен: видел сам — твёрдо, сложил два и два — средне, слышал в СМС — слабо, наугад — догадка. */
enum class EBotEvidence : uint8
{
	None,
	Guess,
	Weak,
	Medium,
	Strong
};

/** Одна запись памяти бота: кто, что сделал с какой вещью, где и когда. */
struct FBotSighting
{
	TWeakObjectPtr<APlayerState> Who;
	EBotSeen What = EBotSeen::Carrying;
	TWeakObjectPtr<AItemActor> Item;
	FName ItemId;
	bool bContraband = false;
	bool bReadable = false;
	FName RoomId;
	float Time = 0.f;
};

/** Слух из СМС: кто, что делал (0 — тащил, 1 — прятал, 2 — шумел, 3 — крутился) и где. Параноику — выдумка. */
struct FBotRumor
{
	TWeakObjectPtr<APlayerState> Who;
	int32 Verb = INDEX_NONE;
	FName RoomId;
	float Time = 0.f;
};

/** Вывод из памяти: на кого (или на какую комнату) думает бот, насколько уверен и почему (для журнала). */
struct FBotSuspect
{
	TWeakObjectPtr<APlayerState> Who;
	FName RoomId;
	EBotEvidence Strength = EBotEvidence::None;
	FString Source;
};

/** Решение про одну записку на одном месте: читать ли, сколько ещё попыток дойти, прочитал ли. */
struct FBotNoteRoll
{
	FVector Location = FVector::ZeroVector;
	int32 TriesLeft = 0;
	bool bRead = false;
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

	/**
	 * Боту пришло СМС: тот же слух, что увидел бы в телефоне человек (Параноику — выдумка), только данными, а не текстом.
	 * Verb: 0 — «что-то тащил», 1 — «что-то прятал», 2 — «шумел», 3 — «крутился без дела». Вызывает AObshagaGameMode.
	 */
	void HearRumor(APlayerState* Who, int32 Verb, FName RoomId);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
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
	/** «Не попадись на глаза» и уже почти отбой или позже: бот сидит у себя и никуда не ходит. */
	bool IsCarefulAtHomeTime() const;
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

	/** bWorldOnly — только то, что лежит на виду (не из тайника). */
	AItemActor* FindItem(FName ItemId, bool bContraband, bool bReadable, bool bWorldOnly = false) const;
	AHidingSpot* FindSpotInRoom(FName RoomId, bool bForPlayer) const;
	ADeviceActor* FindDevice(bool bBroken) const;
	/** Свободный тайник для вещи вне жилых комнат (санузел, кухня, гостиная), ближайший к боту. */
	AHidingSpot* FindSpotOutsideBedrooms() const;
	/** Обыскать по одному тайники комнаты RoomId (те, что ещё не обыскивал). */
	bool PlanSearchRoom(FName RoomId);
	/** Не бросил ли бот недавно эту цель (не дошёл до неё). */
	bool IsGivenUp(const AActor* Actor) const;
	/** Нужна ли вещь в руках этому заданию. Вещь, не нужная ни одному, бот кладёт. */
	bool RowWantsItem(const FTaskRow& Row, const AItemActor* Item) const;
	/** «Осторожный» бот: задание не попадаться или уже выманил Палыча шумом (и теперь нельзя попасться). */
	bool IsCareful() const;
	float GetRoundSeconds() const;

	// --- Память: что бот видел сам и что слышал в СМС ---
	void OnGameEvent(const FGameEvent& Event);
	/** Видит ли бот этого жильца: не дальше BotWitnessDistance, на своём этаже и в прямой видимости. */
	bool CanWitness(const APawn* Other) const;
	void Remember(APlayerState* Who, EBotSeen What, AItemActor* Item, FName RoomId);
	/** Кто на виду несёт вещь: запомнить; сторож заодно зовёт Палыча к вору. */
	void WatchOthers();
	/** Лежит или едет в руках вещь ItemId в комнате RoomId — это видно всем, кто туда заглянет. */
	bool IsItemShownInRoom(FName ItemId, FName RoomId) const;
	FBotSuspect FindTvThief(const FTaskRow& Row) const;
	FBotSuspect FindNoteAuthor() const;
	FBotSuspect FindContrabandRoom() const;

	// --- Планы заданий, у которых нет «отнести вещь» ---
	bool PlanAccuse(const FTaskRow& Row);
	/** Подозреваемый (AccusePlan) рядом и на виду — назвать его. true — попытка была (принята или нет). */
	bool TryAccuse();
	bool PlanTipRoom();
	/** Бот стоит в комнате TipRoomPlan — настучать. true — попытка была. */
	bool TryTipRoom();
	bool PlanFrame(const FTaskRow& Row);
	bool PlanLure();
	/** Лёгкая вещь не для задания, которая громко стукнет при броске. */
	bool IsLureItem(const AItemActor* Item) const;
	/** Палыч спокоен и на нужном расстоянии — бросить вещь в руках. true — бросил. */
	bool TryLureThrow();

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

	/** Записки: решение читать принимается заново, когда записку переложили на новое место. */
	TMap<TWeakObjectPtr<AItemActor>, FBotNoteRoll> NoteRolls;

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
	/** Сторож видел, кто унёс его предмет; тайники в комнате вора, которые он уже обыскал. */
	TWeakObjectPtr<APlayerState> KnownThief;
	TSet<TWeakObjectPtr<AHidingSpot>> SearchedSpots;
	/** Сколько страйков было у бота на прошлой проверке: так он узнаёт, что его поймали. */
	int32 SeenStrikes = 0;
	/** Сторож видит, что его предмет уносит Thief, — зовёт Палыча к вору. */
	void GuardSaw(const APawn* Thief, const AItemActor* Item);
	/** Палыч был занят, когда сторож его звал: позвать ещё раз не раньше этого. */
	float CallKomendantRetryAt = 0.f;
	TWeakObjectPtr<APlayerState> AlibiDecidedFor;
	/** Цель, до которой не дошёл: какое-то время её не берём. */
	TWeakObjectPtr<AActor> GaveUpOn;
	float GaveUpUntil = 0.f;
	/** С чем бот работал последним (взял вещь, ломал или чинил прибор): поймали — за это дело сразу не берётся. */
	TWeakObjectPtr<AActor> LastWorkedOn;

	/** Подписка на шину событий (только сервер). */
	FDelegateHandle EventHandle;
	TArray<FBotSighting> Sightings;
	TArray<FBotRumor> Rumors;

	/** Обвинение: на кого и почему; до какого времени не пробовать снова (Палыч был занят). */
	FBotSuspect AccusePlan;
	float AccuseRetryAt = 0.f;
	/** Стук на комнату: какую и почему; догадку наугад бот решает один раз за раунд. */
	FBotSuspect TipRoomPlan;
	FBotSuspect TipGuess;
	bool bTipGuessDecided = false;
	float TipRoomRetryAt = 0.f;
	/** Подстава: чья комната, подбросил ли уже и куда (это знание пригодится для стука на комнату). */
	TWeakObjectPtr<APlayerState> FrameVictim;
	FName FrameRoom;
	FName PlantedRoom;
	bool bPlanted = false;
	/** Алиби: к чьему допросу бот уже бежал. Допрос самого бота: когда начал думать над ответом. */
	TWeakObjectPtr<APlayerState> AlibiRunFor;
	float AnswerStartTime = -1.f;
	/** Ложная тревога: Палыч уже ходил на шум бота; когда бросил вещь (ждёт, клюнет ли); когда пробовать снова. */
	bool bLureDone = false;
	float LureThrowTime = -1.f;
	float LureRetryAt = 0.f;
	/** Запрещёнку прятать не у себя, а в общем месте (решается раз за раунд). */
	bool bHideOutsideDecided = false;
	bool bHideOutside = false;
};
