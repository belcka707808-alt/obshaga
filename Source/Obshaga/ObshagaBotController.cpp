#include "ObshagaBotController.h"

#include "CarryComponent.h"
#include "DeviceActor.h"
#include "DoorActor.h"
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
#include "TaskComponent.h"
#include "TaskTypes.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace
{
	constexpr float BotThinkSeconds = 0.4f;
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
	constexpr float BotCarefulFleeScale = 1.4f;
	// Ответ на допросе — не мгновенно, а через столько секунд (случайно в пределах).
	constexpr float BotAnswerMinSeconds = 1.5f;
	constexpr float BotAnswerMaxSeconds = 4.f;
	// Как часто бот при безделье машет рукой или говорит фразу.
	constexpr float BotEmoteChance = 0.08f;
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
	ReadNotes.Reset();
	bTipped = false;
	TipAtTime = -1.f;
	AnswerAt = -1.f;
	AlibiDecidedFor.Reset();
	ReactedToCaught.Reset();
	bFireAlarmGrumbled = false;
	bCalledKomendant = false;
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
	if (!bFireAlarmGrumbled && GameState->IsNightEventActive() && GameState->GetNightEvent() == ENightEvent::FireAlarm && FMath::FRand() < 0.15f)
	{
		bFireAlarmGrumbled = true;
		Say(static_cast<uint8>(ObshagaStory::EBotLine::FireAlarm));
	}
	if (Me->IsFrozen())
	{
		return;
	}
	GuardWatch();

	// Бот-Крыса стучит на жертву, когда ночью подходит его время.
	if (MyState->GetTrueRole() == EPlayerRole::Rat && !bTipped && GameState->GetPhase() == ERoundPhase::Night)
	{
		if (TipAtTime < 0.f)
		{
			TipAtTime = Now + FMath::FRandRange(Config->BotTipMinSeconds, FMath::Max(Config->BotTipMinSeconds, Config->BotTipMaxSeconds));
		}
		else if (Now >= TipAtTime && MyState->GetRatTarget())
		{
			bTipped = true;
			GameMode->TipOff(MyState);
			UE_LOG(LogObshaga, Verbose, TEXT("Bot %s tipped off on %s"), *MyState->GetPlayerName(), *MyState->GetRatTarget()->GetPlayerName());
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

	if (Komendant && TryFleeOrHide(Komendant))
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
		return;
	}

	// Idle.
	if (Now < IdleUntil)
	{
		return;
	}
	// Записка рядом — любопытно, что там (без читателей задание «пустить слух» не выполнить никому).
	if (!Me->GetCarryComponent()->IsCarrying())
	{
		for (TActorIterator<AItemActor> It(GetWorld()); It; ++It)
		{
			if (It->GetItemData()->bReadable && It->GetItemState() == EItemState::World && !ReadNotes.Contains(*It)
				&& FVector::Dist(It->GetActorLocation(), Me->GetActorLocation()) < 900.f && FMath::Abs(It->GetActorLocation().Z - Me->GetActorLocation().Z) < BotSameFloorHeight * 2.f)
			{
				ReadNotes.Add(*It);
				TargetItem = *It;
				if (WalkTo(It->GetActorLocation(), EBotGoal::ReadNote, 150.f))
				{
					return;
				}
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
		return;
	}

	if (Info.Suspect == MyState)
	{
		// Пойман: подумать пару секунд и выбрать ответ случайно с весами.
		if (Info.Choice != EInterrogationChoice::None)
		{
			return;
		}
		if (AnswerAt < 0.f)
		{
			AnswerAt = Now + FMath::FRandRange(BotAnswerMinSeconds, BotAnswerMaxSeconds);
			Path.Reset();
			SetState(EBotState::Idle);
		}
		else if (Now >= AnswerAt)
		{
			const float Total = Config->BotConfessWeight + Config->BotLieWeight + Config->BotSilentWeight;
			const float Roll = FMath::FRandRange(0.f, FMath::Max(Total, 0.01f));
			const EInterrogationChoice Choice = Roll < Config->BotConfessWeight ? EInterrogationChoice::Confess
				: (Roll < Config->BotConfessWeight + Config->BotLieWeight ? EInterrogationChoice::Lie : EInterrogationChoice::Silent);
			GameMode->SubmitInterrogationChoice(MyState, Choice);
			UE_LOG(LogObshaga, Verbose, TEXT("Bot %s answers %s"), *MyState->GetPlayerName(), *UEnum::GetValueAsString(Choice));
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
	if (AlibiDecidedFor.Get() != Info.Suspect && SuspectPawn && !Me->IsHiding() && !Me->IsGhost()
		&& FVector::Dist(SuspectPawn->GetActorLocation(), Me->GetActorLocation()) <= GameState->GetAlibiRadius())
	{
		AlibiDecidedFor = Info.Suspect;
		if (FMath::FRand() < Config->BotAlibiChance)
		{
			GameMode->ConfirmAlibi(Me);
			Say(static_cast<uint8>(ObshagaStory::EBotLine::Alibi));
			UE_LOG(LogObshaga, Verbose, TEXT("Bot %s confirms alibi of %s"), *MyState->GetPlayerName(), *Info.Suspect->GetPlayerName());
		}
	}
}

void AObshagaBotController::GuardWatch()
{
	const FTaskRow* Row = GetMyState()->GetTaskComponent()->FindRowByCondition(ETaskCondition::ItemNotMovedFromZone);
	if (!Row || bCalledKomendant || Me->IsHiding())
	{
		return;
	}
	for (TActorIterator<AItemActor> It(GetWorld()); It; ++It)
	{
		const APawn* Thief = Cast<APawn>(It->GetHolder());
		if (It->GetItemData()->ItemId != Row->ItemId || It->GetItemState() != EItemState::Carried || !Thief || Thief == Me
			|| FVector::Dist(Thief->GetActorLocation(), Me->GetActorLocation()) > 1500.f || !HasClearLine(Me->GetActorLocation(), Thief->GetActorLocation()))
		{
			continue;
		}
		// Видит вора с телевизором: кричит и зовёт Палыча туда, где вор сейчас (как стук Крысы, но вслух).
		bCalledKomendant = true;
		LastLineTime = -100.f;
		Say(static_cast<uint8>(ObshagaStory::EBotLine::CallKomendant));
		for (TActorIterator<AKomendantAIController> Brain(GetWorld()); Brain; ++Brain)
		{
			Brain->InvestigateTip(Thief->GetActorLocation());
		}
		UE_LOG(LogObshaga, Verbose, TEXT("Bot %s calls the komendant: %s carries %s"), *GetMyState()->GetPlayerName(), *GetNameSafe(Thief->GetPlayerState()), *It->GetName());
		return;
	}
}

bool AObshagaBotController::TryFleeOrHide(const AKomendantCharacter* Komendant)
{
	const UObshagaRoundConfig* Config = GetRoundConfig();
	const AObshagaPlayerState* MyState = GetMyState();
	const bool bCareful = MyState->GetTaskComponent()->FindRowByCondition(ETaskCondition::NeverSpottedWholeRound) != nullptr;
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
			TargetSpot = Best;
			Me->SetSprinting(true);
			WalkTo(Best->GetActorLocation(), EBotGoal::HideSelf, 160.f);
			Say(static_cast<uint8>(ObshagaStory::EBotLine::SpotKomendant));
			State = EBotState::Flee;
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
	Me->SetSprinting(!Me->GetCarryComponent()->IsCarryingHeavy());
	WalkTo(Best->GetActorLocation(), EBotGoal::None, 120.f);
	State = EBotState::Flee;
	UE_LOG(LogObshaga, Verbose, TEXT("Bot %s flees from the komendant"), *MyState->GetPlayerName());
	Say(static_cast<uint8>(ObshagaStory::EBotLine::SpotKomendant));
	return true;
}

bool AObshagaBotController::PlanTask()
{
	const UTaskComponent* Tasks = GetMyState()->GetTaskComponent();
	const TArray<FTaskRow>& Rows = Tasks->GetRows();
	const TArray<FTaskState>& States = Tasks->GetTasks();
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
	auto GaveUp = [this, Now](const AActor* Actor) { return Actor && GaveUpOn.Get() == Actor && Now < GaveUpUntil; };

	switch (Row.SuccessCondition)
	{
	case ETaskCondition::ItemInRoomAtEnd:
	case ETaskCondition::ContrabandSurvivedInspection:
	{
		// Унести предмет (ТВ, запрещёнку) к себе в комнату и спрятать там, если есть куда.
		const bool bContraband = Row.SuccessCondition == ETaskCondition::ContrabandSurvivedInspection;
		const FName Room = (Row.bUseOwnRoom || bContraband || Row.RoomId.IsNone()) ? MyState->GetHomeRoomId() : Row.RoomId;
		AItemActor* Carried = Carry->GetCarriedItem();
		AItemActor* Item = Carried ? Carried : FindItem(bContraband ? NAME_None : Row.ItemId, bContraband, false);
		if (!Item || GaveUp(Item))
		{
			return false;
		}
		if (Carried)
		{
			CarryRoom = Room;
			AHidingSpot* Spot = FindSpotInRoom(Room, false);
			if (Spot && !GaveUp(Spot))
			{
				TargetSpot = Spot;
				return WalkTo(Spot->GetActorLocation(), EBotGoal::HideItem, 150.f);
			}
			return WalkTo(RoomPoint(Room), EBotGoal::Drop, 120.f);
		}
		// Уже лежит у нас в комнате (или спрятан) — дело сделано.
		if (Item->GetCurrentRoomId() == Room || Item->GetItemState() == EItemState::Hidden || Item->GetItemState() == EItemState::Carried)
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
		AItemActor* Carried = Carry->GetCarriedItem();
		if (Carried)
		{
			CarryRoom = Row.RoomId;
			return WalkTo(RoomPoint(Row.RoomId), EBotGoal::Drop, 120.f);
		}
		AItemActor* Item = FindItem(Row.ItemId, false, false);
		if (Item && !GaveUp(Item) && Item->GetItemState() == EItemState::World && Item->GetCurrentRoomId() != Row.RoomId)
		{
			TargetItem = Item;
			return WalkTo(Item->GetActorLocation(), EBotGoal::PickUp, 150.f);
		}
		if (FMath::FRand() < 0.5f)
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
		ADeviceActor* Device = FindDevice(true);
		if (!Device || GaveUp(Device))
		{
			return false;
		}
		TargetDevice = Device;
		return WalkTo(Device->GetActorLocation(), EBotGoal::RepairDevice, 160.f);
	}

	case ETaskCondition::NoteReadByDistinctPlayers:
	{
		// Пустить слух: взять записку и оставить её на видном месте — в коридоре или гостиной.
		if (bRumorPlanted)
		{
			return false;
		}
		AItemActor* Carried = Carry->GetCarriedItem();
		if (Carried && Carried->GetItemData()->bReadable)
		{
			TArray<FVector> Points;
			for (const AKomendantWaypoint* Waypoint : Waypoints)
			{
				const ARoomVolume* Room = ARoomVolume::FindRoomAt(this, Waypoint->GetActorLocation());
				if (Room && (Room->RoomType == ERoomType::Corridor || Room->RoomType == ERoomType::LivingRoom || Room->RoomType == ERoomType::Kitchen))
				{
					Points.Add(Waypoint->GetActorLocation());
				}
			}
			return !Points.IsEmpty() && WalkTo(Points[FMath::RandRange(0, Points.Num() - 1)], EBotGoal::Drop, 120.f);
		}
		AItemActor* Note = FindItem(Row.ItemId, false, true);
		if (!Note || GaveUp(Note) || Note->GetItemState() != EItemState::World)
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

	default:
		// «Ни разу не попасться» и прочее — это не действие, а манера: просто осторожнее держаться от Палыча.
		return false;
	}
}

void AObshagaBotController::PlanWander()
{
	const UObshagaRoundConfig* Config = GetRoundConfig();
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

	switch (Done)
	{
	case EBotGoal::PickUp:
		if (AItemActor* Item = TargetItem.Get())
		{
			if (Item->GetItemState() == EItemState::World && FVector::Dist(Item->GetActorLocation(), Me->GetActorLocation()) < 260.f)
			{
				Item->Interact(Me);
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
			Carry->Drop();
			bRumorPlanted |= bNote;
			if (bNote)
			{
				// Свою записку бот не читает.
				ReadNotes.Add(TargetItem);
			}
		}
		break;
	case EBotGoal::ReadNote:
		if (AItemActor* Note = TargetItem.Get())
		{
			if (Note->GetItemState() == EItemState::World && FVector::Dist(Note->GetActorLocation(), Me->GetActorLocation()) < 260.f)
			{
				Note->SecondaryInteract(Me);
				UE_LOG(LogObshaga, Verbose, TEXT("Bot %s read %s"), *GetMyState()->GetPlayerName(), *Note->GetName());
			}
		}
		break;
	case EBotGoal::HideItem:
		if (AHidingSpot* Spot = TargetSpot.Get())
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
		if (AHidingSpot* Spot = TargetSpot.Get())
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
		if (ADeviceActor* Device = TargetDevice.Get())
		{
			UE_LOG(LogObshaga, Verbose, TEXT("Bot %s breaks %s"), *GetMyState()->GetPlayerName(), *Device->GetName());
			Device->SecondaryInteract(Me);
			SetState(EBotState::Act);
			ActUntil = Now + Device->GetBreakDuration() + 0.5f;
		}
		break;
	case EBotGoal::RepairDevice:
		if (ADeviceActor* Device = TargetDevice.Get())
		{
			UE_LOG(LogObshaga, Verbose, TEXT("Bot %s repairs %s"), *GetMyState()->GetPlayerName(), *Device->GetName());
			Device->Interact(Me);
			SetState(EBotState::Act);
			ActUntil = Now + Device->GetRepairDuration() + 0.5f;
		}
		break;
	case EBotGoal::GoOutside:
		if (AHidingSpot* Door = TargetSpot.Get())
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
			UE_LOG(LogObshaga, Verbose, TEXT("Evicted bot %s left the building"), *GetMyState()->GetPlayerName());
			UnPossess();
			Body->Destroy();
			Me = nullptr;
		}
		break;
	default:
		break;
	}
}

AItemActor* AObshagaBotController::FindItem(FName ItemId, bool bContraband, bool bReadable) const
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
			// Что мешает: капсулой на полметра вперёд, к точке.
			FHitResult Block;
			FCollisionQueryParams BlockParams(SCENE_QUERY_STAT(BotStuck), false, Me);
			const FVector From = Me->GetActorLocation();
			GetWorld()->SweepSingleByChannel(Block, From, From + ToPoint.GetSafeNormal2D() * 60.f, FQuat::Identity, ECC_Pawn,
				FCollisionShape::MakeCapsule(Me->GetCapsuleComponent()->GetScaledCapsuleRadius(), Me->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 0.8f), BlockParams);
			UE_LOG(LogObshaga, Log, TEXT("Bot %s stuck near %s on the way to %s (point %d of %d), blocked by %s, snapping to waypoint"), GetMyState() ? *GetMyState()->GetPlayerName() : TEXT("-"),
				*Me->GetActorLocation().ToCompactString(), *Point.ToCompactString(), PathIndex + 1, Path.Num(), *GetNameSafe(Block.GetActor()));
			Me->SetActorLocation(Point, false, nullptr, ETeleportType::TeleportPhysics);
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
