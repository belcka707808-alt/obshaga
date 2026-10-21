#include "ObshagaBotController.h"

#include "CarryComponent.h"
#include "DeviceActor.h"
#include "DoorActor.h"
#include "GameEventSubsystem.h"
#include "HidingSpot.h"
#include "ItemActor.h"
#include "KomendantAIController.h"
#include "KomendantCharacter.h"
#include "KomendantWaypoint.h"
#include "NightExitDoor.h"
#include "Obshaga.h"
#include "ObshagaCharacter.h"
#include "ObshagaGameMode.h"
#include "ObshagaGameState.h"
#include "ObshagaItemData.h"
#include "ObshagaPlayerState.h"
#include "ObshagaRoundConfig.h"
#include "ObshagaStory.h"
#include "Components/CapsuleComponent.h"
#include "RoomVolume.h"
#include "SuspicionComponent.h"
#include "TaskComponent.h"
#include "TaskTypes.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace
{
	constexpr float BotThinkSeconds = 0.4f;
	// Осторожный бот идёт домой за столько секунд до отбоя.
	constexpr float BotCurfewHomeSeconds = 45.f;
	// Точка пути считается пройденной, если до неё ближе этого (по горизонтали) и примерно на той же высоте.
	constexpr float BotPointArriveDistance = 70.f;
	constexpr float BotSameFloorHeight = 150.f;
	// Упёрся и не приближается к точке пути столько секунд — переносим на неё (как Палыча).
	constexpr float BotStuckSeconds = 2.5f;
	// Дольше этого идти к одной цели бессмысленно: значит, до неё не добраться, бот бросает её.
	constexpr float BotGiveUpWalkSeconds = 25.f;
	constexpr float BotGiveUpForgetSeconds = 20.f;
	constexpr float BotDoorOpenDistance = 170.f;
	// Палыч ближе этого — из укрытия не вылезаем.
	constexpr float BotStayHiddenDistance = 700.f;
	// Чем «осторожнее» задание (ни разу не попасться), тем раньше бот уходит от Палыча.
	constexpr float BotCarefulFleeScale = 1.8f;
	// Ответ на допросе — не мгновенно, а через столько секунд (случайно в пределах, но не позже чем за секунду до конца окна):
	// стоящие рядом успевают вступиться.
	constexpr float BotAnswerMinSeconds = 1.5f;
	constexpr float BotAnswerMaxSeconds = 5.f;
	// Как часто бот при безделье машет рукой или говорит фразу.
	constexpr float BotEmoteChance = 0.08f;
	// Обвинение: подходим на столько ближе предельной дальности (сервер меряет чуть иначе).
	constexpr float BotAccuseMargin = 150.f;
	// Сервер отказал, не потратив попытку (Палыч занят, не видно) — пробуем снова через столько секунд.
	constexpr float BotRetrySeconds = 4.f;
	// Крыса: Палыч был занят — стучим снова через столько. Сторож зовёт Палыча снова через столько.
	constexpr float BotRatRetrySeconds = 8.f;
	constexpr float BotCallRetrySeconds = 3.f;
	// Записку переложили дальше этого — это новое место: решаем заново, читать ли.
	constexpr float BotNoteMovedDistance = 150.f;
	// Записку-слух кладём там, где ближе этого стоит больше всего соседей.
	constexpr float BotRumorCrowdDistance = 900.f;
	// Бросил вещь, а Палыч за столько секунд не пошёл на шум — не вышло; снова не раньше чем через BotLureRetrySeconds.
	constexpr float BotLureResultSeconds = 4.f;
	constexpr float BotLureRetrySeconds = 20.f;
	// Место для броска ищем не дальше этого от себя.
	constexpr float BotLureSearchRadius = 2500.f;
	// К допросу бежим, если до конца окна не меньше стольких секунд; бегом (с обходами) бот делает примерно столько см в секунду.
	constexpr float BotAlibiMinWindowSeconds = 2.f;
	constexpr float BotAlibiRunSpeed = 450.f;
	// Столько записей помнит бот за раунд; самые старые забываются.
	constexpr int32 BotMaxSightings = 256;

	/** Для журнала: насколько бот уверен. */
	const TCHAR* EvidenceName(EBotEvidence Strength)
	{
		switch (Strength)
		{
		case EBotEvidence::Strong:
			return TEXT("strong");
		case EBotEvidence::Medium:
			return TEXT("medium");
		case EBotEvidence::Weak:
			return TEXT("weak");
		case EBotEvidence::Guess:
			return TEXT("guess");
		default:
			return TEXT("none");
		}
	}

	/** Для журнала: что бот видел. */
	const TCHAR* SeenName(EBotSeen What)
	{
		switch (What)
		{
		case EBotSeen::PickedUp:
			return TEXT("pick up");
		case EBotSeen::Dropped:
			return TEXT("drop");
		case EBotSeen::Thrown:
			return TEXT("throw");
		case EBotSeen::Hidden:
			return TEXT("hide");
		default:
			return TEXT("carry");
		}
	}
}

AObshagaBotController::AObshagaBotController()
{
	// У бота своя карточка игрока, как у человека: роль, задания, очки, страйки.
	bWantsPlayerState = true;
	PrimaryActorTick.bCanEverTick = true;
}

const UObshagaRoundConfig* AObshagaBotController::GetRoundConfig() const
{
	const AObshagaGameMode* GameMode = GetWorld()->GetAuthGameMode<AObshagaGameMode>();
	return GameMode ? GameMode->GetRoundConfig() : GetDefault<UObshagaRoundConfig>();
}

AObshagaPlayerState* AObshagaBotController::GetMyState() const
{
	return GetPlayerState<AObshagaPlayerState>();
}

AKomendantCharacter* AObshagaBotController::FindKomendant() const
{
	for (TActorIterator<AKomendantCharacter> It(GetWorld()); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

void AObshagaBotController::Say(uint8 Situation)
{
	const float Now = GetWorld()->GetTimeSeconds();
	if (Me && Now - LastLineTime >= GetRoundConfig()->BotLineCooldown)
	{
		LastLineTime = Now;
		Me->SayBotLine(Situation, static_cast<uint8>(FMath::RandRange(0, ObshagaStory::BotLinesPerSituation - 1)));
	}
}

void AObshagaBotController::BeginPlay()
{
	Super::BeginPlay();
	// Бот замечает то же, что заметил бы человек на его месте: слушает шину событий, а что из этого он видел, решает сам
	// (дальность и прямая видимость). Подписка одна на весь срок жизни мозга — раз за разом персонаж меняется, а мозг тот же.
	if (UGameEventSubsystem* Bus = UGameEventSubsystem::Get(this); Bus && !EventHandle.IsValid())
	{
		EventHandle = Bus->OnGameEvent.AddUObject(this, &AObshagaBotController::OnGameEvent);
	}
}

void AObshagaBotController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UGameEventSubsystem* Bus = UGameEventSubsystem::Get(this); Bus && EventHandle.IsValid())
	{
		Bus->OnGameEvent.Remove(EventHandle);
	}
	EventHandle.Reset();
	Super::EndPlay(EndPlayReason);
}

float AObshagaBotController::GetRoundSeconds() const
{
	const AObshagaGameMode* GameMode = GetWorld()->GetAuthGameMode<AObshagaGameMode>();
	return GameMode ? GameMode->GetRoundSeconds() : 0.f;
}

bool AObshagaBotController::IsGivenUp(const AActor* Actor) const
{
	return Actor && GaveUpOn.Get() == Actor && GetWorld()->GetTimeSeconds() < GaveUpUntil;
}

bool AObshagaBotController::IsCareful() const
{
	// Выманил Палыча шумом (или только что бросил вещь) — теперь попасться нельзя, иначе «ложная тревога» не засчитается.
	const AObshagaPlayerState* MyState = GetMyState();
	return bLureDone || LureThrowTime >= 0.f
		|| (MyState && MyState->GetTaskComponent()->FindRowByCondition(ETaskCondition::NeverSpottedWholeRound) != nullptr);
}

void AObshagaBotController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	Me = Cast<AObshagaCharacter>(InPawn);
	if (Me)
	{
		// Встречные боты обходят друг друга, а не упираются лбами в узком коридоре.
		UCharacterMovementComponent* Movement = Me->GetCharacterMovement();
		Movement->bUseRVOAvoidance = true;
		Movement->AvoidanceConsiderationRadius = 250.f;
		Movement->AvoidanceWeight = 0.5f;
		// Боты проходят друг сквозь друга: иначе двое встречных в дверном проёме упирались лбами.
		// С людьми и Палычем столкновения остаются.
		for (TActorIterator<AObshagaCharacter> It(GetWorld()); It; ++It)
		{
			const APlayerState* Other = It->GetPlayerState();
			if (*It != Me && Other && Other->IsABot())
			{
				Me->GetCapsuleComponent()->IgnoreActorWhenMoving(*It, true);
				It->GetCapsuleComponent()->IgnoreActorWhenMoving(Me, true);
			}
		}
	}
	Waypoints.Reset();
	for (TActorIterator<AKomendantWaypoint> It(GetWorld()); It; ++It)
	{
		Waypoints.Add(*It);
	}
	SetState(EBotState::Idle);
	IdleUntil = GetWorld()->GetTimeSeconds() + FMath::FRandRange(0.5f, 2.f);
}

void AObshagaBotController::ResetForRound()
{
	SetState(EBotState::Idle);
	Goal = EBotGoal::None;
	Path.Reset();
	TargetItem.Reset();
	TargetSpot.Reset();
	TargetDevice.Reset();
	MySpot.Reset();
	GaveUpOn.Reset();
	bRumorPlanted = false;
	bCurfewDone = false;
	NoteRolls.Reset();
	bTipped = false;
	TipAtTime = -1.f;
	AnswerAt = -1.f;
	AlibiDecidedFor.Reset();
	ReactedToCaught.Reset();
	bFireAlarmGrumbled = false;
	bCalledKomendant = false;
	bKnowsBroken = false;
	SeenStrikes = 0;
	KnownThief.Reset();
	SearchedSpots.Reset();
	CallKomendantRetryAt = 0.f;
	LastWorkedOn.Reset();
	// Память раунда: что видел, что слышал в СМС, что задумал.
	Sightings.Reset();
	Rumors.Reset();
	AccusePlan = FBotSuspect();
	AccuseRetryAt = 0.f;
	TipRoomPlan = FBotSuspect();
	TipGuess = FBotSuspect();
	bTipGuessDecided = false;
	TipRoomRetryAt = 0.f;
	FrameVictim.Reset();
	FrameRoom = NAME_None;
	PlantedRoom = NAME_None;
	bPlanted = false;
	AlibiRunFor.Reset();
	AnswerStartTime = -1.f;
	bLureDone = false;
	LureThrowTime = -1.f;
	LureRetryAt = 0.f;
	bHideOutsideDecided = false;
	bHideOutside = false;
	RoundStartTime = GetWorld()->GetTimeSeconds();
	IdleUntil = GetWorld()->GetTimeSeconds() + FMath::FRandRange(1.f, 4.f);
}

void AObshagaBotController::SetState(EBotState NewState)
{
	State = NewState;
	StateTime = 0.f;
}

void AObshagaBotController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Me || !IsValid(Me))
	{
		return;
	}
	StateTime += DeltaSeconds;

	ThinkTimer -= DeltaSeconds;
	if (ThinkTimer <= 0.f)
	{
		ThinkTimer = BotThinkSeconds;
		Think();
	}
	if (!Me || !IsValid(Me))
	{
		return;
	}

	// Ходьба — каждый кадр, решения — реже.
	if ((State == EBotState::Walk || State == EBotState::Flee || State == EBotState::Leaving) && !Me->IsFrozen() && !Me->IsHiding())
	{
		WalkTime += DeltaSeconds;
		OpenDoorsNearby();
		if (FollowPath(DeltaSeconds))
		{
			// Путь по точкам пройден — последний отрезок до цели по прямой.
			const FVector ToGoal = GoalLocation - Me->GetActorLocation();
			if (ToGoal.Size2D() <= GoalRadius && FMath::Abs(ToGoal.Z) < BotSameFloorHeight)
			{
				Arrive();
			}
			else
			{
				Me->AddMovementInput(FVector(ToGoal.X, ToGoal.Y, 0.f).GetSafeNormal());
				NoProgressTime += DeltaSeconds;
				if (NoProgressTime > BotStuckSeconds)
				{
					// До самой цели не подойти (она за мебелью) — считаем, что дошёл, если рядом; иначе бросаем.
					if (ToGoal.Size2D() < GoalRadius * 1.8f)
					{
						Arrive();
					}
					else
					{
						GaveUpOn = TargetItem.IsValid() ? static_cast<AActor*>(TargetItem.Get()) : (TargetSpot.IsValid() ? static_cast<AActor*>(TargetSpot.Get()) : static_cast<AActor*>(TargetDevice.Get()));
						GaveUpUntil = GetWorld()->GetTimeSeconds() + BotGiveUpForgetSeconds;
						SetState(EBotState::Idle);
					}
				}
			}
		}
		if (WalkTime > BotGiveUpWalkSeconds && State == EBotState::Walk)
		{
			UE_LOG(LogObshaga, Verbose, TEXT("Bot %s gave up walking to %s"), *GetNameSafe(GetMyState()), *GoalLocation.ToCompactString());
			GaveUpOn = TargetItem.IsValid() ? static_cast<AActor*>(TargetItem.Get()) : static_cast<AActor*>(TargetSpot.Get());
			GaveUpUntil = GetWorld()->GetTimeSeconds() + BotGiveUpForgetSeconds;
			SetState(EBotState::Idle);
		}
	}
}

void AObshagaBotController::Think()
{
	const AObshagaGameState* GameState = GetWorld()->GetGameState<AObshagaGameState>();
	AObshagaPlayerState* MyState = GetMyState();
	AObshagaGameMode* GameMode = GetWorld()->GetAuthGameMode<AObshagaGameMode>();
	const UObshagaRoundConfig* Config = GetRoundConfig();
	const float Now = GetWorld()->GetTimeSeconds();
	if (!GameState || !MyState || !GameMode)
	{
		return;
	}

	// Вне раунда (лобби, итоги) бот просто стоит.
	if (GameState->GetRoundState() != ERoundState::InProgress)
	{
		if (State != EBotState::Idle)
		{
			SetState(EBotState::Idle);
		}
		return;
	}

	// Выселен: уходит к ночному выходу и исчезает (карточка игрока остаётся — для итогов).
	if (MyState->IsEvicted())
	{
		if (State != EBotState::Leaving)
		{
			for (TActorIterator<ANightExitDoor> It(GetWorld()); It; ++It)
			{
				SetState(EBotState::Leaving);
				WalkTo(It->GetActorLocation(), EBotGoal::LeaveForever, 220.f);
				State = EBotState::Leaving;
				break;
			}
			if (State != EBotState::Leaving)
			{
				Arrive();
			}
		}
		return;
	}

	HandleInterrogation();
	// Поймали — за то же дело сразу не берётся: пусть дело остынет (иначе вор нёс телевизор снова, едва отпустили).
	const int32 Strikes = MyState->GetSuspicionComponent()->GetStrikes();
	if (Strikes > SeenStrikes)
	{
		SeenStrikes = Strikes;
		AActor* Last = LastWorkedOn.Get();
		if (Last && Config->BotRetryAfterCaughtSeconds > 0.f)
		{
			GaveUpOn = Last;
			GaveUpUntil = Now + Config->BotRetryAfterCaughtSeconds;
		}
	}
	if (!bFireAlarmGrumbled && GameState->IsNightEventActive() && GameState->GetNightEvent() == ENightEvent::FireAlarm && FMath::FRand() < 0.15f)
	{
		bFireAlarmGrumbled = true;
		Say(static_cast<uint8>(ObshagaStory::EBotLine::FireAlarm));
	}
	// Ложная тревога: бросил вещь, а Палыч на шум не пошёл — не вышло, попробуем позже другой вещью.
	if (LureThrowTime >= 0.f && !bLureDone && Now - LureThrowTime > BotLureResultSeconds)
	{
		UE_LOG(LogObshaga, Verbose, TEXT("Bot %s lure result: failed after %.1f s, t=%.1f"), *MyState->GetPlayerName(), Now - LureThrowTime, GetRoundSeconds());
		LureThrowTime = -1.f;
		LureRetryAt = Now + BotLureRetrySeconds;
	}
	if (Me->IsFrozen())
	{
		return;
	}
	WatchOthers();

	// Бот-Крыса стучит на жертву, когда ночью подходит его время.
	if (MyState->GetTrueRole() == EPlayerRole::Rat && !bTipped && GameState->GetPhase() == ERoundPhase::Night)
	{
		if (TipAtTime < 0.f)
		{
			TipAtTime = Now + FMath::FRandRange(Config->BotTipMinSeconds, FMath::Max(Config->BotTipMinSeconds, Config->BotTipMaxSeconds));
		}
		else if (Now >= TipAtTime && MyState->GetRatTarget())
		{
			const AObshagaPlayerState* Victim = Cast<AObshagaPlayerState>(MyState->GetRatTarget());
			if (MyState->HasUsedTip() || (Victim && Victim->IsEvicted()))
			{
				// Стук уже потрачен или жертву выселили (это видно всем) — стучать больше не на кого.
				bTipped = true;
			}
			else if (GameMode->TipOff(MyState))
			{
				bTipped = true;
				UE_LOG(LogObshaga, Verbose, TEXT("Bot %s tipped off on %s"), *MyState->GetPlayerName(), *MyState->GetRatTarget()->GetPlayerName());
			}
			else
			{
				// Палыч занят погоней или допросом: сервер стук не потратил — стучим ещё раз чуть позже.
				TipAtTime = Now + BotRatRetrySeconds;
				UE_LOG(LogObshaga, Verbose, TEXT("Bot %s tip on %s refused, retry in %.0f s"), *MyState->GetPlayerName(), *MyState->GetRatTarget()->GetPlayerName(), BotRatRetrySeconds);
			}
		}
	}

	const AKomendantCharacter* Komendant = FindKomendant();

	switch (State)
	{
	case EBotState::Hidden:
	{
		// Сидит, пока не выйдет время и пока Палыч рядом.
		const bool bDanger = Komendant && FVector::Dist(Komendant->GetActorLocation(), Me->GetActorLocation()) < BotStayHiddenDistance;
		if (!Me->IsHiding())
		{
			SetState(EBotState::Idle);
		}
		else if (Now >= HideUntil && !bDanger && MySpot.IsValid())
		{
			MySpot->Interact(Me);
			SetState(EBotState::Idle);
			IdleUntil = Now + 1.f;
		}
		return;
	}
	case EBotState::Outside:
		if (Now >= OutsideUntil && MySpot.IsValid())
		{
			MySpot->Interact(Me);
			bCurfewDone = true;
			SetState(EBotState::Idle);
		}
		return;
	case EBotState::Act:
		if (Now >= ActUntil)
		{
			SetState(EBotState::Idle);
			IdleUntil = Now + 0.5f;
		}
		return;
	case EBotState::Leaving:
		return;
	default:
		break;
	}

	// Бежит к допросу подтвердить алиби: Палыч занят допросом и на других не смотрит — от него не шарахаемся.
	const bool bRunningToAlibi = State == EBotState::Walk && Goal == EBotGoal::Alibi;
	if (Komendant && !bRunningToAlibi && TryFleeOrHide(Komendant))
	{
		return;
	}
	if (State == EBotState::Flee)
	{
		// Убежал достаточно далеко — снова к делам.
		if (StateTime > 6.f)
		{
			Me->SetSprinting(false);
			SetState(EBotState::Idle);
		}
		return;
	}
	if (State == EBotState::Walk)
	{
		// По дороге: подозреваемый уже близко и на виду — называем сразу; уже вошли в нужную комнату — стучим сразу.
		if (Goal == EBotGoal::Accuse)
		{
			TryAccuse();
		}
		else if (Goal == EBotGoal::TipRoom)
		{
			TryTipRoom();
		}
		return;
	}

	// Idle.
	if (Now < IdleUntil)
	{
		return;
	}
	// Новое дело — новые цели: «не дошёл» должно записываться на то, к чему бот идёт сейчас, а не на прошлое.
	TargetItem.Reset();
	TargetSpot.Reset();
	TargetDevice.Reset();
	// Записка рядом — любопытно, что там (без читателей задание «пустить слух» не выполнить никому).
	// Осторожный бот после отбоя из комнаты за ней не выходит.
	if (!Me->GetCarryComponent()->IsCarrying() && !IsCarefulAtHomeTime())
	{
		for (TActorIterator<AItemActor> It(GetWorld()); It; ++It)
		{
			AItemActor* Note = *It;
			const FVector NoteAt = Note->GetActorLocation();
			if (!Note->GetItemData()->bReadable || Note->GetItemState() != EItemState::World || IsGivenUp(Note)
				|| FVector::Dist(NoteAt, Me->GetActorLocation()) >= 900.f || FMath::Abs(NoteAt.Z - Me->GetActorLocation().Z) >= BotSameFloorHeight * 2.f)
			{
				continue;
			}
			// Читать ли — решается заново для каждого места записки: подкинутую туда, где её уже видели, тоже прочтут.
			const TWeakObjectPtr<AItemActor> NoteKey(Note);
			FBotNoteRoll* Roll = NoteRolls.Find(NoteKey);
			if (!Roll || FVector::Dist(Roll->Location, NoteAt) > BotNoteMovedDistance)
			{
				FBotNoteRoll Fresh;
				Fresh.Location = NoteAt;
				// Две попытки: если дорогу сорвал Палыч, к тому же месту бот сходит ещё раз.
				Fresh.TriesLeft = FMath::FRand() < Config->BotReadNoteChance ? 2 : 0;
				Roll = &NoteRolls.Add(NoteKey, Fresh);
			}
			if (Roll->bRead || Roll->TriesLeft <= 0)
			{
				continue;
			}
			--Roll->TriesLeft;
			TargetItem = Note;
			if (WalkTo(NoteAt, EBotGoal::ReadNote, 150.f))
			{
				return;
			}
		}
	}
	if (!PlanTask())
	{
		PlanWander();
	}
}

void AObshagaBotController::HandleInterrogation()
{
	const AObshagaGameState* GameState = GetWorld()->GetGameState<AObshagaGameState>();
	AObshagaGameMode* GameMode = GetWorld()->GetAuthGameMode<AObshagaGameMode>();
	AObshagaPlayerState* MyState = GetMyState();
	const FInterrogationInfo& Info = GameState->GetInterrogation();
	const UObshagaRoundConfig* Config = GetRoundConfig();
	const float Now = GetWorld()->GetTimeSeconds();
	if (!Info.bActive || !Info.Suspect)
	{
		AnswerAt = -1.f;
		AnswerStartTime = -1.f;
		// Допрос кончился, пока бот бежал подтвердить алиби, — бежать уже незачем.
		if (State == EBotState::Walk && Goal == EBotGoal::Alibi)
		{
			Goal = EBotGoal::None;
			Path.Reset();
			Me->SetSprinting(false);
			SetState(EBotState::Idle);
		}
		return;
	}
	const float Remaining = GameState->GetInterrogationRemainingSeconds();

	if (Info.Suspect == MyState)
	{
		// Пойман: подумать пару секунд и выбрать ответ.
		if (Info.Choice != EInterrogationChoice::None)
		{
			return;
		}
		if (AnswerAt < 0.f)
		{
			// Не спешим: пусть стоящие рядом успеют вступиться (тогда врать выгоднее). Но и не до последней секунды.
			AnswerStartTime = Now;
			const float Latest = FMath::Max(BotAnswerMinSeconds, FMath::Min(BotAnswerMaxSeconds, Remaining - 1.f));
			AnswerAt = Now + FMath::FRandRange(BotAnswerMinSeconds, Latest);
			Path.Reset();
			SetState(EBotState::Idle);
		}
		// Вступились — отвечаем сразу (ждать больше нечего), но не раньше, чем бот «подумал».
		else if (Now >= AnswerAt || (Info.AlibiCount > 0 && Now - AnswerStartTime >= BotAnswerMinSeconds))
		{
			const int32 Alibis = Info.AlibiCount;
			// За него вступились — ложь пройдёт вероятнее, и бот это понимает. Иначе — случайно с весами.
			const bool bLieForFriends = Alibis > 0 && FMath::FRand() < Config->BotLieWithAlibiChance;
			const float Total = Config->BotConfessWeight + Config->BotLieWeight + Config->BotSilentWeight;
			const float Roll = FMath::FRandRange(0.f, FMath::Max(Total, 0.01f));
			const EInterrogationChoice Choice = bLieForFriends ? EInterrogationChoice::Lie
				: (Roll < Config->BotConfessWeight ? EInterrogationChoice::Confess
					: (Roll < Config->BotConfessWeight + Config->BotLieWeight ? EInterrogationChoice::Lie : EInterrogationChoice::Silent));
			GameMode->SubmitInterrogationChoice(MyState, Choice);
			UE_LOG(LogObshaga, Verbose, TEXT("Bot %s answers %s (alibi %d)"), *MyState->GetPlayerName(), *UEnum::GetValueAsString(Choice), Alibis);
			AnswerAt = -1.f;
		}
		return;
	}

	// Допрашивают соседа, а бот стоит рядом: иногда вступается.
	const APawn* SuspectPawn = Info.Suspect->GetPawn();
	if (ReactedToCaught.Get() != Info.Suspect && SuspectPawn && !Me->IsHiding() && FVector::Dist(SuspectPawn->GetActorLocation(), Me->GetActorLocation()) < 1200.f
		&& FVector::Dist(SuspectPawn->GetActorLocation(), Me->GetActorLocation()) > 50.f)
	{
		ReactedToCaught = Info.Suspect;
		if (FMath::FRand() < 0.6f)
		{
			Say(static_cast<uint8>(ObshagaStory::EBotLine::NeighbourCaught));
		}
	}
	if (AlibiDecidedFor.Get() == Info.Suspect || !SuspectPawn || Me->IsHiding() || Me->IsGhost())
	{
		return;
	}
	// У кого задание «прикрой друга», тот вступается всегда и даже прибегает издалека; остальные — иногда и только стоя рядом.
	const bool bCoverTask = MyState->GetTaskComponent()->FindRowByCondition(ETaskCondition::AlibiConfirmedSuccessfully) != nullptr;
	const float Distance = FVector::Dist(SuspectPawn->GetActorLocation(), Me->GetActorLocation());
	const float Radius = GameState->GetAlibiRadius();
	if (Distance <= Radius)
	{
		AlibiDecidedFor = Info.Suspect;
		if (bCoverTask || FMath::FRand() < Config->BotAlibiChance)
		{
			GameMode->ConfirmAlibi(Me);
			Say(static_cast<uint8>(ObshagaStory::EBotLine::Alibi));
			UE_LOG(LogObshaga, Verbose, TEXT("Bot %s confirms alibi of %s"), *MyState->GetPlayerName(), *Info.Suspect->GetPlayerName());
		}
		if (State == EBotState::Walk && Goal == EBotGoal::Alibi)
		{
			Goal = EBotGoal::None;
			Path.Reset();
			Me->SetSprinting(false);
			SetState(EBotState::Idle);
		}
		return;
	}
	// Бежать к допросу — один раз на допрос, если успеваем: окно не закрывается и путь есть.
	// Из шкафа, с улицы и посреди дела (прячет вещь, чинит) не срывается.
	const bool bFreeToRun = State == EBotState::Idle || State == EBotState::Walk || State == EBotState::Flee;
	if (bCoverTask && bFreeToRun && !Me->IsFrozen() && AlibiRunFor.Get() != Info.Suspect && Remaining >= BotAlibiMinWindowSeconds
		&& Distance <= Config->BotAlibiRunDistance && (Distance - Radius) / BotAlibiRunSpeed <= Remaining - 0.5f)
	{
		AlibiRunFor = Info.Suspect;
		if (WalkTo(SuspectPawn->GetActorLocation(), EBotGoal::Alibi, Radius * 0.8f))
		{
			Me->SetSprinting(!Me->GetCarryComponent()->IsCarryingHeavy());
			UE_LOG(LogObshaga, Verbose, TEXT("Bot %s runs to alibi for %s: %.0f cm, %.1f s left, t=%.1f"), *MyState->GetPlayerName(), *Info.Suspect->GetPlayerName(),
				Distance, Remaining, GetRoundSeconds());
		}
	}
}

void AObshagaBotController::WatchOthers()
{
	if (Me->IsHiding() || Me->IsGhost())
	{
		return;
	}
	// Кто на виду несёт вещь — запоминаем, где и что (потом это улики для обвинения или стука).
	const FTaskRow* GuardRow = GetMyState()->GetTaskComponent()->FindRowByCondition(ETaskCondition::ItemNotMovedFromZone);
	for (TActorIterator<AObshagaCharacter> It(GetWorld()); It; ++It)
	{
		AObshagaCharacter* Other = *It;
		APlayerState* OtherState = Other->GetPlayerState();
		AItemActor* Item = Other->GetCarryComponent()->GetCarriedItem();
		if (Other == Me || !OtherState || !Item || Other->IsGhost() || !CanWitness(Other))
		{
			continue;
		}
		const ARoomVolume* Room = Other->GetCurrentRoom();
		Remember(OtherState, EBotSeen::Carrying, Item, Room ? Room->RoomId : NAME_None);
		if (GuardRow && Item->GetItemData()->ItemId == GuardRow->ItemId)
		{
			GuardSaw(Other, Item);
		}
	}
}

void AObshagaBotController::GuardSaw(const APawn* Thief, const AItemActor* Item)
{
	// Запоминает вора: если тот спрячет телевизор у себя, сторож пойдёт обыскивать его комнату.
	KnownThief = Thief->GetPlayerState();
	const float Now = GetWorld()->GetTimeSeconds();
	if (bCalledKomendant || Now < CallKomendantRetryAt)
	{
		return;
	}
	// Видит вора с телевизором: кричит и зовёт Палыча туда, где вор сейчас (как стук Крысы, но вслух).
	// Палыч занят погоней или допросом — зовём ещё раз чуть позже (раньше такой зов пропадал зря).
	bool bTaken = false;
	for (TActorIterator<AKomendantAIController> Brain(GetWorld()); Brain; ++Brain)
	{
		bTaken |= Brain->InvestigateTip(Thief->GetActorLocation());
	}
	if (!bTaken)
	{
		CallKomendantRetryAt = Now + BotCallRetrySeconds;
		return;
	}
	bCalledKomendant = true;
	LastLineTime = -100.f;
	Say(static_cast<uint8>(ObshagaStory::EBotLine::CallKomendant));
	UE_LOG(LogObshaga, Verbose, TEXT("Bot %s calls the komendant: %s carries %s"), *GetMyState()->GetPlayerName(), *GetNameSafe(Thief->GetPlayerState()), *Item->GetName());
}

bool AObshagaBotController::TryFleeOrHide(const AKomendantCharacter* Komendant)
{
	const UObshagaRoundConfig* Config = GetRoundConfig();
	const AObshagaPlayerState* MyState = GetMyState();
	const bool bCareful = IsCareful();
	// Осторожный бот после отбоя у себя в комнате не подозрителен — а выбежать от Палыча в коридор как раз подозрительно.
	// Утром Палыч проверяет и шкафы, так что прятаться у себя тоже нельзя: сидящего в шкафу он ловит.
	if (IsCarefulAtHomeTime() && !Me->GetCarryComponent()->IsCarrying())
	{
		const ARoomVolume* Room = ARoomVolume::FindRoomAt(this, Me->GetActorLocation());
		if (Room && Room->RoomId == MyState->GetHomeRoomId())
		{
			return false;
		}
	}
	const float FleeDistance = Config->BotFleeDistance * (bCareful ? BotCarefulFleeScale : 1.f);
	const FVector Here = Me->GetActorLocation();
	const FVector KomendantAt = Komendant->GetActorLocation();
	const float Distance = FVector::Dist(Here, KomendantAt);
	if (Distance > FleeDistance || FMath::Abs(Here.Z - KomendantAt.Z) > BotSameFloorHeight * 2.f || !HasClearLine(Here, KomendantAt))
	{
		return false;
	}
	// От спокойного Палыча бот не шарахается, если ничего не несёт: просто идёт по своим делам.
	// Убегает, когда Палыч насторожился или гонится, когда в руках что-то есть, ночью вблизи и если задание — не попадаться.
	const AObshagaGameState* GameState = GetWorld()->GetGameState<AObshagaGameState>();
	const bool bNightClose = GameState && GameState->GetPhase() != ERoundPhase::Evening && Distance < FleeDistance * 0.6f;
	if (Komendant->GetAlert() == EKomendantAlert::Calm && !Me->GetCarryComponent()->IsCarrying() && !bCareful && !bNightClose)
	{
		return false;
	}
	if (State == EBotState::Flee && StateTime < 3.f)
	{
		return true;
	}

	// Шкаф или кабинка рядом — прячемся (с предметом в руках в шкаф не залезть).
	if (!Me->GetCarryComponent()->IsCarrying())
	{
		AHidingSpot* Best = nullptr;
		float BestDistance = Config->BotHideSearchRadius;
		for (TActorIterator<AHidingSpot> It(GetWorld()); It; ++It)
		{
			if (!It->CanHidePlayer() || It->IsBusy() || It->GetHiddenPlayer() || It->IsA<ANightExitDoor>())
			{
				continue;
			}
			const float SpotDistance = FVector::Dist(It->GetActorLocation(), Here);
			// Тайник по ту сторону от Палыча не годится: бежать к нему — бежать навстречу.
			if (SpotDistance < BestDistance && FVector::Dist(It->GetActorLocation(), KomendantAt) > Distance * 0.7f
				&& FMath::Abs(It->GetActorLocation().Z - Here.Z) < BotSameFloorHeight)
			{
				BestDistance = SpotDistance;
				Best = It.operator*();
			}
		}
		if (Best)
		{
			UE_LOG(LogObshaga, Verbose, TEXT("Bot %s runs to hide in %s (komendant %.0f cm)"), *MyState->GetPlayerName(), *Best->GetName(), Distance);
			TargetSpot = Best;
			WalkTo(Best->GetActorLocation(), EBotGoal::HideSelf, 160.f);
			Say(static_cast<uint8>(ObshagaStory::EBotLine::SpotKomendant));
			State = EBotState::Flee;
			// Бег включаем после WalkTo: он выключает бег всем, кто ещё не в состоянии «убегает».
			Me->SetSprinting(true);
			return true;
		}
	}

	// Иначе убегаем: точка маршрута на том же этаже, подальше от Палыча, но не слишком далеко от нас.
	AKomendantWaypoint* Best = nullptr;
	float BestScore = -1.f;
	for (AKomendantWaypoint* Waypoint : Waypoints)
	{
		const FVector At = Waypoint->GetActorLocation();
		const float FromMe = FVector::Dist(At, Here);
		if (FromMe > 1800.f || FMath::Abs(At.Z - Here.Z) > BotSameFloorHeight * 2.f)
		{
			continue;
		}
		const float Score = FVector::Dist(At, KomendantAt) - FromMe * 0.3f;
		if (Score > BestScore)
		{
			BestScore = Score;
			Best = Waypoint;
		}
	}
	if (!Best)
	{
		return false;
	}
	WalkTo(Best->GetActorLocation(), EBotGoal::None, 120.f);
	State = EBotState::Flee;
	Me->SetSprinting(!Me->GetCarryComponent()->IsCarryingHeavy());
	UE_LOG(LogObshaga, Verbose, TEXT("Bot %s flees from the komendant"), *MyState->GetPlayerName());
	Say(static_cast<uint8>(ObshagaStory::EBotLine::SpotKomendant));
	return true;
}

bool AObshagaBotController::PlanTask()
{
	const UTaskComponent* Tasks = GetMyState()->GetTaskComponent();
	const TArray<FTaskRow>& Rows = Tasks->GetRows();
	const TArray<FTaskState>& States = Tasks->GetTasks();

	// В руках вещь, не нужная ни одному заданию (сторож вынул из шкафа чужое, приманка больше не нужна), — кладём её.
	// Иначе бот носил бы её до конца раунда: задания с вещами за чужую вещь не берутся.
	UCarryComponent* Carry = Me->GetCarryComponent();
	if (AItemActor* Carried = Carry->GetCarriedItem())
	{
		bool bWanted = false;
		for (int32 Index = 0; Index < Rows.Num() && !bWanted; ++Index)
		{
			bWanted = States.IsValidIndex(Index) && States[Index].Status == ETaskStatus::InProgress && RowWantsItem(Rows[Index], Carried);
		}
		if (!bWanted)
		{
			UE_LOG(LogObshaga, Verbose, TEXT("Bot %s puts down %s: no task needs it"), *GetMyState()->GetPlayerName(), *Carried->GetName());
			Carry->Drop();
			IdleUntil = GetWorld()->GetTimeSeconds() + 0.5f;
			return true;
		}
	}

	// «Не попадись на глаза»: после отбоя любой выход в коридор уже подозрителен — осторожный бот бросает остальные
	// дела и сидит у себя до утра (прогулку домой сделает PlanWander).
	if (IsCarefulAtHomeTime())
	{
		return false;
	}
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		if (States.IsValidIndex(Index) && States[Index].Status == ETaskStatus::InProgress && PlanTaskRow(Rows[Index]))
		{
			return true;
		}
	}
	return false;
}

bool AObshagaBotController::PlanTaskRow(const FTaskRow& Row)
{
	const AObshagaGameState* GameState = GetWorld()->GetGameState<AObshagaGameState>();
	AObshagaPlayerState* MyState = GetMyState();
	UCarryComponent* Carry = Me->GetCarryComponent();
	const float Now = GetWorld()->GetTimeSeconds();
	const AKomendantCharacter* Komendant = FindKomendant();
	const float KomendantDistance = Komendant ? FVector::Dist(Komendant->GetActorLocation(), Me->GetActorLocation()) : 100000.f;
	auto GaveUp = [this](const AActor* Actor) { return IsGivenUp(Actor); };
	// Что в руках. Задание с вещью берётся за дело, только если это его вещь (иначе её несёт другое задание бота).
	AItemActor* Carried = Carry->GetCarriedItem();

	switch (Row.SuccessCondition)
	{
	case ETaskCondition::ItemInRoomAtEnd:
	{
		// Унести предмет (телевизор) к себе в комнату (или в комнату задания) и спрятать там, если есть куда.
		const FName Room = (Row.bUseOwnRoom || Row.RoomId.IsNone()) ? MyState->GetHomeRoomId() : Row.RoomId;
		if (Carried)
		{
			if (!RowWantsItem(Row, Carried))
			{
				return false;
			}
			CarryRoom = Room;
			AHidingSpot* Spot = FindSpotInRoom(Room, false);
			if (Spot && !GaveUp(Spot))
			{
				TargetSpot = Spot;
				return WalkTo(Spot->GetActorLocation(), EBotGoal::HideItem, 150.f);
			}
			return WalkTo(RoomPoint(Room), EBotGoal::Drop, 120.f);
		}
		AItemActor* Item = FindItem(Row.ItemId, false, false);
		if (!Item || GaveUp(Item))
		{
			return false;
		}
		// Лежит или спрятан у нас в комнате — дело сделано.
		if (Item->GetCurrentRoomId() == Room)
		{
			return false;
		}
		// Спрятан где-то ещё (не нами): если бот видел, в какой комнате его прятали, — обыскивает там тайники;
		// иначе искать негде (сквозь дверцы шкафов бот не видит).
		if (Item->GetItemState() == EItemState::Hidden)
		{
			FName SeenRoom;
			for (const FBotSighting& Seen : Sightings)
			{
				if (Seen.Item.Get() == Item && Seen.What == EBotSeen::Hidden)
				{
					SeenRoom = Seen.RoomId;
				}
			}
			return PlanSearchRoom(SeenRoom);
		}
		// Тащить на глазах у Палыча — нет.
		if (KomendantDistance < GetRoundConfig()->BotFleeDistance * 1.2f)
		{
			return false;
		}
		TargetItem = Item;
		return WalkTo(Item->GetActorLocation(), EBotGoal::PickUp, 150.f);
	}

	case ETaskCondition::ContrabandSurvivedInspection:
	{
		// Спрятать запрещёнку. Где — решается раз за раунд: чаще в общем месте (санузел, кухня, гостиная) — туда утренний
		// обыск не ходит, хотя Палыч может заглянуть и на обходе; иначе у себя в комнате, как раньше.
		if (!bHideOutsideDecided)
		{
			bHideOutsideDecided = true;
			bHideOutside = FMath::FRand() < GetRoundConfig()->BotHideOutsideBedroomChance;
		}
		const FName Home = MyState->GetHomeRoomId();
		AHidingSpot* Spot = bHideOutside ? FindSpotOutsideBedrooms() : nullptr;
		if (!Spot)
		{
			Spot = FindSpotInRoom(Home, false);
		}
		if (Spot && GaveUp(Spot))
		{
			Spot = nullptr;
		}
		if (Carried)
		{
			if (!RowWantsItem(Row, Carried))
			{
				return false;
			}
			CarryRoom = Home;
			if (Spot)
			{
				TargetSpot = Spot;
				return WalkTo(Spot->GetActorLocation(), EBotGoal::HideItem, 150.f);
			}
			return WalkTo(RoomPoint(Home), EBotGoal::Drop, 120.f);
		}
		// Своя запрещёнка уже в тайнике — дело сделано. Спрятанную кем-то другим бот не трогает (и не считает своей):
		// тогда у этого задания дела нет, и бот займётся другим.
		for (TActorIterator<AItemActor> It(GetWorld()); It; ++It)
		{
			if (It->GetItemData()->bContraband && It->GetItemState() == EItemState::Hidden && It->GetLastHiddenBy() == MyState)
			{
				return false;
			}
		}
		AItemActor* Item = FindItem(NAME_None, true, false, true);
		if (!Item || GaveUp(Item))
		{
			return false;
		}
		// Лежит у себя, а спрятать некуда — так и оставим (иначе бот поднимал бы её и клал обратно по кругу).
		if (!Spot && Item->GetCurrentRoomId() == Home)
		{
			return false;
		}
		// Тащить на глазах у Палыча — нет.
		if (KomendantDistance < GetRoundConfig()->BotFleeDistance * 1.2f)
		{
			return false;
		}
		TargetItem = Item;
		return WalkTo(Item->GetActorLocation(), EBotGoal::PickUp, 150.f);
	}

	case ETaskCondition::ItemNotMovedFromZone:
	{
		// Сторожить предмет: если его унесли и бросили — вернуть на место; иначе прогуливаться рядом.
		if (Carried)
		{
			if (!RowWantsItem(Row, Carried))
			{
				return false;
			}
			CarryRoom = Row.RoomId;
			return WalkTo(RoomPoint(Row.RoomId), EBotGoal::Drop, 120.f);
		}
		AItemActor* Item = FindItem(Row.ItemId, false, false);
		if (Item && !GaveUp(Item) && Item->GetItemState() == EItemState::World && Item->GetCurrentRoomId() != Row.RoomId)
		{
			TargetItem = Item;
			return WalkTo(Item->GetActorLocation(), EBotGoal::PickUp, 150.f);
		}
		// Предмет спрятан: обыскиваем тайники по одному там, где видели, как его прятали, а нет — в комнате известного вора.
		if (Item && Item->GetItemState() == EItemState::Hidden)
		{
			FName SearchRoom;
			for (const FBotSighting& Seen : Sightings)
			{
				if (Seen.Item.Get() == Item && Seen.What == EBotSeen::Hidden)
				{
					SearchRoom = Seen.RoomId;
				}
			}
			const AObshagaPlayerState* Thief = Cast<AObshagaPlayerState>(KnownThief.Get());
			if (SearchRoom.IsNone() && Thief)
			{
				SearchRoom = Thief->GetHomeRoomId();
			}
			if (PlanSearchRoom(SearchRoom))
			{
				return true;
			}
		}
		// Утром сторож не уходит из гостиной: пусть Палыч видит, что телевизор под присмотром.
		const bool bMorning = GameState->GetPhase() == ERoundPhase::Morning;
		if (bMorning || FMath::FRand() < 0.5f)
		{
			const FVector Point = RoomPoint(Row.RoomId);
			return !Point.IsZero() && WalkTo(Point + FVector(FMath::FRandRange(-150.f, 150.f), FMath::FRandRange(-150.f, 150.f), 0.f), EBotGoal::Wander, 160.f);
		}
		return false;
	}

	case ETaskCondition::DeviceBrokenAndNotSeen:
	{
		// Сломать прибор, пока Палыча рядом нет.
		ADeviceActor* Device = FindDevice(false);
		if (!Device || FindDevice(true) || KomendantDistance < 1500.f || GaveUp(Device) || Now - RoundStartTime < GetRoundConfig()->BotSabotageDelaySeconds)
		{
			return false;
		}
		TargetDevice = Device;
		return WalkTo(Device->GetActorLocation(), EBotGoal::BreakDevice, 160.f);
	}

	case ETaskCondition::DeviceRepaired:
	{
		// Чинить идёт только тот, кто знает о поломке: видел сломанный прибор сам. Иначе изредка заглядывает на кухню.
		ADeviceActor* Device = FindDevice(true);
		// «Видит» — штатной проверкой контроллера: луч, упёршийся в сам прибор, считается видимостью
		// (своя проверка линии принимала прибор за стену, и поломку не замечал никто).
		if (Device && !GaveUp(Device) && (bKnowsBroken || (FVector::Dist(Device->GetActorLocation(), Me->GetActorLocation()) < 900.f && LineOfSightTo(Device))))
		{
			bKnowsBroken = true;
			TargetDevice = Device;
			return WalkTo(Device->GetActorLocation(), EBotGoal::RepairDevice, 160.f);
		}
		ADeviceActor* Any = Device ? Device : FindDevice(false);
		if (Any && FMath::FRand() < GetRoundConfig()->BotFixCheckChance)
		{
			return WalkTo(Any->GetActorLocation() + FVector(FMath::FRandRange(-200.f, 200.f), FMath::FRandRange(-200.f, 200.f), 0.f), EBotGoal::Wander, 200.f);
		}
		return false;
	}

	case ETaskCondition::NoteReadByDistinctPlayers:
	{
		// Пустить слух: взять записку и оставить её там, где её прочтут, — в гостиной или на кухне, где больше народу.
		if (bRumorPlanted)
		{
			return false;
		}
		if (Carried)
		{
			// В руках что-то другое — записку взять нечем (раньше бот ходил к ней снова и снова).
			if (!RowWantsItem(Row, Carried))
			{
				return false;
			}
			TArray<FVector> Crowded;
			int32 MostNeighbours = -1;
			for (const AKomendantWaypoint* Waypoint : Waypoints)
			{
				const FVector At = Waypoint->GetActorLocation();
				const ARoomVolume* Room = ARoomVolume::FindRoomAt(this, At);
				if (!Room || (Room->RoomType != ERoomType::LivingRoom && Room->RoomType != ERoomType::Kitchen))
				{
					continue;
				}
				int32 Neighbours = 0;
				for (TActorIterator<AObshagaCharacter> It(GetWorld()); It; ++It)
				{
					const APlayerState* OtherState = It->GetPlayerState();
					if (*It != Me && OtherState && !It->IsGhost() && !It->IsHiding() && FVector::Dist(It->GetActorLocation(), At) < BotRumorCrowdDistance
						&& FMath::Abs(It->GetActorLocation().Z - At.Z) < BotSameFloorHeight * 2.f)
					{
						++Neighbours;
					}
				}
				if (Neighbours > MostNeighbours)
				{
					MostNeighbours = Neighbours;
					Crowded.Reset();
				}
				if (Neighbours == MostNeighbours)
				{
					Crowded.Add(At);
				}
			}
			// Ни гостиной, ни кухни на карте — по-старому, в любом коридоре.
			if (Crowded.IsEmpty())
			{
				for (const AKomendantWaypoint* Waypoint : Waypoints)
				{
					const ARoomVolume* Room = ARoomVolume::FindRoomAt(this, Waypoint->GetActorLocation());
					if (Room && Room->RoomType == ERoomType::Corridor)
					{
						Crowded.Add(Waypoint->GetActorLocation());
					}
				}
			}
			return !Crowded.IsEmpty() && WalkTo(Crowded[FMath::RandRange(0, Crowded.Num() - 1)], EBotGoal::Drop, 120.f);
		}
		AItemActor* Note = FindItem(Row.ItemId, false, true, true);
		if (!Note || GaveUp(Note))
		{
			return false;
		}
		TargetItem = Note;
		return WalkTo(Note->GetActorLocation(), EBotGoal::PickUp, 150.f);
	}

	case ETaskCondition::LeftBuildingAndReturnedUnseen:
	{
		// Ночью выйти через ночной выход, погулять и вернуться.
		// До утра должно хватить времени дойти, погулять и вернуться.
		if (bCurfewDone || GameState->GetPhase() != ERoundPhase::Night || GameState->GetPhaseRemainingSeconds() < GetRoundConfig()->BotOutsideSeconds + 35.f
			|| Carry->IsCarrying() || KomendantDistance < 1500.f)
		{
			return false;
		}
		for (TActorIterator<ANightExitDoor> It(GetWorld()); It; ++It)
		{
			if (!It->GetHiddenPlayer() && !GaveUp(*It))
			{
				TargetSpot = *It;
				return WalkTo(It->GetActorLocation(), EBotGoal::GoOutside, 170.f);
			}
		}
		return false;
	}

	case ETaskCondition::CorrectAccusation:
		return PlanAccuse(Row);

	case ETaskCondition::InspectionFoundContrabandInRoom:
		return PlanTipRoom();

	case ETaskCondition::ItemPlantedInRoomOfPlayer:
	case ETaskCondition::PlayerCaughtWithItem:
		return PlanFrame(Row);

	case ETaskCondition::NoiseLuredKomendantAndUnseen:
		return PlanLure();

	default:
		// «Ни разу не попасться» — это не действие, а манера: просто осторожнее держаться от Палыча.
		// «Прикрой друга» бот делает на допросе (HandleInterrogation), а не по плану.
		return false;
	}
}

bool AObshagaBotController::IsCarefulAtHomeTime() const
{
	// Домой — заранее, за BotCurfewHomeSeconds до отбоя: идти к себе уже ночью по коридору — тоже «подозрительно».
	const AObshagaGameState* GameState = GetWorld()->GetGameState<AObshagaGameState>();
	const bool bNearCurfew = GameState && (GameState->GetPhase() != ERoundPhase::Evening || GameState->GetPhaseRemainingSeconds() < BotCurfewHomeSeconds);
	return bNearCurfew && IsCareful();
}

void AObshagaBotController::PlanWander()
{
	const UObshagaRoundConfig* Config = GetRoundConfig();
	// «Не попадись на глаза»: такой бот по общаге не гуляет, а сидит у себя в комнате.
	if (IsCarefulAtHomeTime() || (IsCareful() && FMath::FRand() < 0.8f))
	{
		const ARoomVolume* Home = ARoomVolume::FindRoomById(this, GetMyState()->GetHomeRoomId());
		const ARoomVolume* Here = ARoomVolume::FindRoomAt(this, Me->GetActorLocation());
		if (Home && Here != Home && WalkTo(RoomPoint(Home->RoomId), EBotGoal::Wander, 150.f))
		{
			return;
		}
		IdleUntil = GetWorld()->GetTimeSeconds() + FMath::FRandRange(Config->BotIdleMaxSeconds, Config->BotIdleMaxSeconds * 3.f);
		return;
	}
	// Прогулка: случайная точка на своём этаже, чаще в коридоре и общих комнатах.
	TArray<AKomendantWaypoint*> Candidates;
	for (AKomendantWaypoint* Waypoint : Waypoints)
	{
		if (Waypoint->bPatrolTarget && FVector::Dist(Waypoint->GetActorLocation(), Me->GetActorLocation()) > 200.f)
		{
			Candidates.Add(Waypoint);
		}
	}
	if (FMath::FRand() < BotEmoteChance)
	{
		Me->TryEmote(FMath::RandRange(0, 7));
	}
	if (Candidates.IsEmpty() || FMath::FRand() < 0.35f)
	{
		IdleUntil = GetWorld()->GetTimeSeconds() + FMath::FRandRange(Config->BotIdleMinSeconds, FMath::Max(Config->BotIdleMinSeconds, Config->BotIdleMaxSeconds));
		return;
	}
	WalkTo(Candidates[FMath::RandRange(0, Candidates.Num() - 1)]->GetActorLocation(), EBotGoal::Wander, 120.f);
}

bool AObshagaBotController::WalkTo(const FVector& Location, EBotGoal NewGoal, float ArriveRadius)
{
	if (Location.IsZero() || !PlanPathTo(Location))
	{
		return false;
	}
	Goal = NewGoal;
	GoalLocation = Location;
	GoalRadius = ArriveRadius;
	WalkTime = 0.f;
	NoProgressTime = 0.f;
	if (State != EBotState::Flee)
	{
		Me->SetSprinting(false);
	}
	SetState(EBotState::Walk);
	return true;
}

void AObshagaBotController::Arrive()
{
	const UObshagaRoundConfig* Config = GetRoundConfig();
	const float Now = GetWorld()->GetTimeSeconds();
	UCarryComponent* Carry = Me->GetCarryComponent();
	const EBotGoal Done = Goal;
	Goal = EBotGoal::None;
	Path.Reset();
	Me->SetSprinting(false);
	SetState(EBotState::Idle);
	IdleUntil = Now + FMath::FRandRange(0.3f, 1.5f);
	// Цели этого похода забираем себе и сразу забываем: иначе «не дошёл» следующего похода записалось бы
	// на эту, уже достигнутую цель (и бот, например, перестал бы брать телевизор, который только что донёс).
	AItemActor* ItemTarget = TargetItem.Get();
	AHidingSpot* SpotTarget = TargetSpot.Get();
	ADeviceActor* DeviceTarget = TargetDevice.Get();
	TargetItem.Reset();
	TargetSpot.Reset();
	TargetDevice.Reset();

	switch (Done)
	{
	case EBotGoal::PickUp:
		if (AItemActor* Item = ItemTarget)
		{
			if (Item->GetItemState() == EItemState::World && FVector::Dist(Item->GetActorLocation(), Me->GetActorLocation()) < 260.f)
			{
				Item->Interact(Me);
				if (Me->GetCarryComponent()->GetCarriedItem() == Item)
				{
					LastWorkedOn = Item;
				}
				if (Item->GetItemData()->bHeavy && Me->GetCarryComponent()->GetCarriedItem() == Item)
				{
					Say(static_cast<uint8>(ObshagaStory::EBotLine::BragTV));
				}
				UE_LOG(LogObshaga, Verbose, TEXT("Bot %s picked up %s (%s)"), *GetMyState()->GetPlayerName(), *Item->GetItemData()->ItemId.ToString(), *Item->GetName());
			}
		}
		break;
	case EBotGoal::Drop:
		if (Carry->IsCarrying())
		{
			const bool bNote = Carry->GetCarriedItem()->GetItemData()->bReadable;
			UE_LOG(LogObshaga, Verbose, TEXT("Bot %s dropped %s"), *GetMyState()->GetPlayerName(), *Carry->GetCarriedItem()->GetName());
			AItemActor* Dropped = Carry->GetCarriedItem();
			Carry->Drop();
			bRumorPlanted |= bNote;
			if (bNote)
			{
				// Свою записку бот не читает — ни здесь, ни если её потом переложат.
				FBotNoteRoll Own;
				Own.Location = Dropped->GetActorLocation();
				Own.bRead = true;
				NoteRolls.Add(TWeakObjectPtr<AItemActor>(Dropped), Own);
			}
		}
		break;
	case EBotGoal::SearchSpot:
		if (AHidingSpot* Spot = SpotTarget)
		{
			if (!Carry->IsCarrying() && !Spot->IsBusy())
			{
				// С пустыми руками E у тайника — обыскать; найденное само окажется в руках.
				Spot->Interact(Me);
				SetState(EBotState::Act);
				ActUntil = Now + 2.5f;
				UE_LOG(LogObshaga, Verbose, TEXT("Bot %s searches %s"), *GetMyState()->GetPlayerName(), *Spot->GetName());
			}
		}
		break;
	case EBotGoal::ReadNote:
		if (AItemActor* Note = ItemTarget)
		{
			if (Note->GetItemState() == EItemState::World && FVector::Dist(Note->GetActorLocation(), Me->GetActorLocation()) < 260.f)
			{
				Note->SecondaryInteract(Me);
				// Прочитал: на этом месте больше не читает (переложат — решит заново).
				FBotNoteRoll& Roll = NoteRolls.FindOrAdd(TWeakObjectPtr<AItemActor>(Note));
				Roll.Location = Note->GetActorLocation();
				Roll.bRead = true;
				UE_LOG(LogObshaga, Verbose, TEXT("Bot %s read %s"), *GetMyState()->GetPlayerName(), *Note->GetName());
			}
		}
		break;
	case EBotGoal::HideItem:
		if (AHidingSpot* Spot = SpotTarget)
		{
			if (Carry->IsCarrying() && !Spot->IsBusy())
			{
				UE_LOG(LogObshaga, Verbose, TEXT("Bot %s hides %s in %s"), *GetMyState()->GetPlayerName(), *Carry->GetCarriedItem()->GetName(), *Spot->GetName());
				Spot->Interact(Me);
				SetState(EBotState::Act);
				ActUntil = Now + 3.f;
			}
		}
		break;
	case EBotGoal::HideSelf:
		if (AHidingSpot* Spot = SpotTarget)
		{
			if (!Spot->GetHiddenPlayer() && !Spot->IsBusy() && !Carry->IsCarrying())
			{
				Spot->SecondaryInteract(Me);
				if (Me->IsHiding())
				{
					MySpot = Spot;
					SetState(EBotState::Hidden);
					HideUntil = Now + FMath::FRandRange(Config->BotHideMinSeconds, FMath::Max(Config->BotHideMinSeconds, Config->BotHideMaxSeconds));
					UE_LOG(LogObshaga, Verbose, TEXT("Bot %s hid in %s"), *GetMyState()->GetPlayerName(), *Spot->GetName());
				}
			}
		}
		break;
	case EBotGoal::BreakDevice:
		// Пока шёл, плиту мог сломать кто-то другой (или её уже ломают) — тогда тут делать нечего.
		if (ADeviceActor* Device = DeviceTarget; Device && !Device->IsBroken() && !Device->IsBusy())
		{
			UE_LOG(LogObshaga, Verbose, TEXT("Bot %s breaks %s"), *GetMyState()->GetPlayerName(), *Device->GetName());
			LastWorkedOn = Device;
			Device->SecondaryInteract(Me);
			SetState(EBotState::Act);
			ActUntil = Now + Device->GetBreakDuration() + 0.5f;
		}
		break;
	case EBotGoal::RepairDevice:
		if (ADeviceActor* Device = DeviceTarget)
		{
			UE_LOG(LogObshaga, Verbose, TEXT("Bot %s repairs %s"), *GetMyState()->GetPlayerName(), *Device->GetName());
			LastWorkedOn = Device;
			Device->Interact(Me);
			SetState(EBotState::Act);
			ActUntil = Now + Device->GetRepairDuration() + 0.5f;
		}
		break;
	case EBotGoal::GoOutside:
		if (AHidingSpot* Door = SpotTarget)
		{
			Door->Interact(Me);
			UE_LOG(LogObshaga, Verbose, TEXT("Bot %s goes outside: %d"), *GetMyState()->GetPlayerName(), Me->IsHiding() ? 1 : 0);
			if (Me->IsHiding())
			{
				MySpot = Door;
				SetState(EBotState::Outside);
				OutsideUntil = Now + Config->BotOutsideSeconds;
			}
		}
		break;
	case EBotGoal::LeaveForever:
		// Выселенный бот ушёл: персонаж исчезает, карточка игрока остаётся для итогов.
		if (APawn* Body = GetPawn())
		{
			const AObshagaGameMode* LeaveMode = GetWorld()->GetAuthGameMode<AObshagaGameMode>();
			UE_LOG(LogObshaga, Verbose, TEXT("Evicted bot %s left the building, t=%.1f"), *GetMyState()->GetPlayerName(), LeaveMode ? LeaveMode->GetRoundSeconds() : 0.f);
			UnPossess();
			Body->Destroy();
			Me = nullptr;
		}
		break;
	case EBotGoal::Accuse:
		TryAccuse();
		break;
	case EBotGoal::TipRoom:
		TryTipRoom();
		break;
	case EBotGoal::Lure:
		TryLureThrow();
		break;
	default:
		// Alibi: дошёл до допроса — подтверждает HandleInterrogation (он же решает, в радиусе ли бот).
		break;
	}
}

AItemActor* AObshagaBotController::FindItem(FName ItemId, bool bContraband, bool bReadable, bool bWorldOnly) const
{
	AItemActor* Best = nullptr;
	float BestDistance = TNumericLimits<float>::Max();
	for (TActorIterator<AItemActor> It(GetWorld()); It; ++It)
	{
		const UObshagaItemData* Data = It->GetItemData();
		const bool bMatches = bContraband ? Data->bContraband : (bReadable ? Data->bReadable && (ItemId.IsNone() || Data->ItemId == ItemId) : Data->ItemId == ItemId);
		if (!bMatches)
		{
			continue;
		}
		// В чужих руках или в чужом тайнике предмет недоступен (кроме того, что в руках у самого бота).
		if (It->GetItemState() == EItemState::Carried && It->GetHolder() != Me)
		{
			continue;
		}
		if (bWorldOnly && It->GetItemState() != EItemState::World)
		{
			continue;
		}
		const float Distance = FVector::Dist(It->GetActorLocation(), Me->GetActorLocation());
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = *It;
		}
	}
	return Best;
}

AHidingSpot* AObshagaBotController::FindSpotInRoom(FName RoomId, bool bForPlayer) const
{
	for (TActorIterator<AHidingSpot> It(GetWorld()); It; ++It)
	{
		const ARoomVolume* Room = ARoomVolume::FindRoomAt(this, It->GetActorLocation() + FVector(0.f, 0.f, 50.f));
		if (Room && Room->RoomId == RoomId && !It->IsBusy() && !It->IsA<ANightExitDoor>()
			&& (bForPlayer ? It->CanHidePlayer() && !It->GetHiddenPlayer() : !It->GetHiddenItem()))
		{
			return *It;
		}
	}
	return nullptr;
}

ADeviceActor* AObshagaBotController::FindDevice(bool bBroken) const
{
	for (TActorIterator<ADeviceActor> It(GetWorld()); It; ++It)
	{
		if (It->IsBroken() == bBroken && !It->IsBusy())
		{
			return *It;
		}
	}
	return nullptr;
}

FVector AObshagaBotController::RoomPoint(FName RoomId) const
{
	const ARoomVolume* Room = ARoomVolume::FindRoomById(this, RoomId);
	if (!Room)
	{
		return FVector::ZeroVector;
	}
	const FVector Center = Room->GetRootComponent()->Bounds.Origin;
	const AKomendantWaypoint* Best = nullptr;
	float BestDistance = TNumericLimits<float>::Max();
	for (const AKomendantWaypoint* Waypoint : Waypoints)
	{
		const ARoomVolume* WaypointRoom = ARoomVolume::FindRoomAt(this, Waypoint->GetActorLocation());
		const float Distance = FVector::Dist(Waypoint->GetActorLocation(), Center);
		if (WaypointRoom == Room && Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = Waypoint;
		}
	}
	return Best ? Best->GetActorLocation() : FVector::ZeroVector;
}

bool AObshagaBotController::HasClearLine(const FVector& From, const FVector& To) const
{
	// Мешают только стены и перекрытия: мебель, двери, предметы и люди не в счёт.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(BotClearLine), false);
	for (TActorIterator<APawn> It(GetWorld()); It; ++It)
	{
		Params.AddIgnoredActor(*It);
	}
	for (TActorIterator<ADoorActor> It(GetWorld()); It; ++It)
	{
		Params.AddIgnoredActor(*It);
	}
	for (TActorIterator<AItemActor> It(GetWorld()); It; ++It)
	{
		Params.AddIgnoredActor(*It);
	}
	FHitResult Hit;
	return !GetWorld()->LineTraceSingleByChannel(Hit, From, To, ECC_Visibility, Params);
}

bool AObshagaBotController::HasClearWay(const FVector& From, const FVector& To) const
{
	FCollisionQueryParams Params(SCENE_QUERY_STAT(BotClearWay), false);
	for (TActorIterator<APawn> It(GetWorld()); It; ++It)
	{
		Params.AddIgnoredActor(*It);
	}
	for (TActorIterator<ADoorActor> It(GetWorld()); It; ++It)
	{
		Params.AddIgnoredActor(*It);
	}
	for (TActorIterator<AItemActor> It(GetWorld()); It; ++It)
	{
		Params.AddIgnoredActor(*It);
	}
	const float Radius = Me ? Me->GetCapsuleComponent()->GetScaledCapsuleRadius() + 6.f : 40.f;
	FHitResult Hit;
	return !GetWorld()->SweepSingleByChannel(Hit, From, To, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(Radius), Params);
}

AKomendantWaypoint* AObshagaBotController::FindNearestWaypoint(const FVector& Location) const
{
	AKomendantWaypoint* Best = nullptr;
	float BestScore = TNumericLimits<float>::Max();
	for (AKomendantWaypoint* Waypoint : Waypoints)
	{
		const FVector Delta = Waypoint->GetActorLocation() - Location;
		// Точка на другом этаже «далеко», даже прямо над головой; точка за стеной хуже видимой.
		float Score = Delta.Size() + FMath::Abs(Delta.Z) * 3.f + (FMath::Abs(Delta.Z) > BotSameFloorHeight ? 100000.f : 0.f);
		if (Score < BestScore && !HasClearLine(Waypoint->GetActorLocation(), Location))
		{
			Score += 10000.f;
		}
		// Тонкий луч проходит мимо цветка или коробки, а плечо жильца в них упирается.
		else if (Score < BestScore && !HasClearWay(Location, Waypoint->GetActorLocation()))
		{
			Score += 3000.f;
		}
		if (Score < BestScore)
		{
			BestScore = Score;
			Best = Waypoint;
		}
	}
	return Best;
}

bool AObshagaBotController::PlanPathTo(const FVector& Target)
{
	Path.Reset();
	PathIndex = 0;
	PathBestDistance = TNumericLimits<float>::Max();
	NoProgressTime = 0.f;

	AKomendantWaypoint* Start = FindNearestWaypoint(Me->GetActorLocation());
	AKomendantWaypoint* End = FindNearestWaypoint(Target);
	if (!Start || !End)
	{
		return false;
	}

	// Кратчайший путь по графу точек (Дейкстра; точек мало).
	TMap<AKomendantWaypoint*, float> Distance;
	TMap<AKomendantWaypoint*, AKomendantWaypoint*> Previous;
	TArray<AKomendantWaypoint*> Open;
	Distance.Add(Start, 0.f);
	Open.Add(Start);
	while (!Open.IsEmpty())
	{
		int32 BestIndex = 0;
		for (int32 Index = 1; Index < Open.Num(); ++Index)
		{
			if (Distance[Open[Index]] < Distance[Open[BestIndex]])
			{
				BestIndex = Index;
			}
		}
		AKomendantWaypoint* Current = Open[BestIndex];
		Open.RemoveAtSwap(BestIndex);
		if (Current == End)
		{
			break;
		}
		for (const TWeakObjectPtr<AKomendantWaypoint>& NeighborPtr : Current->Neighbors)
		{
			AKomendantWaypoint* Neighbor = NeighborPtr.Get();
			if (!Neighbor)
			{
				continue;
			}
			const float NewDistance = Distance[Current] + FVector::Dist(Current->GetActorLocation(), Neighbor->GetActorLocation());
			const float* Known = Distance.Find(Neighbor);
			if (!Known || NewDistance < *Known)
			{
				Distance.Add(Neighbor, NewDistance);
				Previous.Add(Neighbor, Current);
				Open.AddUnique(Neighbor);
			}
		}
	}
	if (!Distance.Contains(End))
	{
		return false;
	}
	for (AKomendantWaypoint* Step = End; Step; Step = Previous.FindRef(Step))
	{
		Path.Insert(Step->GetActorLocation(), 0);
	}
	// Первая точка пути позади — если до второй можно пройти напрямую, первую пропускаем, чтобы не шагать назад.
	// Проверяем шаром толщиной с жильца, а не тонким лучом: луч проходил в дверной проём наискосок, а сам жилец
	// цеплялся плечом за косяк и застревал.
	if (Path.Num() > 1 && HasClearWay(Me->GetActorLocation(), Path[1]))
	{
		Path.RemoveAt(0);
	}
	ADoorActor::ThreadDoorways(GetWorld(), Path, Me->GetActorLocation());
	return true;
}

bool AObshagaBotController::FollowPath(float DeltaSeconds)
{
	if (!Path.IsValidIndex(PathIndex))
	{
		return true;
	}
	const FVector Point = Path[PathIndex];
	const FVector ToPoint = Point - Me->GetActorLocation();
	if (ToPoint.Size2D() < BotPointArriveDistance && FMath::Abs(ToPoint.Z) < BotSameFloorHeight)
	{
		++PathIndex;
		PathBestDistance = TNumericLimits<float>::Max();
		NoProgressTime = 0.f;
		return !Path.IsValidIndex(PathIndex);
	}
	Me->AddMovementInput(FVector(ToPoint.X, ToPoint.Y, 0.f).GetSafeNormal());

	// Страховка, как у Палыча: упёрся и не приближается — переносим на точку пути.
	const float Distance = ToPoint.Size2D();
	if (Distance < PathBestDistance - 15.f)
	{
		PathBestDistance = Distance;
		NoProgressTime = 0.f;
	}
	else
	{
		NoProgressTime += DeltaSeconds;
		if (NoProgressTime > BotStuckSeconds * 0.4f)
		{
			// Сначала пробуем обойти препятствие боком (встречный жилец, угол мебели, створка двери).
			const FVector Side = FVector::CrossProduct(FVector::UpVector, ToPoint.GetSafeNormal2D()) * (FMath::Fmod(NoProgressTime, 1.6f) < 0.8f ? 1.f : -1.f);
			Me->AddMovementInput(Side);
		}
		if (NoProgressTime > BotStuckSeconds)
		{
			NoProgressTime = 0.f;
			// Что мешает: капсулой на полметра вперёд, к точке. Других ботов бот проходит насквозь — их и здесь не считаем.
			FHitResult Block;
			FCollisionQueryParams BlockParams(SCENE_QUERY_STAT(BotStuck), false, Me);
			FCollisionResponseParams BlockResponse;
			Me->GetCapsuleComponent()->InitSweepCollisionParams(BlockParams, BlockResponse);
			const FVector From = Me->GetActorLocation();
			GetWorld()->SweepSingleByChannel(Block, From, From + ToPoint.GetSafeNormal2D() * 60.f, FQuat::Identity, ECC_Pawn,
				FCollisionShape::MakeCapsule(Me->GetCapsuleComponent()->GetScaledCapsuleRadius(), Me->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 0.8f), BlockParams, BlockResponse);

			// Дорогу загородил человек или Палыч (встал в дверях) — сквозь него не переносим, иначе оба застревали
			// друг в друге. Бот и дальше обходит его боком, а не дойдёт — бросит цель, как обычно.
			if (Cast<APawn>(Block.GetActor()))
			{
				UE_LOG(LogObshaga, Verbose, TEXT("Bot %s blocked by %s on the way to %s, side-stepping"), GetMyState() ? *GetMyState()->GetPlayerName() : TEXT("-"),
					*GetNameSafe(Block.GetActor()), *Point.ToCompactString());
				return false;
			}

			UE_LOG(LogObshaga, Log, TEXT("Bot %s stuck near %s on the way to %s (point %d of %d), blocked by %s, snapping to waypoint"), GetMyState() ? *GetMyState()->GetPlayerName() : TEXT("-"),
				*Me->GetActorLocation().ToCompactString(), *Point.ToCompactString(), PathIndex + 1, Path.Num(), *GetNameSafe(Block.GetActor()));
			// TeleportTo не ставит внутрь мебели или чужой капсулы; свободного места у точки нет — переносить некуда.
			if (!Me->TeleportTo(Point, Me->GetActorRotation()))
			{
				UE_LOG(LogObshaga, Verbose, TEXT("Bot %s: no free room at %s, snap skipped"), GetMyState() ? *GetMyState()->GetPlayerName() : TEXT("-"), *Point.ToCompactString());
			}
			if (AObshagaGameMode* GameMode = GetWorld()->GetAuthGameMode<AObshagaGameMode>())
			{
				GameMode->ReportStuck(false);
			}
		}
	}
	return false;
}

void AObshagaBotController::OpenDoorsNearby()
{
	const FVector Location = Me->GetActorLocation();
	for (TActorIterator<ADoorActor> It(GetWorld()); It; ++It)
	{
		if (!It->IsOpen() && FVector::Dist(It->GetDoorCenter(), Location) < BotDoorOpenDistance)
		{
			It->OpenFor(Location);
		}
	}
}

// --- Задания-помощники: что брать, куда прятать, где искать ---

bool AObshagaBotController::RowWantsItem(const FTaskRow& Row, const AItemActor* Item) const
{
	const UObshagaItemData* Data = Item ? Item->GetItemData() : nullptr;
	if (!Data)
	{
		return false;
	}
	switch (Row.SuccessCondition)
	{
	case ETaskCondition::ItemInRoomAtEnd:
	case ETaskCondition::ItemNotMovedFromZone:
		return Data->ItemId == Row.ItemId;
	case ETaskCondition::ContrabandSurvivedInspection:
		return Data->bContraband;
	case ETaskCondition::ItemPlantedInRoomOfPlayer:
	case ETaskCondition::PlayerCaughtWithItem:
		return !bPlanted && (Row.ItemId.IsNone() ? Data->bContraband : Data->ItemId == Row.ItemId);
	case ETaskCondition::NoteReadByDistinctPlayers:
		return !bRumorPlanted && Data->bReadable && (Row.ItemId.IsNone() || Data->ItemId == Row.ItemId);
	case ETaskCondition::NoiseLuredKomendantAndUnseen:
		return !bLureDone && LureThrowTime < 0.f && IsLureItem(Item);
	default:
		return false;
	}
}

AHidingSpot* AObshagaBotController::FindSpotOutsideBedrooms() const
{
	// Санузел, кухня, гостиная: утренний обыск идёт по жилым комнатам, и вещь там не «чья-то».
	AHidingSpot* Best = nullptr;
	float BestDistance = TNumericLimits<float>::Max();
	for (TActorIterator<AHidingSpot> It(GetWorld()); It; ++It)
	{
		if (It->IsBusy() || It->IsA<ANightExitDoor>() || It->GetHiddenPlayer() || It->GetHiddenItem() || IsGivenUp(*It))
		{
			continue;
		}
		const ARoomVolume* Room = ARoomVolume::FindRoomAt(this, It->GetActorLocation() + FVector(0.f, 0.f, 50.f));
		if (!Room || (Room->RoomType != ERoomType::Bathroom && Room->RoomType != ERoomType::Kitchen && Room->RoomType != ERoomType::LivingRoom))
		{
			continue;
		}
		const float Distance = FVector::Dist(It->GetActorLocation(), Me->GetActorLocation());
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = *It;
		}
	}
	return Best;
}

bool AObshagaBotController::PlanSearchRoom(FName RoomId)
{
	// Обыскивает тайники по одному (каждый — один раз за раунд): найденное само окажется в руках.
	if (RoomId.IsNone() || Me->GetCarryComponent()->IsCarrying())
	{
		return false;
	}
	for (TActorIterator<AHidingSpot> It(GetWorld()); It; ++It)
	{
		const ARoomVolume* SpotRoom = ARoomVolume::FindRoomAt(this, It->GetActorLocation() + FVector(0.f, 0.f, 50.f));
		if (SpotRoom && SpotRoom->RoomId == RoomId && !SearchedSpots.Contains(*It) && !It->IsBusy() && !It->IsA<ANightExitDoor>()
			&& !It->GetHiddenPlayer() && !IsGivenUp(*It))
		{
			SearchedSpots.Add(*It);
			TargetSpot = *It;
			if (WalkTo(It->GetActorLocation(), EBotGoal::SearchSpot, 150.f))
			{
				return true;
			}
		}
	}
	return false;
}

// --- Память: что бот видел сам и что слышал в СМС ---

void AObshagaBotController::OnGameEvent(const FGameEvent& Event)
{
	AObshagaPlayerState* MyState = GetMyState();
	const AObshagaGameState* GameState = GetWorld()->GetGameState<AObshagaGameState>();
	if (!Me || !IsValid(Me) || !MyState || !GameState || GameState->GetRoundState() != ERoundState::InProgress)
	{
		return;
	}
	APlayerState* Who = Event.Instigator;
	AItemActor* Item = Event.Item;
	const float Now = GetWorld()->GetTimeSeconds();
	const UTaskComponent* Tasks = MyState->GetTaskComponent();

	// Свои события: удалась ли ложная тревога, подброшена ли вещь.
	if (Who == MyState)
	{
		if (Event.Type == EGameEventType::KomendantAlerted && !bLureDone && Tasks->FindRowByCondition(ETaskCondition::NoiseLuredKomendantAndUnseen))
		{
			bLureDone = true;
			UE_LOG(LogObshaga, Verbose, TEXT("Bot %s lure result: alerted after %.1f s, t=%.1f"), *MyState->GetPlayerName(),
				LureThrowTime >= 0.f ? Now - LureThrowTime : -1.f, GetRoundSeconds());
			LureThrowTime = -1.f;
		}
		else if ((Event.Type == EGameEventType::ItemHidden || Event.Type == EGameEventType::ItemDropped) && Item && !bPlanted && !FrameRoom.IsNone())
		{
			const FTaskRow* FrameRow = Tasks->FindRowByCondition(ETaskCondition::ItemPlantedInRoomOfPlayer);
			if (!FrameRow)
			{
				FrameRow = Tasks->FindRowByCondition(ETaskCondition::PlayerCaughtWithItem);
			}
			const UObshagaItemData* Data = Item->GetItemData();
			const bool bFrameItem = FrameRow && (FrameRow->ItemId.IsNone() ? Data->bContraband : Data->ItemId == FrameRow->ItemId);
			if (bFrameItem && Item->GetCurrentRoomId() == FrameRoom)
			{
				bPlanted = true;
				PlantedRoom = FrameRoom;
				UE_LOG(LogObshaga, Verbose, TEXT("Bot %s planted %s (%s) in %s, room of %s, t=%.1f"), *MyState->GetPlayerName(), *Data->ItemId.ToString(), *Item->GetName(),
					*FrameRoom.ToString(), *GetNameSafe(FrameVictim.Get()), GetRoundSeconds());
			}
		}
		return;
	}

	// Чужие действия с вещами: запоминаем, только если бот это видел (рядом, на своём этаже, в прямой видимости).
	EBotSeen What = EBotSeen::Carrying;
	switch (Event.Type)
	{
	case EGameEventType::ItemPickedUp:
		What = EBotSeen::PickedUp;
		break;
	case EGameEventType::ItemDropped:
		What = EBotSeen::Dropped;
		break;
	case EGameEventType::ItemThrown:
		What = EBotSeen::Thrown;
		break;
	case EGameEventType::ItemHidden:
		What = EBotSeen::Hidden;
		break;
	default:
		return;
	}
	if (!Who || !Item || Me->IsHiding() || Me->IsGhost() || !CanWitness(Who->GetPawn()))
	{
		return;
	}
	// Спрятанное помним по комнате тайника: туда потом идти обыскивать или стучать.
	Remember(Who, What, Item, What == EBotSeen::Hidden ? Item->GetCurrentRoomId() : Event.RoomId);
}

bool AObshagaBotController::CanWitness(const APawn* Other) const
{
	if (!Me || !Other)
	{
		return false;
	}
	const FVector Here = Me->GetActorLocation();
	const FVector There = Other->GetActorLocation();
	return FVector::Dist(Here, There) <= GetRoundConfig()->BotWitnessDistance && FMath::Abs(Here.Z - There.Z) < BotSameFloorHeight * 2.f && LineOfSightTo(Other);
}

void AObshagaBotController::Remember(APlayerState* Who, EBotSeen What, AItemActor* Item, FName RoomId)
{
	const float Now = GetWorld()->GetTimeSeconds();
	// Несёт ту же вещь по той же комнате — это та же запись, только свежее (иначе память забилась бы за минуту).
	if (What == EBotSeen::Carrying)
	{
		for (int32 Index = Sightings.Num() - 1; Index >= 0; --Index)
		{
			FBotSighting& Old = Sightings[Index];
			if (Old.Who.Get() == Who && Old.Item.Get() == Item)
			{
				if (Old.What == EBotSeen::Carrying && Old.RoomId == RoomId)
				{
					Old.Time = Now;
					return;
				}
				break;
			}
		}
	}
	if (Sightings.Num() >= BotMaxSightings)
	{
		Sightings.RemoveAt(0);
	}
	const UObshagaItemData* Data = Item->GetItemData();
	FBotSighting& Seen = Sightings.AddDefaulted_GetRef();
	Seen.Who = Who;
	Seen.What = What;
	Seen.Item = Item;
	Seen.ItemId = Data->ItemId;
	Seen.bContraband = Data->bContraband;
	Seen.bReadable = Data->bReadable;
	Seen.RoomId = RoomId;
	Seen.Time = Now;
}

void AObshagaBotController::HearRumor(APlayerState* Who, int32 Verb, FName RoomId)
{
	if (!Who)
	{
		return;
	}
	FBotRumor& Rumor = Rumors.AddDefaulted_GetRef();
	Rumor.Who = Who;
	Rumor.Verb = Verb;
	Rumor.RoomId = RoomId;
	Rumor.Time = GetWorld()->GetTimeSeconds();
	const AObshagaPlayerState* MyState = GetMyState();
	UE_LOG(LogObshaga, Verbose, TEXT("Bot %s heard rumor: %s verb %d room %s, t=%.1f"), MyState ? *MyState->GetPlayerName() : TEXT("-"), *Who->GetPlayerName(), Verb,
		*RoomId.ToString(), GetRoundSeconds());
}

bool AObshagaBotController::IsItemShownInRoom(FName ItemId, FName RoomId) const
{
	for (TActorIterator<AItemActor> It(GetWorld()); It; ++It)
	{
		if (It->GetItemData()->ItemId == ItemId && It->GetItemState() != EItemState::Hidden && It->GetCurrentRoomId() == RoomId)
		{
			return true;
		}
	}
	return false;
}

FBotSuspect AObshagaBotController::FindTvThief(const FTaskRow& Row) const
{
	// Вор — кто взял вещь в её комнате и вынес оттуда. Бот судит только по тому, что видел сам, и по своим СМС.
	const APlayerState* MyState = GetMyState();
	const FName TheftRoom = Row.RoomId;
	const float Now = GetWorld()->GetTimeSeconds();
	// Что вещи нет на месте — видно любому, кто заглянет в комнату.
	const bool bGone = !Row.ItemId.IsNone() && !TheftRoom.IsNone() && !IsItemShownInRoom(Row.ItemId, TheftRoom);
	FBotSuspect Best;
	float BestScore = -1.f;
	auto Consider = [&Best, &BestScore](APlayerState* Who, EBotEvidence Strength, float Score, const FString& Source)
	{
		if (Strength > Best.Strength || (Strength == Best.Strength && Score > BestScore))
		{
			Best.Who = Who;
			Best.Strength = Strength;
			Best.Source = Source;
			BestScore = Score;
		}
	};

	for (int32 Index = 0; Index < Sightings.Num(); ++Index)
	{
		const FBotSighting& Seen = Sightings[Index];
		APlayerState* Who = Seen.Who.Get();
		if (!Who || Who == MyState || (!Row.ItemId.IsNone() && Seen.ItemId != Row.ItemId))
		{
			continue;
		}
		// Тот же человек потом положил вещь обратно в её комнату — вернул, а не украл.
		// Первое, что бот видел, — как он поднял вещь не в её комнате, — значит, нёс обратно (сторож).
		bool bReturned = false;
		bool bTookOutside = false;
		bool bCarriedOut = false;
		bool bFirstSeen = true;
		for (int32 Other = 0; Other < Sightings.Num(); ++Other)
		{
			const FBotSighting& Later = Sightings[Other];
			if (Later.Who != Seen.Who || Later.Item != Seen.Item)
			{
				continue;
			}
			if (bFirstSeen)
			{
				bFirstSeen = false;
				bTookOutside = Later.What == EBotSeen::PickedUp && !TheftRoom.IsNone() && Later.RoomId != TheftRoom;
			}
			if (Other > Index && (Later.What == EBotSeen::Dropped || Later.What == EBotSeen::Hidden) && Later.RoomId == TheftRoom)
			{
				bReturned = true;
			}
			if (Other > Index && Later.What != EBotSeen::PickedUp && !Later.RoomId.IsNone() && Later.RoomId != TheftRoom)
			{
				bCarriedOut = true;
			}
		}
		if (bReturned || bTookOutside)
		{
			continue;
		}
		const float Ago = Now - Seen.Time;
		if (Seen.What == EBotSeen::PickedUp && Seen.RoomId == TheftRoom)
		{
			if (bCarriedOut)
			{
				Consider(Who, EBotEvidence::Strong, Seen.Time, FString::Printf(TEXT("saw pick up %s in %s and carry it out %.0f s ago"), *Seen.ItemId.ToString(), *TheftRoom.ToString(), Ago));
			}
			else if (bGone)
			{
				Consider(Who, EBotEvidence::Medium, Seen.Time, FString::Printf(TEXT("saw pick up %s in %s %.0f s ago, it is gone"), *Seen.ItemId.ToString(), *TheftRoom.ToString(), Ago));
			}
		}
		else if (Seen.What != EBotSeen::PickedUp && !Seen.RoomId.IsNone() && Seen.RoomId != TheftRoom)
		{
			Consider(Who, EBotEvidence::Medium, Seen.Time, FString::Printf(TEXT("saw %s %s in %s %.0f s ago"), SeenName(Seen.What), *Seen.ItemId.ToString(), *Seen.RoomId.ToString(), Ago));
		}
	}

	// СМС «кто-то что-то тащил» — слабая улика, и только когда вещи правда нет на месте. Слух про ту самую комнату — вероятнее.
	if (bGone)
	{
		for (const FBotRumor& Rumor : Rumors)
		{
			APlayerState* Who = Rumor.Who.Get();
			if (Who && Who != MyState && Rumor.Verb == 0)
			{
				const float Score = Rumor.Time + (Rumor.RoomId == TheftRoom ? 100000.f : 0.f);
				Consider(Who, EBotEvidence::Weak, Score, FString::Printf(TEXT("sms: carried something in %s %.0f s ago"), *Rumor.RoomId.ToString(), Now - Rumor.Time));
			}
		}
	}
	return Best;
}

FBotSuspect AObshagaBotController::FindNoteAuthor() const
{
	// Автор слуха — кто первым на глазах у бота оставил записку (положил, бросил, спрятал).
	const AObshagaPlayerState* MyState = GetMyState();
	const FTaskRow* Row = MyState->GetTaskComponent()->FindRowByCondition(ETaskCondition::CorrectAccusation);
	const FName NoteId = Row ? Row->ItemId : NAME_None;
	const float Now = GetWorld()->GetTimeSeconds();
	FBotSuspect Found;
	for (const FBotSighting& Seen : Sightings)
	{
		APlayerState* Who = Seen.Who.Get();
		if (!Who || Who == MyState || !Seen.bReadable || Seen.What == EBotSeen::PickedUp || Seen.What == EBotSeen::Carrying
			|| (!NoteId.IsNone() && Seen.ItemId != NoteId))
		{
			continue;
		}
		Found.Who = Who;
		Found.Strength = EBotEvidence::Strong;
		Found.Source = FString::Printf(TEXT("saw %s note in %s %.0f s ago"), SeenName(Seen.What), *Seen.RoomId.ToString(), Now - Seen.Time);
		break;
	}
	return Found;
}

FBotSuspect AObshagaBotController::FindContrabandRoom() const
{
	// Где запрещёнка: сам подбросил — знаю точно; видел, как прятали в чужой комнате, — твёрдо;
	// видел, как туда несли или там бросили, — средне; СМС «кто-то что-то прятал» — слабо.
	const AObshagaPlayerState* MyState = GetMyState();
	const FName Home = MyState->GetHomeRoomId();
	const float Now = GetWorld()->GetTimeSeconds();
	auto IsForeignBedroom = [this, Home](FName RoomId)
	{
		const ARoomVolume* Room = RoomId.IsNone() ? nullptr : ARoomVolume::FindRoomById(this, RoomId);
		return Room && Room->RoomType == ERoomType::Bedroom && RoomId != Home;
	};

	FBotSuspect Best;
	if (bPlanted && IsForeignBedroom(PlantedRoom))
	{
		Best.Who = FrameVictim;
		Best.RoomId = PlantedRoom;
		Best.Strength = EBotEvidence::Strong;
		Best.Source = TEXT("planted it myself");
		return Best;
	}
	float BestTime = -1.f;
	for (int32 Index = 0; Index < Sightings.Num(); ++Index)
	{
		const FBotSighting& Seen = Sightings[Index];
		if (!Seen.bContraband || Seen.Who.Get() == MyState || !IsForeignBedroom(Seen.RoomId))
		{
			continue;
		}
		// Потом её на глазах у бота подняли — там её уже нет.
		bool bMovedLater = false;
		for (int32 Other = Index + 1; Other < Sightings.Num() && !bMovedLater; ++Other)
		{
			bMovedLater = Sightings[Other].Item == Seen.Item && Sightings[Other].What == EBotSeen::PickedUp;
		}
		if (bMovedLater || Seen.What == EBotSeen::PickedUp)
		{
			continue;
		}
		const EBotEvidence Strength = Seen.What == EBotSeen::Hidden ? EBotEvidence::Strong : EBotEvidence::Medium;
		if (Strength > Best.Strength || (Strength == Best.Strength && Seen.Time > BestTime))
		{
			Best.Who = Seen.Who;
			Best.RoomId = Seen.RoomId;
			Best.Strength = Strength;
			Best.Source = FString::Printf(TEXT("saw %s %s in %s %.0f s ago"), SeenName(Seen.What), *Seen.ItemId.ToString(), *Seen.RoomId.ToString(), Now - Seen.Time);
			BestTime = Seen.Time;
		}
	}
	if (Best.Strength == EBotEvidence::None)
	{
		for (const FBotRumor& Rumor : Rumors)
		{
			if (Rumor.Verb == 1 && Rumor.Who.Get() != MyState && IsForeignBedroom(Rumor.RoomId) && Rumor.Time > BestTime)
			{
				Best.Who = Rumor.Who;
				Best.RoomId = Rumor.RoomId;
				Best.Strength = EBotEvidence::Weak;
				Best.Source = FString::Printf(TEXT("sms: hid something in %s %.0f s ago"), *Rumor.RoomId.ToString(), Now - Rumor.Time);
				BestTime = Rumor.Time;
			}
		}
	}
	return Best;
}

// --- Обвинение («найди вора», «найди автора слуха») ---

bool AObshagaBotController::PlanAccuse(const FTaskRow& Row)
{
	AObshagaPlayerState* MyState = GetMyState();
	const AObshagaGameState* GameState = GetWorld()->GetGameState<AObshagaGameState>();
	if (!MyState->CanAccuse() || GetWorld()->GetTimeSeconds() < AccuseRetryAt || Me->GetCarryComponent()->IsCarrying())
	{
		return false;
	}
	const FBotSuspect Suspect = MyState->IsAccuseRumor() ? FindNoteAuthor() : FindTvThief(Row);
	// Видел сам или сложил два и два — идёт сразу. По одному слуху — только утром: обвинение одно, ошибка дорогая.
	if (Suspect.Strength == EBotEvidence::None || (Suspect.Strength < EBotEvidence::Medium && GameState->GetPhase() != ERoundPhase::Morning))
	{
		return false;
	}
	const AObshagaPlayerState* SuspectState = Cast<AObshagaPlayerState>(Suspect.Who.Get());
	const AObshagaCharacter* SuspectCharacter = SuspectState ? Cast<AObshagaCharacter>(SuspectState->GetPawn()) : nullptr;
	if (!SuspectCharacter || SuspectState->IsEvicted() || SuspectCharacter->IsHiding() || SuspectCharacter->IsGhost())
	{
		return false;
	}
	AccusePlan = Suspect;
	if (TryAccuse())
	{
		return true;
	}
	const float Reach = FMath::Max(GetRoundConfig()->AccuseDistance - BotAccuseMargin, 100.f);
	return WalkTo(SuspectCharacter->GetActorLocation(), EBotGoal::Accuse, FMath::Min(Reach * 0.5f, 400.f));
}

bool AObshagaBotController::TryAccuse()
{
	AObshagaPlayerState* MyState = GetMyState();
	AObshagaGameMode* GameMode = GetWorld()->GetAuthGameMode<AObshagaGameMode>();
	AObshagaPlayerState* SuspectState = Cast<AObshagaPlayerState>(AccusePlan.Who.Get());
	AObshagaCharacter* SuspectCharacter = SuspectState ? Cast<AObshagaCharacter>(SuspectState->GetPawn()) : nullptr;
	if (!GameMode || !MyState->CanAccuse() || !SuspectCharacter || SuspectCharacter->IsHiding() || SuspectCharacter->IsGhost())
	{
		return false;
	}
	// Как человек: показать можно только на того, кто рядом и на виду (сервер проверит то же самое).
	const float Reach = FMath::Max(GetRoundConfig()->AccuseDistance - BotAccuseMargin, 100.f);
	if (FVector::Dist(SuspectCharacter->GetActorLocation(), Me->GetActorLocation()) > Reach || !LineOfSightTo(SuspectCharacter))
	{
		return false;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	const bool bAccepted = GameMode->Accuse(MyState, SuspectCharacter);
	UE_LOG(LogObshaga, Verbose, TEXT("Bot %s accuses %s: evidence %s (%s), accepted %d, t=%.1f"), *MyState->GetPlayerName(), *SuspectState->GetPlayerName(),
		EvidenceName(AccusePlan.Strength), *AccusePlan.Source, bAccepted ? 1 : 0, GetRoundSeconds());
	if (bAccepted)
	{
		LastLineTime = -100.f;
		Say(static_cast<uint8>(MyState->IsAccuseRumor() ? ObshagaStory::EBotLine::AccuseRumor : ObshagaStory::EBotLine::AccuseThief));
	}
	else
	{
		// Палыч занят — попытка не потрачена, скажем позже.
		AccuseRetryAt = Now + BotRetrySeconds;
	}
	if (State == EBotState::Walk && Goal == EBotGoal::Accuse)
	{
		Goal = EBotGoal::None;
		Path.Reset();
		SetState(EBotState::Idle);
		IdleUntil = Now + 0.5f;
	}
	return true;
}

// --- Стук на комнату («настучи, чтобы нашли запрещёнку») ---

bool AObshagaBotController::PlanTipRoom()
{
	AObshagaPlayerState* MyState = GetMyState();
	const AObshagaGameState* GameState = GetWorld()->GetGameState<AObshagaGameState>();
	if (!MyState->CanTipRoom() || GetWorld()->GetTimeSeconds() < TipRoomRetryAt)
	{
		return false;
	}
	FBotSuspect Plan = FindContrabandRoom();
	if (Plan.Strength < EBotEvidence::Medium)
	{
		// Улик мало — тянем до последнего (вдруг увидим), а под конец стучим по слуху или наугад.
		const ERoundPhase Phase = GameState->GetPhase();
		const bool bLate = Phase == ERoundPhase::Morning || (Phase == ERoundPhase::Night && GameState->GetPhaseRemainingSeconds() <= GetRoundConfig()->BotTipGuessSeconds);
		if (!bLate)
		{
			return false;
		}
		if (Plan.Strength != EBotEvidence::Weak)
		{
			// Наугад — решается один раз за раунд: стучать ли вообще и на какую комнату.
			if (!bTipGuessDecided)
			{
				bTipGuessDecided = true;
				if (FMath::FRand() < GetRoundConfig()->BotTipGuessChance)
				{
					TArray<FName> Rooms;
					for (TActorIterator<ARoomVolume> It(GetWorld()); It; ++It)
					{
						if (It->RoomType == ERoomType::Bedroom && It->RoomId != MyState->GetHomeRoomId() && !RoomPoint(It->RoomId).IsZero())
						{
							Rooms.AddUnique(It->RoomId);
						}
					}
					if (!Rooms.IsEmpty())
					{
						TipGuess.RoomId = Rooms[FMath::RandRange(0, Rooms.Num() - 1)];
						TipGuess.Strength = EBotEvidence::Guess;
						TipGuess.Source = TEXT("random guess");
					}
				}
			}
			Plan = TipGuess;
		}
	}
	if (Plan.RoomId.IsNone() || Plan.Strength == EBotEvidence::None)
	{
		return false;
	}
	TipRoomPlan = Plan;
	if (TryTipRoom())
	{
		return true;
	}
	return WalkTo(RoomPoint(Plan.RoomId), EBotGoal::TipRoom, 120.f);
}

bool AObshagaBotController::TryTipRoom()
{
	AObshagaPlayerState* MyState = GetMyState();
	AObshagaGameMode* GameMode = GetWorld()->GetAuthGameMode<AObshagaGameMode>();
	const ARoomVolume* Here = Me->GetCurrentRoom();
	if (!GameMode || TipRoomPlan.RoomId.IsNone() || !MyState->CanTipRoom() || !Here || Here->RoomId != TipRoomPlan.RoomId)
	{
		return false;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	const bool bAccepted = GameMode->TipOffRoom(MyState);
	UE_LOG(LogObshaga, Verbose, TEXT("Bot %s tips room %s: evidence %s (%s), accepted %d, t=%.1f"), *MyState->GetPlayerName(), *TipRoomPlan.RoomId.ToString(),
		EvidenceName(TipRoomPlan.Strength), *TipRoomPlan.Source, bAccepted ? 1 : 0, GetRoundSeconds());
	if (bAccepted)
	{
		LastLineTime = -100.f;
		Say(static_cast<uint8>(ObshagaStory::EBotLine::TipRoom));
	}
	else
	{
		TipRoomRetryAt = Now + BotRetrySeconds;
	}
	if (State == EBotState::Walk && Goal == EBotGoal::TipRoom)
	{
		Goal = EBotGoal::None;
		Path.Reset();
		SetState(EBotState::Idle);
		IdleUntil = Now + 0.5f;
	}
	return true;
}

// --- Подстава («подбрось соседу») ---

bool AObshagaBotController::PlanFrame(const FTaskRow& Row)
{
	if (bPlanted)
	{
		return false;
	}
	AObshagaPlayerState* MyState = GetMyState();
	const AObshagaGameState* GameState = GetWorld()->GetGameState<AObshagaGameState>();
	// Жертва выбирается один раз (заново — только если её выселили): чужой жилец, у которого своя комната.
	const AObshagaPlayerState* Victim = Cast<AObshagaPlayerState>(FrameVictim.Get());
	if (!Victim || Victim->IsEvicted())
	{
		TArray<AObshagaPlayerState*> Seen;
		TArray<AObshagaPlayerState*> All;
		for (APlayerState* Other : GameState->PlayerArray)
		{
			AObshagaPlayerState* OtherState = Cast<AObshagaPlayerState>(Other);
			if (!OtherState || OtherState == MyState || OtherState->IsEvicted() || OtherState->GetHomeRoomId().IsNone()
				|| OtherState->GetHomeRoomId() == MyState->GetHomeRoomId())
			{
				continue;
			}
			All.Add(OtherState);
			// Кого бот видит прямо сейчас и кто не в шкафу — того и подставить проще всего вспомнить.
			const AObshagaCharacter* OtherCharacter = Cast<AObshagaCharacter>(OtherState->GetPawn());
			if (OtherCharacter && !OtherCharacter->IsHiding() && CanWitness(OtherCharacter))
			{
				Seen.Add(OtherState);
			}
		}
		const TArray<AObshagaPlayerState*>& Pool = Seen.IsEmpty() ? All : Seen;
		if (Pool.IsEmpty())
		{
			return false;
		}
		AObshagaPlayerState* Picked = Pool[FMath::RandRange(0, Pool.Num() - 1)];
		FrameVictim = Picked;
		FrameRoom = Picked->GetHomeRoomId();
	}
	if (FrameRoom.IsNone())
	{
		return false;
	}

	UCarryComponent* Carry = Me->GetCarryComponent();
	if (AItemActor* Carried = Carry->GetCarriedItem())
	{
		if (!RowWantsItem(Row, Carried))
		{
			return false;
		}
		// Запрещёнку — в тайник жертвы (утром Палыч найдёт её там); прочее — просто оставить в комнате.
		AHidingSpot* Spot = Carried->GetItemData()->bContraband ? FindSpotInRoom(FrameRoom, false) : nullptr;
		if (Spot && !IsGivenUp(Spot))
		{
			TargetSpot = Spot;
			return WalkTo(Spot->GetActorLocation(), EBotGoal::HideItem, 150.f);
		}
		return WalkTo(RoomPoint(FrameRoom), EBotGoal::Drop, 120.f);
	}
	AItemActor* Item = Row.ItemId.IsNone() ? FindItem(NAME_None, true, false, true) : FindItem(Row.ItemId, false, false, true);
	if (!Item || IsGivenUp(Item) || Item->GetCurrentRoomId() == FrameRoom)
	{
		return false;
	}
	const AKomendantCharacter* Komendant = FindKomendant();
	if (Komendant && FVector::Dist(Komendant->GetActorLocation(), Me->GetActorLocation()) < GetRoundConfig()->BotFleeDistance * 1.2f)
	{
		return false;
	}
	TargetItem = Item;
	return WalkTo(Item->GetActorLocation(), EBotGoal::PickUp, 150.f);
}

// --- Ложная тревога («вымани Палыча шумом») ---

bool AObshagaBotController::IsLureItem(const AItemActor* Item) const
{
	const UObshagaItemData* Data = Item ? Item->GetItemData() : nullptr;
	if (!Data || Data->bHeavy || Data->bContraband || Data->bReadable || Data->NoiseFactor < GetRoundConfig()->KomendantMinNoise)
	{
		return false;
	}
	// Вещь, нужная другому заданию бота, — не для броска.
	for (const FTaskRow& Row : GetMyState()->GetTaskComponent()->GetRows())
	{
		if (!Row.ItemId.IsNone() && Row.ItemId == Data->ItemId)
		{
			return false;
		}
	}
	return true;
}

bool AObshagaBotController::PlanLure()
{
	const float Now = GetWorld()->GetTimeSeconds();
	const AKomendantCharacter* Komendant = FindKomendant();
	if (bLureDone || LureThrowTime >= 0.f || Now < LureRetryAt || !Komendant)
	{
		return false;
	}
	UCarryComponent* Carry = Me->GetCarryComponent();
	AItemActor* Carried = Carry->GetCarriedItem();
	if (!Carried)
	{
		// Взять лёгкую звонкую вещь, что лежит на виду.
		AItemActor* Best = nullptr;
		float BestDistance = TNumericLimits<float>::Max();
		for (TActorIterator<AItemActor> It(GetWorld()); It; ++It)
		{
			if (It->GetItemState() != EItemState::World || IsGivenUp(*It) || !IsLureItem(*It))
			{
				continue;
			}
			const float Distance = FVector::Dist(It->GetActorLocation(), Me->GetActorLocation());
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				Best = *It;
			}
		}
		if (!Best)
		{
			return false;
		}
		TargetItem = Best;
		return WalkTo(Best->GetActorLocation(), EBotGoal::PickUp, 150.f);
	}
	const FTaskRow* LureRow = GetMyState()->GetTaskComponent()->FindRowByCondition(ETaskCondition::NoiseLuredKomendantAndUnseen);
	if (!LureRow || !RowWantsItem(*LureRow, Carried) || Komendant->GetAlert() != EKomendantAlert::Calm)
	{
		return false;
	}
	// Встать так, чтобы Палыч был не слишком близко (поймает) и не слишком далеко (не услышит), и лучше — за стеной от него.
	const UObshagaRoundConfig* Config = GetRoundConfig();
	const FVector Here = Me->GetActorLocation();
	const FVector KomendantAt = Komendant->GetActorLocation();
	const float Distance = FVector::Dist(Here, KomendantAt);
	const bool bInBand = Distance >= Config->BotLureMinDistance && Distance <= Config->BotLureMaxDistance;
	if (bInBand && !LineOfSightTo(Komendant))
	{
		return TryLureThrow();
	}
	const AKomendantWaypoint* Covered = nullptr;
	const AKomendantWaypoint* Exposed = nullptr;
	float CoveredDistance = BotLureSearchRadius;
	float ExposedDistance = BotLureSearchRadius;
	for (const AKomendantWaypoint* Waypoint : Waypoints)
	{
		const FVector At = Waypoint->GetActorLocation();
		const float FromMe = FVector::Dist(At, Here);
		const float FromKomendant = FVector::Dist(At, KomendantAt);
		if (FromMe > BotLureSearchRadius || FromKomendant < Config->BotLureMinDistance || FromKomendant > Config->BotLureMaxDistance)
		{
			continue;
		}
		if (!HasClearLine(At, KomendantAt))
		{
			if (FromMe < CoveredDistance)
			{
				CoveredDistance = FromMe;
				Covered = Waypoint;
			}
		}
		else if (FromMe < ExposedDistance)
		{
			ExposedDistance = FromMe;
			Exposed = Waypoint;
		}
	}
	if (Covered)
	{
		return WalkTo(Covered->GetActorLocation(), EBotGoal::Lure, 100.f);
	}
	if (bInBand)
	{
		return TryLureThrow();
	}
	return Exposed && WalkTo(Exposed->GetActorLocation(), EBotGoal::Lure, 100.f);
}

bool AObshagaBotController::TryLureThrow()
{
	AObshagaPlayerState* MyState = GetMyState();
	const AObshagaGameState* GameState = GetWorld()->GetGameState<AObshagaGameState>();
	UCarryComponent* Carry = Me->GetCarryComponent();
	AItemActor* Item = Carry->GetCarriedItem();
	const AKomendantCharacter* Komendant = FindKomendant();
	if (!Item || !IsLureItem(Item) || !Komendant || Komendant->GetAlert() != EKomendantAlert::Calm || bLureDone || LureThrowTime >= 0.f)
	{
		return false;
	}
	const UObshagaRoundConfig* Config = GetRoundConfig();
	const FVector KomendantAt = Komendant->GetActorLocation();
	const float Distance = FVector::Dist(Me->GetActorLocation(), KomendantAt);
	if (Distance < Config->BotLureMinDistance || Distance > Config->BotLureMaxDistance)
	{
		return false;
	}
	const bool bSeesMe = LineOfSightTo(Komendant);
	// Бросаем от Палыча: вещь стукнет подальше от него, а сам бот уйдёт в другую сторону.
	// Бросок летит туда, куда «смотрит» персонаж, — поворачиваем и тело, и взгляд контроллера.
	FVector Away = Me->GetActorLocation() - KomendantAt;
	Away.Z = 0.f;
	const FRotator Facing = Away.IsNearlyZero() ? Me->GetActorRotation() : Away.Rotation();
	Me->SetActorRotation(Facing);
	SetControlRotation(Facing);
	Carry->Throw();
	if (Carry->GetCarriedItem() == Item)
	{
		return false;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	UE_LOG(LogObshaga, Verbose, TEXT("Bot %s lure throw %s (%s): komendant %.0f cm, sees me %d, phase %s, t=%.1f"), *MyState->GetPlayerName(),
		*Item->GetItemData()->ItemId.ToString(), *Item->GetName(), Distance, bSeesMe ? 1 : 0,
		*StaticEnum<ERoundPhase>()->GetNameStringByValue(static_cast<int64>(GameState->GetPhase())), GetRoundSeconds());
	LureThrowTime = Now;
	// Ту же вещь сразу не подбираем: пусть полежит, где упала.
	GaveUpOn = Item;
	GaveUpUntil = Now + BotLureRetrySeconds;
	if (FMath::FRand() < 0.5f)
	{
		Say(static_cast<uint8>(ObshagaStory::EBotLine::Lure));
	}
	// И скорее домой: попасться теперь нельзя.
	if (!WalkTo(RoomPoint(MyState->GetHomeRoomId()), EBotGoal::Wander, 150.f))
	{
		Goal = EBotGoal::None;
		Path.Reset();
		SetState(EBotState::Idle);
		IdleUntil = Now + 0.5f;
	}
	return true;
}
