#include "ObshagaGameMode.h"

#include "CarryComponent.h"
#include "DeviceActor.h"
#include "DoorActor.h"
#include "GameEventSubsystem.h"
#include "HidingSpot.h"
#include "InteractionComponent.h"
#include "ItemActor.h"
#include "KomendantAIController.h"
#include "NightExitDoor.h"
#include "Obshaga.h"
#include "ObshagaCharacter.h"
#include "ObshagaGameState.h"
#include "ObshagaHUD.h"
#include "ObshagaItemData.h"
#include "ObshagaPlayerController.h"
#include "ObshagaPlayerState.h"
#include "ObshagaRoundConfig.h"
#include "RoomVolume.h"
#include "SuspicionComponent.h"
#include "TaskComponent.h"
#include "TaskDirector.h"
#include "Dom/JsonObject.h"
#include "Engine/PlayerStartPIE.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "TimerManager.h"

#define LOCTEXT_NAMESPACE "ObshagaGameMode"

AObshagaGameMode::AObshagaGameMode()
{
	// Внешность персонажа назначается в BP_ObshagaGameMode (Default Pawn Class).
	DefaultPawnClass = AObshagaCharacter::StaticClass();
	PlayerControllerClass = AObshagaPlayerController::StaticClass();
	PlayerStateClass = AObshagaPlayerState::StaticClass();
	GameStateClass = AObshagaGameState::StaticClass();
	HUDClass = AObshagaHUD::StaticClass();
}

const UObshagaRoundConfig* AObshagaGameMode::GetRoundConfig() const
{
	return RoundConfig ? RoundConfig.Get() : GetDefault<UObshagaRoundConfig>();
}

void AObshagaGameMode::BeginPlay()
{
	Super::BeginPlay();

	TaskDirector = NewObject<UTaskDirector>(this);
	TaskDirector->Initialize(GetRoundConfig()->TasksTable);
	TaskDirector->DebugMainTasks = GetRoundConfig()->DebugMainTasks;

	if (GetRoundConfig()->bAutoStart)
	{
		const float Delay = FMath::Max(GetRoundConfig()->StartDelaySeconds, 0.1f);
		GetWorldTimerManager().SetTimer(PhaseTimer, this, &AObshagaGameMode::StartRound, Delay, false);
	}
}

TArray<AObshagaPlayerState*> AObshagaGameMode::GetObshagaPlayers() const
{
	TArray<AObshagaPlayerState*> Result;
	if (const AGameStateBase* State = GetGameState<AGameStateBase>())
	{
		for (APlayerState* Player : State->PlayerArray)
		{
			if (AObshagaPlayerState* PlayerState = Cast<AObshagaPlayerState>(Player))
			{
				Result.Add(PlayerState);
			}
		}
	}
	return Result;
}

void AObshagaGameMode::NotifyPlayer(const APlayerState* PlayerState, const FText& Text) const
{
	if (AObshagaPlayerController* Controller = PlayerState ? Cast<AObshagaPlayerController>(PlayerState->GetOwner()) : nullptr)
	{
		Controller->ClientShowNotice(Text);
	}
}

void AObshagaGameMode::NotifyAll(const FText& Text) const
{
	for (const AObshagaPlayerState* PlayerState : GetObshagaPlayers())
	{
		NotifyPlayer(PlayerState, Text);
	}
}

void AObshagaGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	Super::HandleStartingNewPlayer_Implementation(NewPlayer);

	AObshagaPlayerState* PlayerState = NewPlayer ? NewPlayer->GetPlayerState<AObshagaPlayerState>() : nullptr;
	if (!PlayerState)
	{
		return;
	}

	// Пока нет лобби с настоящими именами (M7), игроки — «Жилец 1», «Жилец 2»...
	PlayerState->SetPlayerName(FString::Printf(TEXT("Жилец %d"), NextPlayerNumber++));
	AssignHomeRoom(NewPlayer);

	// Кто вошёл уже во время раунда, тоже получает задания (обычным жильцом).
	const AObshagaGameState* State = GetGameState<AObshagaGameState>();
	if (State && State->GetRoundState() == ERoundState::InProgress)
	{
		TaskDirector->AssignTasksTo(PlayerState, GetNumPlayers(), State->GetPhase());
		GrantTaskAbilities(PlayerState);
		ScheduleSelfBreakIfNeeded();
		NotifyPlayer(PlayerState, LOCTEXT("NewTask", "Новые секретные задания — открой телефон [Tab]"));
	}
}

void AObshagaGameMode::GrantTaskAbilities(AObshagaPlayerState* PlayerState) const
{
	const UTaskComponent* Tasks = PlayerState->GetTaskComponent();
	PlayerState->SetTaskAbilities(Tasks->FindRowByCondition(ETaskCondition::CorrectAccusation) != nullptr,
		Tasks->FindRowByCondition(ETaskCondition::InspectionFoundContrabandInRoom) != nullptr);
}

void AObshagaGameMode::ScheduleSelfBreakIfNeeded()
{
	// Если кому-то выпало чинить, а ломать некому и ничего не сломано, прибор сломается сам.
	const TArray<AObshagaPlayerState*> Players = GetObshagaPlayers();
	auto AnyoneHas = [&Players](ETaskCondition Condition)
	{
		return Players.ContainsByPredicate([Condition](const AObshagaPlayerState* PlayerState)
			{ return PlayerState->GetTaskComponent()->FindRowByCondition(Condition) != nullptr; });
	};
	bool bAnyBroken = false;
	for (TActorIterator<ADeviceActor> It(GetWorld()); It; ++It)
	{
		bAnyBroken |= It->IsBroken();
	}
	if (bAnyBroken || GetWorldTimerManager().IsTimerActive(SelfBreakTimer)
		|| !AnyoneHas(ETaskCondition::DeviceRepaired) || AnyoneHas(ETaskCondition::DeviceBrokenAndNotSeen))
	{
		return;
	}

	const UObshagaRoundConfig* Config = GetRoundConfig();
	const float Delay = FMath::FRandRange(Config->SelfBreakMinSeconds, FMath::Max(Config->SelfBreakMinSeconds, Config->SelfBreakMaxSeconds));
	GetWorldTimerManager().SetTimer(SelfBreakTimer, this, &AObshagaGameMode::BreakDeviceByItself, FMath::Max(Delay, 0.1f), false);
}

AActor* AObshagaGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	// Домашняя комната игрока — та, где он появился. Поэтому новичка селим в наименее заселённую спальню:
	// у соседей по комнате не получится ни подставить друг друга, ни настучать.
	TSet<const AActor*> Taken;
	TMap<FName, int32> Residents;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* Other = It->Get();
		const AActor* Spot = (Other && Other != Player) ? Other->StartSpot.Get() : nullptr;
		if (!Spot)
		{
			continue;
		}
		Taken.Add(Spot);
		if (const ARoomVolume* Room = ARoomVolume::FindRoomAt(this, Spot->GetActorLocation()))
		{
			++Residents.FindOrAdd(Room->RoomId);
		}
	}

	TArray<APlayerStart*> Best;
	int32 BestCount = MAX_int32;
	for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
	{
		const ARoomVolume* Room = ARoomVolume::FindRoomAt(this, It->GetActorLocation());
		if (It->IsA<APlayerStartPIE>() || Taken.Contains(*It) || !Room || Room->RoomType != ERoomType::Bedroom)
		{
			continue;
		}

		const int32 Count = Residents.FindRef(Room->RoomId);
		if (Count < BestCount)
		{
			BestCount = Count;
			Best.Reset();
		}
		if (Count == BestCount)
		{
			Best.Add(*It);
		}
	}
	return Best.IsEmpty() ? Super::ChoosePlayerStart_Implementation(Player) : Best[FMath::RandRange(0, Best.Num() - 1)];
}

void AObshagaGameMode::BreakDeviceByItself()
{
	for (TActorIterator<ADeviceActor> It(GetWorld()); It; ++It)
	{
		if (!It->IsBroken())
		{
			It->BreakByItself();
			return;
		}
	}
}

void AObshagaGameMode::AssignHomeRoom(APlayerController* Controller)
{
	// Комната, в которой игрок появился, становится его домашней.
	AObshagaPlayerState* PlayerState = Controller ? Controller->GetPlayerState<AObshagaPlayerState>() : nullptr;
	const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (PlayerState && Pawn)
	{
		const ARoomVolume* Room = ARoomVolume::FindRoomAt(this, Pawn->GetActorLocation());
		PlayerState->SetHomeRoomId(Room ? Room->RoomId : NAME_None);
		UE_LOG(LogObshaga, Verbose, TEXT("%s lives in %s (spawned at %s)"), *PlayerState->GetPlayerName(), *PlayerState->GetHomeRoomId().ToString(),
			*Pawn->GetActorLocation().ToCompactString());
	}
}

void AObshagaGameMode::RequestStart(APlayerController* By)
{
	// Начинать может только хост — тот, чей контроллер на сервере локальный.
	const AObshagaGameState* State = GetGameState<AObshagaGameState>();
	if (!By || !By->IsLocalController() || !State)
	{
		return;
	}

	if (State->GetRoundState() == ERoundState::WaitingToStart)
	{
		GetWorldTimerManager().ClearTimer(PhaseTimer);
		StartRound();
	}
	else if (State->GetRoundState() == ERoundState::Finished)
	{
		StartRound();
	}
}

void AObshagaGameMode::AssignRoles(const TArray<AObshagaPlayerState*>& Players)
{
	const UObshagaRoundConfig* Config = GetRoundConfig();
	if (Players.Num() < Config->MinPlayersForRoles)
	{
		return;
	}

	TArray<AObshagaPlayerState*> Shuffled = Players;
	for (int32 Index = Shuffled.Num() - 1; Index > 0; --Index)
	{
		Shuffled.Swap(Index, FMath::RandRange(0, Index));
	}

	Shuffled[0]->SetRole(EPlayerRole::Rat);
	NotifyPlayer(Shuffled[0], LOCTEXT("YouAreRat", "Ты — Крыса. Подробности в телефоне [Tab]"));

	const bool bParanoid = Players.Num() >= Config->PlayersForSureParanoid || FMath::RandBool();
	if (bParanoid && Shuffled.Num() > 1)
	{
		// Параноику ничего не сообщаем: он не должен знать.
		Shuffled[1]->SetRole(EPlayerRole::Paranoid);
	}

	for (const AObshagaPlayerState* PlayerState : Players)
	{
		UE_LOG(LogObshaga, Verbose, TEXT("Role of %s: %s"), *PlayerState->GetPlayerName(), *UEnum::GetValueAsString(PlayerState->GetTrueRole()));
	}
}

void AObshagaGameMode::StartRound()
{
	AObshagaGameState* State = GetGameState<AObshagaGameState>();
	if (!State || State->GetRoundState() == ERoundState::InProgress)
	{
		return;
	}

	// Каждый раунд, и первый тоже, начинается с чистого мира: то, что натворили в лобби, не считается.
	ResetWorldForRematch();

	const UObshagaRoundConfig* Config = GetRoundConfig();
	const TArray<AObshagaPlayerState*> Players = GetObshagaPlayers();

	CaughtLastRound = CaughtThisRound;
	CaughtThisRound.Reset();
	TipRat.Reset();
	TipTarget.Reset();
	SmsSent = 0;

	for (AObshagaPlayerState* PlayerState : Players)
	{
		PlayerState->ResetForRound();
	}

	TaskDirector->Reset();
	RoundStartWorldTime = GetWorld()->GetTimeSeconds();
	if (UGameEventSubsystem* Bus = UGameEventSubsystem::Get(this))
	{
		Bus->ClearEventLog();
		FGameEvent Event;
		Event.Type = EGameEventType::RoundStarted;
		Bus->Publish(Event);
	}

	AssignRoles(Players);
	for (AObshagaPlayerState* PlayerState : Players)
	{
		TaskDirector->AssignTasksTo(PlayerState, Players.Num(), ERoundPhase::Evening);
		GrantTaskAbilities(PlayerState);
		if (PlayerState->GetTrueRole() != EPlayerRole::Rat)
		{
			NotifyPlayer(PlayerState, LOCTEXT("NewTask", "Новые секретные задания — открой телефон [Tab]"));
		}
	}

	GetWorldTimerManager().ClearTimer(SelfBreakTimer);
	ScheduleSelfBreakIfNeeded();

	// Крыса заранее знает одно чужое основное задание.
	for (AObshagaPlayerState* Rat : Players)
	{
		if (Rat->GetTrueRole() != EPlayerRole::Rat)
		{
			continue;
		}

		TArray<AObshagaPlayerState*> Victims = Players.FilterByPredicate([Rat](const AObshagaPlayerState* Other)
		{
			return Other != Rat && Other->GetTaskComponent()->GetMainTask() != nullptr;
		});
		if (!Victims.IsEmpty())
		{
			AObshagaPlayerState* Victim = Victims[FMath::RandRange(0, Victims.Num() - 1)];
			const FText Intel = FText::Format(LOCTEXT("RatIntel", "{0} должен: «{1}»"),
				FText::FromString(Victim->GetPlayerName()), Victim->GetTaskComponent()->GetMainTask()->Title);
			Rat->SetRatIntel(Intel, Victim);
		}
	}

	for (TActorIterator<AKomendantAIController> It(GetWorld()); It; ++It)
	{
		It->ResetForRound();
	}

	State->StartRound(Config->AlibiRadius, Config->AccuseDistance);
	State->SetNightLighting(Config->NightSunScale, Config->NightSkyScale, Config->NightFadeSeconds);
	BeginPhase(ERoundPhase::Evening);

	GetWorldTimerManager().SetTimer(LiveStatusTimer, this, &AObshagaGameMode::UpdateLiveTaskStatus, 1.f, true);
	if (Config->SmsCount > 0)
	{
		// Первое СМС — через полинтервала: так они приходят в середине фаз и не спорят с объявлением фазы.
		const float Interval = Config->GetTotalSeconds() / Config->SmsCount;
		GetWorldTimerManager().SetTimer(SmsTimer, this, &AObshagaGameMode::SendSms, Interval, true, Interval * 0.5f);
	}
	UE_LOG(LogObshaga, Log, TEXT("Round started: %.0f s, %d players"), Config->GetTotalSeconds(), Players.Num());
}

void AObshagaGameMode::BeginPhase(ERoundPhase NewPhase)
{
	const UObshagaRoundConfig* Config = GetRoundConfig();
	float Duration = Config->EveningSeconds;
	FText Announcement = LOCTEXT("PhaseEvening", "Вечер. Комендант расслаблен");
	if (NewPhase == ERoundPhase::Night)
	{
		Duration = Config->NightSeconds;
		Announcement = LOCTEXT("PhaseNight", "Ночь. Отбой! Комендант настороже");
	}
	else if (NewPhase == ERoundPhase::Morning)
	{
		Duration = Config->MorningSeconds;
		Announcement = LOCTEXT("PhaseMorning", "Утро. Комендант идёт проверять комнаты");

		// Ночь кончилась: тех, кто ещё гуляет, загоняют обратно.
		for (TActorIterator<ANightExitDoor> It(GetWorld()); It; ++It)
		{
			It->ForceReturn();
		}
	}

	GetGameState<AObshagaGameState>()->SetPhase(NewPhase, Duration);
	GetWorldTimerManager().SetTimer(PhaseTimer, this, &AObshagaGameMode::AdvancePhase, Duration, false);

	if (UGameEventSubsystem* Bus = UGameEventSubsystem::Get(this))
	{
		FGameEvent Event;
		Event.Type = EGameEventType::PhaseChanged;
		Bus->Publish(Event);
	}
	NotifyAll(Announcement);
	UE_LOG(LogObshaga, Log, TEXT("Phase: %s (%.0f s)"), *UEnum::GetValueAsString(NewPhase), Duration);
}

void AObshagaGameMode::AdvancePhase()
{
	switch (GetGameState<AObshagaGameState>()->GetPhase())
	{
	case ERoundPhase::Evening:
		BeginPhase(ERoundPhase::Night);
		break;
	case ERoundPhase::Night:
		BeginPhase(ERoundPhase::Morning);
		break;
	default:
		EndRound();
		break;
	}
}

void AObshagaGameMode::UpdateLiveTaskStatus()
{
	for (const AObshagaPlayerState* PlayerState : GetObshagaPlayers())
	{
		PlayerState->GetTaskComponent()->UpdateLiveStatus();
	}
}

void AObshagaGameMode::SendSms()
{
	const TArray<AObshagaPlayerState*> Players = GetObshagaPlayers();
	for (AObshagaPlayerState* PlayerState : Players)
	{
		PlayerState->AddSms(MakeSmsFor(PlayerState, Players));
		NotifyPlayer(PlayerState, LOCTEXT("NewSms", "Новое СМС — открой телефон [Tab]"));
	}

	if (++SmsSent >= GetRoundConfig()->SmsCount)
	{
		GetWorldTimerManager().ClearTimer(SmsTimer);
	}
}

FText AObshagaGameMode::MakeSmsFor(const AObshagaPlayerState* Reader, const TArray<AObshagaPlayerState*>& Players) const
{
	const FText Quiet = LOCTEXT("SmsQuiet", "Сегодня в общаге подозрительно тихо...");
	const FText Verbs[] = {
		LOCTEXT("SmsCarried", "что-то тащил"),
		LOCTEXT("SmsHid", "что-то прятал"),
		LOCTEXT("SmsNoise", "шумел"),
		LOCTEXT("SmsLurked", "крутился без дела")
	};
	auto Compose = [&Verbs](const APlayerState* Who, int32 VerbIndex, const ARoomVolume* Room)
	{
		return FText::Format(LOCTEXT("SmsFormat", "Кто-то видел, как {0} {1} ({2})"),
			FText::FromString(Who->GetPlayerName()), Verbs[VerbIndex], Room->DisplayName);
	};

	const TArray<AObshagaPlayerState*> Others = Players.FilterByPredicate([Reader](const AObshagaPlayerState* Other) { return Other != Reader; });
	if (Others.IsEmpty())
	{
		return Quiet;
	}

	// Параноику приходят выдумки: случайный сосед, случайное дело, случайная комната.
	if (Reader->GetTrueRole() == EPlayerRole::Paranoid)
	{
		// Но только такие, какие бывают и в правдивых СМС, — иначе по нелепой фразе он вычислил бы свою роль:
		// «прятал» — лишь там, где есть тайники, «крутился» — лишь в общих комнатах.
		TSet<const ARoomVolume*> RoomsWithSpots;
		for (TActorIterator<AHidingSpot> It(GetWorld()); It; ++It)
		{
			RoomsWithSpots.Add(ARoomVolume::FindRoomAt(this, It->GetActorLocation() + FVector(0.f, 0.f, 50.f)));
		}

		TArray<TPair<int32, const ARoomVolume*>> Lies;
		for (TActorIterator<ARoomVolume> It(GetWorld()); It; ++It)
		{
			const ARoomVolume* Room = *It;
			Lies.Emplace(0, Room);
			Lies.Emplace(2, Room);
			if (RoomsWithSpots.Contains(Room))
			{
				Lies.Emplace(1, Room);
			}
			if (Room->RoomType != ERoomType::Corridor && Room->RoomType != ERoomType::Stairs && Room->RoomType != ERoomType::Bedroom)
			{
				Lies.Emplace(3, Room);
			}
		}
		if (Lies.IsEmpty())
		{
			return Quiet;
		}
		const TPair<int32, const ARoomVolume*>& Lie = Lies[FMath::RandRange(0, Lies.Num() - 1)];
		return Compose(Others[FMath::RandRange(0, Others.Num() - 1)], Lie.Key, Lie.Value);
	}

	// Остальным — правда: случайное настоящее событие с участием другого игрока.
	const UGameEventSubsystem* Bus = UGameEventSubsystem::Get(this);
	TArray<FText> Rumors;
	for (const FGameEvent& Event : Bus->GetEventLog())
	{
		const AObshagaPlayerState* Who = Cast<AObshagaPlayerState>(Event.Instigator);
		const ARoomVolume* Room = ARoomVolume::FindRoomById(this, Event.RoomId);
		if (!Who || Who == Reader || !Room)
		{
			continue;
		}

		switch (Event.Type)
		{
		case EGameEventType::ItemPickedUp:
			Rumors.Add(Compose(Who, 0, Room));
			break;
		case EGameEventType::ItemHidden:
			Rumors.Add(Compose(Who, 1, Room));
			break;
		case EGameEventType::Noise:
			Rumors.Add(Compose(Who, 2, Room));
			break;
		case EGameEventType::RoomEntered:
			// «Крутился» — только про общие комнаты, не про коридоры и не про чью-то спальню.
			if (Room->RoomType != ERoomType::Corridor && Room->RoomType != ERoomType::Stairs && Room->RoomType != ERoomType::Bedroom)
			{
				Rumors.Add(Compose(Who, 3, Room));
			}
			break;
		default:
			break;
		}
	}
	return Rumors.IsEmpty() ? Quiet : Rumors[FMath::RandRange(0, Rumors.Num() - 1)];
}

void AObshagaGameMode::TipOff(AObshagaPlayerState* Rat)
{
	const AObshagaGameState* State = GetGameState<AObshagaGameState>();
	if (!Rat || !State || State->GetRoundState() != ERoundState::InProgress
		|| Rat->GetTrueRole() != EPlayerRole::Rat || Rat->HasUsedTip() || Rat->IsEvicted())
	{
		return;
	}

	APlayerState* Target = Rat->GetRatTarget();
	AObshagaCharacter* TargetCharacter = Target ? Cast<AObshagaCharacter>(Target->GetPawn()) : nullptr;
	if (!TargetCharacter)
	{
		return;
	}

	// Стук один на раунд, поэтому впустую он не тратится: сначала убеждаемся, что от него будет толк.
	const AObshagaPlayerState* TargetState = Cast<AObshagaPlayerState>(Target);
	if (TargetCharacter->IsGhost() || (TargetState && TargetState->IsEvicted()))
	{
		NotifyPlayer(Rat, LOCTEXT("TipEvicted", "Его уже выселили — стучать не на кого"));
		return;
	}

	// Комендант идёт туда, где жертва находится прямо сейчас.
	bool bTaken = false;
	for (TActorIterator<AKomendantAIController> It(GetWorld()); It; ++It)
	{
		bTaken |= It->InvestigateTip(TargetCharacter->GetActorLocation());
	}
	if (!bTaken)
	{
		NotifyPlayer(Rat, LOCTEXT("TipBusy", "Комендант занят — стукни позже"));
		return;
	}

	Rat->MarkTipUsed();
	TipRat = Rat;
	TipTarget = Target;
	TipExpireTime = GetWorld()->GetTimeSeconds() + GetRoundConfig()->RatTipWindowSeconds;

	if (UGameEventSubsystem* Bus = UGameEventSubsystem::Get(this))
	{
		FGameEvent Event;
		Event.Type = EGameEventType::TipOff;
		Event.Instigator = Rat;
		Event.Target = Target;
		const ARoomVolume* Room = TargetCharacter->GetCurrentRoom();
		Event.RoomId = Room ? Room->RoomId : NAME_None;
		Bus->Publish(Event);
	}
	NotifyPlayer(Rat, LOCTEXT("TipDone", "Ты настучал. Комендант пошёл проверять"));
}

void AObshagaGameMode::Accuse(AObshagaPlayerState* Accuser, AObshagaCharacter* Suspect)
{
	const AObshagaGameState* State = GetGameState<AObshagaGameState>();
	const UObshagaRoundConfig* Config = GetRoundConfig();
	AObshagaCharacter* AccuserCharacter = Accuser ? Cast<AObshagaCharacter>(Accuser->GetPawn()) : nullptr;
	AObshagaPlayerState* SuspectState = Suspect ? Suspect->GetPlayerState<AObshagaPlayerState>() : nullptr;
	if (!State || State->GetRoundState() != ERoundState::InProgress || !AccuserCharacter || !SuspectState || SuspectState == Accuser
		|| !Accuser->CanAccuse() || Accuser->IsEvicted() || AccuserCharacter->IsHiding() || AccuserCharacter->IsFrozen()
		|| Suspect->IsHiding() || Suspect->IsGhost())
	{
		return;
	}

	// Клиенту не верим: показать можно только на того, кто рядом и на виду.
	const float MaxDistance = Config->AccuseDistance + UInteractionComponent::ServerRangeSlack;
	const FTaskRow* Row = Accuser->GetTaskComponent()->FindRowByCondition(ETaskCondition::CorrectAccusation);
	if (!Row || FVector::Dist(AccuserCharacter->GetActorLocation(), Suspect->GetActorLocation()) > MaxDistance
		|| !AccuserCharacter->GetInteractionComponent()->HasLineOfSight(Suspect))
	{
		return;
	}

	// Коменданту некогда слушать — попытка не тратится. От виновности это не зависит, так что ничего не выдаёт.
	bool bCanListen = false;
	for (TActorIterator<AKomendantAIController> It(GetWorld()); It; ++It)
	{
		bCanListen |= It->CanTakeTip();
	}
	if (!bCanListen)
	{
		NotifyPlayer(Accuser, LOCTEXT("AccuseBusy", "Комендант занят — скажи позже"));
		return;
	}

	// Обвинить можно один раз за раунд. Верно ли обвинение, решается сейчас:
	// украденный предмет (или ничего) записывается в событие, и задание читает его оттуда.
	Accuser->SetTaskAbilities(false, Accuser->CanTipRoom());
	AItemActor* StolenItem = UTaskComponent::FindStolenItem(this, SuspectState, *Row);
	UGameEventSubsystem::PublishFrom(AccuserCharacter, EGameEventType::Accusation, StolenItem, Suspect);

	if (StolenItem)
	{
		SuspectState->GetSuspicionComponent()->AddSuspicion(Config->AccusationSuspicion);
		for (TActorIterator<AKomendantAIController> It(GetWorld()); It; ++It)
		{
			It->InvestigateTip(Suspect->GetActorLocation());
		}
		NotifyPlayer(Accuser, LOCTEXT("AccuseRight", "Комендант поверил и пошёл разбираться"));
	}
	else
	{
		Accuser->GetSuspicionComponent()->AddSuspicion(Config->FalseAccusationSuspicion);
		NotifyPlayer(Accuser, LOCTEXT("AccuseWrong", "Комендант не поверил. Теперь он косится на тебя"));
	}
}

void AObshagaGameMode::TipOffRoom(AObshagaPlayerState* By)
{
	const AObshagaGameState* State = GetGameState<AObshagaGameState>();
	AObshagaCharacter* Character = By ? Cast<AObshagaCharacter>(By->GetPawn()) : nullptr;
	if (!State || State->GetRoundState() != ERoundState::InProgress || !Character || !By->CanTipRoom() || By->IsEvicted() || Character->IsGhost())
	{
		return;
	}
	if (Character->IsHiding())
	{
		NotifyPlayer(By, LOCTEXT("RoomTipHiding", "Сначала вылези из укрытия"));
		return;
	}
	if (Character->IsFrozen())
	{
		NotifyPlayer(By, LOCTEXT("RoomTipFrozen", "Не на допросе же стучать"));
		return;
	}

	const ARoomVolume* Room = Character->GetCurrentRoom();
	if (!Room || Room->RoomType != ERoomType::Bedroom)
	{
		NotifyPlayer(By, LOCTEXT("RoomTipNotBedroom", "Стучать можно только на жилую комнату — зайди в неё"));
		return;
	}
	if (Room->RoomId == By->GetHomeRoomId())
	{
		NotifyPlayer(By, LOCTEXT("RoomTipOwn", "На свою комнату стучать незачем"));
		return;
	}

	// Настучать можно один раз за раунд.
	By->SetTaskAbilities(By->CanAccuse(), false);
	for (TActorIterator<AKomendantAIController> It(GetWorld()); It; ++It)
	{
		It->InspectRoom(Room->RoomId);
	}
	UGameEventSubsystem::PublishFrom(Character, EGameEventType::RoomTipOff);
	NotifyPlayer(By, FText::Format(LOCTEXT("RoomTipDone", "Ты настучал: {0}. Комендант придёт с обыском"), Room->DisplayName));
}

void AObshagaGameMode::Evict(AObshagaPlayerState* PlayerState, AObshagaCharacter* Character)
{
	PlayerState->SetEvicted(true);
	PlayerState->SetScore(PlayerState->GetScore() - GetRoundConfig()->EvictionPenalty);
	if (Character)
	{
		Character->SetGhost(true);
		UGameEventSubsystem::PublishFrom(Character, EGameEventType::PlayerEvicted);
	}
	NotifyAll(FText::Format(LOCTEXT("Evicted", "{0} выселен из общаги!"), FText::FromString(PlayerState->GetPlayerName())));
	UE_LOG(LogObshaga, Verbose, TEXT("%s evicted"), *PlayerState->GetPlayerName());
}

bool AObshagaGameMode::StartInterrogation(AObshagaCharacter* Suspect, const FVector& EvidenceLocation)
{
	AObshagaGameState* State = GetGameState<AObshagaGameState>();
	AObshagaPlayerState* PlayerState = Suspect ? Suspect->GetPlayerState<AObshagaPlayerState>() : nullptr;
	if (!State || !PlayerState || State->GetRoundState() != ERoundState::InProgress || State->GetInterrogation().bActive)
	{
		return false;
	}

	// Что было в руках: запрещёнку комендант забирает на вахту, остальное падает на месте.
	bSuspectHadContraband = false;
	UCarryComponent* Carry = Suspect->GetCarryComponent();
	AItemActor* CaughtWith = Carry->GetCarriedItem();
	if (AItemActor* Item = CaughtWith)
	{
		bSuspectHadContraband = Item->GetItemData()->bContraband;
		if (bSuspectHadContraband)
		{
			Carry->ReleaseForHiding();
			Item->ReleaseToWorld(EvidenceLocation, FVector::ZeroVector);
			Item->ForgetCarrier();
			UGameEventSubsystem::PublishFrom(Suspect, EGameEventType::ContrabandConfiscated, Item);
		}
		else
		{
			Carry->Drop();
		}
	}

	Suspect->SetFrozen(true);
	InterrogatedCharacter = Suspect;
	AlibiBy.Reset();
	CaughtThisRound.AddUnique(PlayerState);

	const float Duration = GetRoundConfig()->InterrogationSeconds;
	FInterrogationInfo Info;
	Info.bActive = true;
	Info.Suspect = PlayerState;
	Info.EndServerTime = static_cast<float>(State->GetServerWorldTimeSeconds()) + Duration;
	State->SetInterrogation(Info);

	GetWorldTimerManager().SetTimer(InterrogationTimer, this, &AObshagaGameMode::ResolveInterrogation, Duration, false);
	UGameEventSubsystem::PublishFrom(Suspect, EGameEventType::PlayerCaught, CaughtWith);
	UE_LOG(LogObshaga, Verbose, TEXT("Interrogation started: %s (contraband=%d)"), *PlayerState->GetPlayerName(), bSuspectHadContraband ? 1 : 0);

	// Поймали того, на кого недавно стучала Крыса, — ей бонус.
	AObshagaPlayerState* Rat = TipRat.Get();
	if (Rat && TipTarget == PlayerState && GetWorld()->GetTimeSeconds() <= TipExpireTime)
	{
		// Очки видны всем, поэтому бонус начисляется только на итогах: скачок посреди раунда выдал бы Крысу.
		Rat->AddPendingRatBonus(GetRoundConfig()->RatTipBonus);
		NotifyPlayer(Rat, FText::Format(LOCTEXT("TipWorked", "Стук сработал: +{0} очков на итогах"), FText::AsNumber(GetRoundConfig()->RatTipBonus)));
		if (UGameEventSubsystem* Bus = UGameEventSubsystem::Get(this))
		{
			FGameEvent Event;
			Event.Type = EGameEventType::TipSucceeded;
			Event.Instigator = Rat;
			Event.Target = PlayerState;
			Bus->Publish(Event);
		}
		TipTarget.Reset();
	}
	return true;
}

void AObshagaGameMode::SubmitInterrogationChoice(AObshagaPlayerState* PlayerState, EInterrogationChoice Choice)
{
	AObshagaGameState* State = GetGameState<AObshagaGameState>();
	FInterrogationInfo Info = State->GetInterrogation();
	if (!Info.bActive || Info.Suspect != PlayerState || Info.Choice != EInterrogationChoice::None || Choice == EInterrogationChoice::None)
	{
		return;
	}

	Info.Choice = Choice;
	State->SetInterrogation(Info);

	// Ложь проверяется в конце окна: друзьям нужно время подтвердить алиби. Остальное решается сразу.
	if (Choice != EInterrogationChoice::Lie)
	{
		ResolveInterrogation();
	}
}

void AObshagaGameMode::ConfirmAlibi(AObshagaCharacter* By)
{
	AObshagaGameState* State = GetGameState<AObshagaGameState>();
	FInterrogationInfo Info = State->GetInterrogation();
	const AObshagaCharacter* Suspect = InterrogatedCharacter.Get();
	if (!Info.bActive || !Suspect || !By || By == Suspect || By->IsHiding() || By->IsGhost() || AlibiBy.Contains(By)
		|| FVector::Dist(By->GetActorLocation(), Suspect->GetActorLocation()) > GetRoundConfig()->AlibiRadius)
	{
		return;
	}

	AlibiBy.Add(By);
	Info.AlibiCount = static_cast<uint8>(AlibiBy.Num());
	State->SetInterrogation(Info);

	NotifyPlayer(By->GetPlayerState(), LOCTEXT("AlibiGiven", "Ты подтвердил алиби"));
	NotifyPlayer(Info.Suspect, LOCTEXT("AlibiReceived", "За тебя вступились! Соври — шансы выше"));
}

void AObshagaGameMode::ResolveInterrogation()
{
	AObshagaGameState* State = GetGameState<AObshagaGameState>();
	const FInterrogationInfo Info = State ? State->GetInterrogation() : FInterrogationInfo();
	if (!Info.bActive)
	{
		return;
	}
	GetWorldTimerManager().ClearTimer(InterrogationTimer);

	const UObshagaRoundConfig* Config = GetRoundConfig();
	AObshagaPlayerState* PlayerState = Cast<AObshagaPlayerState>(Info.Suspect);
	AObshagaCharacter* Suspect = InterrogatedCharacter.Get();

	// Не успел выбрать — считается, что промолчал.
	const EInterrogationChoice Choice = Info.Choice == EInterrogationChoice::None ? EInterrogationChoice::Silent : Info.Choice;

	bool bStrike = true;
	int32 Penalty = 0;
	EGameEventType EventType = EGameEventType::InterrogationSilent;
	FText Result;
	switch (Choice)
	{
	case EInterrogationChoice::Confess:
		Penalty = Config->ConfessPenalty;
		EventType = EGameEventType::InterrogationConfessed;
		Result = LOCTEXT("ResultConfess", "Сознался: страйк и −{0} очков");
		break;

	case EInterrogationChoice::Lie:
	{
		float Chance = Config->LieBaseChance + Config->AlibiBonus * AlibiBy.Num();
		if (bSuspectHadContraband)
		{
			Chance -= Config->ContrabandLiePenalty;
		}
		if (FMath::FRand() < FMath::Clamp(Chance, 0.05f, 0.95f))
		{
			bStrike = false;
			EventType = EGameEventType::InterrogationLieSucceeded;
			Result = LOCTEXT("ResultLieOk", "Комендант поверил! Ни страйка, ни штрафа");
		}
		else
		{
			Penalty = Config->FailedLiePenalty;
			EventType = EGameEventType::InterrogationLieFailed;
			Result = LOCTEXT("ResultLieFail", "Не поверил: страйк и −{0} очков");
		}
		break;
	}

	default:
		Penalty = Config->SilentPenalty;
		Result = LOCTEXT("ResultSilent", "Промолчал: страйк и −{0} очков");
		break;
	}

	bool bEvict = false;
	if (PlayerState)
	{
		USuspicionComponent* Suspicion = PlayerState->GetSuspicionComponent();
		if (bStrike)
		{
			Suspicion->AddStrike();
		}
		Suspicion->SetSuspicion(Config->SuspicionAfterStrike);
		PlayerState->SetScore(PlayerState->GetScore() - Penalty);
		NotifyPlayer(PlayerState, FText::Format(Result, FText::AsNumber(Penalty)));
		bEvict = Suspicion->GetStrikes() >= Config->EvictionStrikes && !PlayerState->IsEvicted();
		UE_LOG(LogObshaga, Verbose, TEXT("Interrogation of %s resolved: %s, strike=%d, penalty=%d, alibi=%d"),
			*PlayerState->GetPlayerName(), *UEnum::GetValueAsString(Choice), bStrike ? 1 : 0, Penalty, AlibiBy.Num());
	}

	if (Suspect)
	{
		Suspect->SetFrozen(false);
		UGameEventSubsystem::PublishFrom(Suspect, EventType);

		// Алиби засчитывается только если ложь удалась.
		for (const TWeakObjectPtr<AObshagaCharacter>& Helper : AlibiBy)
		{
			if (Helper.IsValid() && EventType == EGameEventType::InterrogationLieSucceeded)
			{
				UGameEventSubsystem::PublishFrom(Helper.Get(), EGameEventType::AlibiConfirmed, nullptr, Suspect);
				NotifyPlayer(Helper->GetPlayerState(), LOCTEXT("AlibiWorked", "Твоё алиби сработало"));
			}
		}
	}

	InterrogatedCharacter.Reset();
	AlibiBy.Reset();
	State->SetInterrogation(FInterrogationInfo());

	if (bEvict)
	{
		Evict(PlayerState, Suspect);
	}
}

void AObshagaGameMode::EndRound()
{
	// Незаконченный допрос закрываем до подсчёта очков.
	ResolveInterrogation();

	AObshagaGameState* State = GetGameState<AObshagaGameState>();
	GetWorldTimerManager().ClearTimer(PhaseTimer);
	GetWorldTimerManager().ClearTimer(LiveStatusTimer);
	GetWorldTimerManager().ClearTimer(SmsTimer);
	GetWorldTimerManager().ClearTimer(SelfBreakTimer);

	// Подводим итоги и раскрываем всем, кто кем был и что делал.
	const TArray<AObshagaPlayerState*> Players = GetObshagaPlayers();
	TArray<FRevealedTask> RevealedTasks;
	TArray<FRevealedPlayer> RevealedPlayers;
	for (AObshagaPlayerState* PlayerState : Players)
	{
		const int32 Earned = PlayerState->GetTaskComponent()->FinalizeTasks() + PlayerState->TakePendingRatBonus();
		PlayerState->SetScore(PlayerState->GetScore() + Earned);
		PlayerState->ForceNetUpdate();

		for (const FTaskState& Task : PlayerState->GetTaskComponent()->GetTasks())
		{
			FRevealedTask& Line = RevealedTasks.AddDefaulted_GetRef();
			Line.PlayerName = PlayerState->GetPlayerName();
			Line.TaskTitle = Task.Title;
			Line.Status = Task.Status;
			Line.Reward = Task.Reward;
		}

		FRevealedPlayer& Revealed = RevealedPlayers.AddDefaulted_GetRef();
		Revealed.PlayerName = PlayerState->GetPlayerName();
		Revealed.Role = PlayerState->GetTrueRole();
		Revealed.Score = FMath::RoundToInt32(PlayerState->GetScore());
		Revealed.Strikes = PlayerState->GetSuspicionComponent()->GetStrikes();
		Revealed.bEvicted = PlayerState->IsEvicted();
	}
	AssignTitles(RevealedPlayers, Players);
	WriteTelemetry(Players);

	State->FinishRound(RevealedTasks, RevealedPlayers, BuildChronicle());
	if (UGameEventSubsystem* Bus = UGameEventSubsystem::Get(this))
	{
		FGameEvent Event;
		Event.Type = EGameEventType::RoundEnded;
		Bus->Publish(Event);
	}
	UE_LOG(LogObshaga, Log, TEXT("Round ended"));
}

void AObshagaGameMode::AssignTitles(TArray<FRevealedPlayer>& Players, const TArray<AObshagaPlayerState*>& States) const
{
	// Метрики раунда считаем по журналу событий.
	const UGameEventSubsystem* Bus = UGameEventSubsystem::Get(this);
	auto Count = [Bus](const APlayerState* Who, EGameEventType Type)
	{
		int32 Result = 0;
		for (const FGameEvent& Event : Bus->GetEventLog())
		{
			Result += (Event.Type == Type && Event.Instigator == Who) ? 1 : 0;
		}
		return Result;
	};

	int32 BestScore = TNumericLimits<int32>::Min();
	int32 NumBest = 0;
	for (const FRevealedPlayer& Player : Players)
	{
		if (Player.Score > BestScore)
		{
			BestScore = Player.Score;
			NumBest = 1;
		}
		else if (Player.Score == BestScore)
		{
			++NumBest;
		}
	}

	for (int32 Index = 0; Index < Players.Num(); ++Index)
	{
		FRevealedPlayer& Player = Players[Index];
		const APlayerState* State = States[Index];
		const int32 Caught = Count(State, EGameEventType::PlayerCaught);

		// К каждому титулу — подпись для скриншота.
		if (Player.bEvicted)
		{
			Player.Title = LOCTEXT("TitleEvicted", "Выселен с вещами");
			Player.Caption = LOCTEXT("CapEvicted", "Чемодан, вокзал, родители.");
		}
		else if (Player.Score == BestScore && NumBest == 1 && BestScore > 0)
		{
			Player.Title = LOCTEXT("TitleKing", "Король общаги");
			Player.Caption = LOCTEXT("CapKing", "Корона из фольги, зато своя.");
		}
		else if (Count(State, EGameEventType::TipSucceeded) > 0)
		{
			Player.Title = LOCTEXT("TitleSnitch", "Главный стукач");
			Player.Caption = LOCTEXT("CapSnitch", "Комендант передаёт привет и печенье.");
		}
		else if (Count(State, EGameEventType::PlayerFramed) > 0)
		{
			Player.Title = LOCTEXT("TitleFramer", "Мастер подстав");
			Player.Caption = LOCTEXT("CapFramer", "Руки чистые, совесть — как получится.");
		}
		else if (Caught == 0 && Count(State, EGameEventType::PlayerSpotted) == 0)
		{
			Player.Title = LOCTEXT("TitleSaint", "Святой");
			Player.Caption = LOCTEXT("CapSaint", "Комендант до сих пор не уверен, что этот жилец существует.");
		}
		else if (Caught >= 2)
		{
			Player.Title = LOCTEXT("TitleFavorite", "Любимчик коменданта");
			Player.Caption = LOCTEXT("CapFavorite", "На вахте уже знают, какой чай он пьёт.");
		}
		else if (Count(State, EGameEventType::AlibiConfirmed) > 0)
		{
			Player.Title = LOCTEXT("TitleFriend", "Настоящий друг");
			Player.Caption = LOCTEXT("CapFriend", "Соврал коменданту в глаза — ради другого.");
		}
		else if (Count(State, EGameEventType::ItemPickedUp) >= 3)
		{
			Player.Title = LOCTEXT("TitleThief", "Лучший вор");
			Player.Caption = LOCTEXT("CapThief", "Что плохо лежит — уже не лежит.");
		}
		else if (Count(State, EGameEventType::KomendantAlerted) >= 2)
		{
			// Паника — это шум, на который комендант действительно пришёл; скрип двери не в счёт.
			Player.Title = LOCTEXT("TitlePanic", "Паникёр");
			Player.Caption = LOCTEXT("CapPanic", "Громче него в общаге только будильник.");
		}
		else
		{
			Player.Title = LOCTEXT("TitleQuiet", "Тихоня");
			Player.Caption = LOCTEXT("CapQuiet", "Сидел тихо. Подозрительно тихо.");
		}
	}
}

TArray<FText> AObshagaGameMode::BuildChronicle() const
{
	struct FLine
	{
		float Time = 0.f;
		int32 Priority = 0;
		FText Text;
	};

	auto Name = [](const APlayerState* Who) { return Who ? FText::FromString(Who->GetPlayerName()) : LOCTEXT("Someone", "Кто-то"); };
	auto Pick = [](std::initializer_list<FText> Variants) { return *(Variants.begin() + FMath::RandRange(0, static_cast<int32>(Variants.size()) - 1)); };

	// Шаблоны строк; чем выше приоритет, тем важнее событие для хроники.
	TArray<FLine> Lines;
	const UGameEventSubsystem* Bus = UGameEventSubsystem::Get(this);
	for (const FGameEvent& Event : Bus->GetEventLog())
	{
		const ARoomVolume* Room = ARoomVolume::FindRoomById(this, Event.RoomId);
		const FText RoomName = Room ? Room->DisplayName : LOCTEXT("Somewhere", "где-то");
		const FText ItemName = Event.Item ? Event.Item->GetDisplayName() : FText::GetEmpty();
		const FText Who = Name(Event.Instigator);
		const FText Whom = Name(Event.Target);

		FLine Line;
		// У каждого события несколько вариантов фразы — выбирается случайный, чтобы хроника не повторялась из раунда в раунд.
		switch (Event.Type)
		{
		case EGameEventType::ItemPickedUp:
			// В хронику идут только заметные кражи: тяжёлое и запрещёнка.
			if (!Event.Item || !(Event.Item->GetItemData()->bHeavy || Event.Item->GetItemData()->bContraband))
			{
				continue;
			}
			Line.Priority = 1;
			Line.Text = FText::Format(Pick({
				LOCTEXT("ChrPickedUp1", "{0} утащил: {1} ({2})"),
				LOCTEXT("ChrPickedUp2", "{0} решил, что {1} ему нужнее ({2})"),
				LOCTEXT("ChrPickedUp3", "{2}: {1} уходит в руках {0}. Никто ничего не видел") }), Who, ItemName, RoomName);
			break;
		case EGameEventType::ItemHidden:
			Line.Priority = 2;
			Line.Text = FText::Format(Pick({
				LOCTEXT("ChrHidden1", "{0} спрятал: {1} ({2})"),
				LOCTEXT("ChrHidden2", "{0} запихнул {1} поглубже ({2}) и сделал честное лицо"),
				LOCTEXT("ChrHidden3", "{1} теперь живёт в тайнике ({2}). Спасибо, {0}") }), Who, ItemName, RoomName);
			break;
		case EGameEventType::PlayerFoundHiding:
			Line.Priority = 3;
			Line.Text = FText::Format(Pick({
				LOCTEXT("ChrFoundHiding1", "{0} открыл шкаф, а там {1}"),
				LOCTEXT("ChrFoundHiding2", "{0} нашёл в укрытии {1}. Неловко вышло"),
				LOCTEXT("ChrFoundHiding3", "{1} сидел тихо, пока не пришёл {0}") }), Who, Whom);
			break;
		case EGameEventType::ContrabandConfiscated:
			Line.Priority = 4;
			Line.Text = FText::Format(Pick({
				LOCTEXT("ChrConfiscated1", "Комендант изъял запрещёнку. Привет, {0}"),
				LOCTEXT("ChrConfiscated2", "Комендант нашёл тайник ({1}). {0}, это было хорошее место"),
				LOCTEXT("ChrConfiscated3", "Запрещёнка переехала на вахту. {0} скорбит") }), Who, RoomName);
			break;
		case EGameEventType::TipOff:
			Line.Priority = 4;
			Line.Text = FText::Format(Pick({
				LOCTEXT("ChrTipOff1", "{0} настучал коменданту на {1}"),
				LOCTEXT("ChrTipOff2", "{0} шепнул коменданту пару слов про {1}"),
				LOCTEXT("ChrTipOff3", "Анонимный звонок на вахту. Очень похоже на голос {0}") }), Who, Whom);
			break;
		case EGameEventType::TipSucceeded:
			Line.Priority = 5;
			Line.Text = FText::Format(Pick({
				LOCTEXT("ChrTipWorked1", "Стук сработал: {1} попался, {0} доволен"),
				LOCTEXT("ChrTipWorked2", "{0} сдал {1} — и не прогадал") }), Who, Whom);
			break;
		case EGameEventType::PlayerCaught:
			Line.Priority = 5;
			Line.Text = Event.Item
				? FText::Format(Pick({
					LOCTEXT("ChrCaughtItem1", "{0} пойман ({1}). В руках — {2}. Он не знает, как оно там оказалось"),
					LOCTEXT("ChrCaughtItem2", "{0} и {2} встретили коменданта ({1})"),
					LOCTEXT("ChrCaughtItem3", "Комендант, {0}, {2}. Немая сцена ({1})") }), Who, RoomName, ItemName)
				: FText::Format(Pick({
					LOCTEXT("ChrCaught1", "{0} пойман комендантом ({1})"),
					LOCTEXT("ChrCaught2", "{0} не успел убежать ({1})"),
					LOCTEXT("ChrCaught3", "«А ты куда собрался?» — комендант, обращаясь к {0} ({1})") }), Who, RoomName);
			break;
		case EGameEventType::InterrogationConfessed:
			Line.Priority = 3;
			Line.Text = FText::Format(Pick({
				LOCTEXT("ChrConfessed1", "{0} во всём сознался"),
				LOCTEXT("ChrConfessed2", "{0} раскололся за три секунды"),
				LOCTEXT("ChrConfessed3", "{0}: «Да, это я. И мне не стыдно»") }), Who);
			break;
		case EGameEventType::InterrogationSilent:
			Line.Priority = 3;
			Line.Text = FText::Format(Pick({
				LOCTEXT("ChrSilent1", "{0} молчал как партизан"),
				LOCTEXT("ChrSilent2", "{0} смотрел в пол и считал плитку"),
				LOCTEXT("ChrSilent3", "На допросе {0} не сказал ни слова. Не помогло") }), Who);
			break;
		case EGameEventType::InterrogationLieSucceeded:
			Line.Priority = 5;
			Line.Text = FText::Format(Pick({
				LOCTEXT("ChrLieOk1", "{0} соврал коменданту — и тот поверил"),
				LOCTEXT("ChrLieOk2", "{0} нёс полную чушь. Комендант кивал"),
				LOCTEXT("ChrLieOk3", "«Я просто шёл в туалет», — сказал {0}. Прокатило") }), Who);
			break;
		case EGameEventType::InterrogationLieFailed:
			Line.Priority = 4;
			Line.Text = FText::Format(Pick({
				LOCTEXT("ChrLieFail1", "{0} соврал, но комендант не поверил"),
				LOCTEXT("ChrLieFail2", "{0} врал вдохновенно. Комендант слушал с интересом и выписал страйк"),
				LOCTEXT("ChrLieFail3", "Легенда {0} развалилась на втором предложении") }), Who);
			break;
		case EGameEventType::AlibiConfirmed:
			Line.Priority = 5;
			Line.Text = FText::Format(Pick({
				LOCTEXT("ChrAlibi1", "{0} прикрыл {1}. Вот это дружба"),
				LOCTEXT("ChrAlibi2", "«Он был со мной», — {0} про {1}. Комендант поверил обоим"),
				LOCTEXT("ChrAlibi3", "{0} спас {1} от страйка. Должок") }), Who, Whom);
			break;
		case EGameEventType::PlayerEvicted:
			Line.Priority = 6;
			Line.Text = FText::Format(Pick({
				LOCTEXT("ChrEvicted1", "{0} выселен из общаги"),
				LOCTEXT("ChrEvicted2", "{0} собирает вещи. Три страйка — это три страйка"),
				LOCTEXT("ChrEvicted3", "Минус один жилец: {0} отправляется домой к маме") }), Who);
			break;
		case EGameEventType::PlayerFramed:
			Line.Priority = 6;
			Line.Text = FText::Format(Pick({
				LOCTEXT("ChrFramed1", "У {1} нашли запрещёнку. Её подбросил {0}"),
				LOCTEXT("ChrFramed2", "{1} клянётся, что это не его. И он прав: это {0}"),
				LOCTEXT("ChrFramed3", "{0} оставил {1} маленький подарок. Комендант оценил") }), Who, Whom);
			break;
		case EGameEventType::Accusation:
			Line.Priority = 4;
			Line.Text = Event.Item
				? FText::Format(Pick({
					LOCTEXT("ChrAccuseRight1", "{0} показал коменданту на {1}: «Это он украл!» И не ошибся"),
					LOCTEXT("ChrAccuseRight2", "Детектив {0} раскрыл дело: вор — {1}") }), Who, Whom)
				: FText::Format(Pick({
					LOCTEXT("ChrAccuseWrong1", "{0} обвинил {1} в краже. Мимо"),
					LOCTEXT("ChrAccuseWrong2", "{0} ткнул пальцем в {1}. Комендант запомнил самого {0}") }), Who, Whom);
			break;
		case EGameEventType::RoomTipOff:
			Line.Priority = 4;
			Line.Text = FText::Format(Pick({
				LOCTEXT("ChrRoomTip1", "{0} настучал коменданту: «Обыщите — {1}»"),
				LOCTEXT("ChrRoomTip2", "{0} намекнул коменданту, что {1} стоит проверить") }), Who, RoomName);
			break;
		case EGameEventType::NoteRead:
			Line.Priority = 1;
			Line.Text = FText::Format(Pick({
				LOCTEXT("ChrNoteRead1", "{0} прочитал записку со слухом ({1})"),
				LOCTEXT("ChrNoteRead2", "{0} нашёл записку ({1}) и теперь знает лишнее") }), Who, RoomName);
			break;
		case EGameEventType::DeviceBroken:
			Line.Priority = 3;
			Line.Text = Event.Instigator
				? FText::Format(Pick({
					LOCTEXT("ChrBroke1", "{0} что-то сломал ({1})"),
					LOCTEXT("ChrBroke2", "{1}: {0} «просто посмотрел», и оно сломалось") }), Who, RoomName)
				: FText::Format(Pick({
					LOCTEXT("ChrBrokeItself1", "Что-то сломалось само ({0}). Общага, что с неё взять"),
					LOCTEXT("ChrBrokeItself2", "{0}: техника не выдержала и ушла на покой") }), RoomName);
			break;
		case EGameEventType::DeviceRepaired:
			Line.Priority = 3;
			Line.Text = FText::Format(Pick({
				LOCTEXT("ChrRepaired1", "{0} всё починил ({1}). Золотые руки"),
				LOCTEXT("ChrRepaired2", "{0} починил ({1}) изолентой и добрым словом") }), Who, RoomName);
			break;
		case EGameEventType::LeftBuilding:
			Line.Priority = 3;
			Line.Text = FText::Format(Pick({
				LOCTEXT("ChrLeft1", "{0} после отбоя ушёл в ночь"),
				LOCTEXT("ChrLeft2", "{0} вышел «подышать». После отбоя. Ну да") }), Who);
			break;
		case EGameEventType::ReturnedToBuilding:
			Line.Priority = 2;
			Line.Text = FText::Format(Pick({
				LOCTEXT("ChrReturned1", "{0} вернулся с улицы как ни в чём не бывало"),
				LOCTEXT("ChrReturned2", "{0} снова в общаге. Шаурма была вкусная") }), Who);
			break;
		default:
			continue;
		}
		Line.Time = Event.Time;
		Lines.Add(Line);
	}

	// Если событий больше, чем строк на экране, оставляем самые важные и возвращаем порядок по времени.
	const int32 MaxLines = GetRoundConfig()->MaxChronicleLines;
	if (Lines.Num() > MaxLines)
	{
		Lines.StableSort([](const FLine& A, const FLine& B) { return A.Priority > B.Priority; });
		Lines.SetNum(MaxLines);
		Lines.StableSort([](const FLine& A, const FLine& B) { return A.Time < B.Time; });
	}

	TArray<FText> Result;
	for (const FLine& Line : Lines)
	{
		const int32 Seconds = FMath::Max(0, FMath::FloorToInt32(Line.Time - RoundStartWorldTime));
		const FString Stamp = FString::Printf(TEXT("%02d:%02d"), Seconds / 60, Seconds % 60);
		Result.Add(FText::Format(LOCTEXT("ChrLine", "{0} — {1}"), FText::FromString(Stamp), Line.Text));
	}
	return Result;
}

void AObshagaGameMode::WriteTelemetry(const TArray<AObshagaPlayerState*>& Players) const
{
	const UGameEventSubsystem* Bus = UGameEventSubsystem::Get(this);
	if (!Bus)
	{
		return;
	}

	auto NameOf = [](const APlayerState* Who) { return Who ? Who->GetPlayerName() : FString(); };
	auto EnumName = [](const UEnum* Enum, int64 Value) { return Enum->GetNameStringByValue(Value); };

	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("time"), FDateTime::Now().ToIso8601());
	Root->SetNumberField(TEXT("durationSeconds"), GetWorld()->GetTimeSeconds() - RoundStartWorldTime);
	Root->SetNumberField(TEXT("numPlayers"), Players.Num());

	TArray<TSharedPtr<FJsonValue>> PlayersJson;
	for (const AObshagaPlayerState* PlayerState : Players)
	{
		const TSharedRef<FJsonObject> PlayerJson = MakeShared<FJsonObject>();
		PlayerJson->SetStringField(TEXT("name"), PlayerState->GetPlayerName());
		PlayerJson->SetStringField(TEXT("role"), EnumName(StaticEnum<EPlayerRole>(), static_cast<int64>(PlayerState->GetTrueRole())));
		PlayerJson->SetStringField(TEXT("homeRoom"), PlayerState->GetHomeRoomId().ToString());
		PlayerJson->SetNumberField(TEXT("score"), FMath::RoundToInt32(PlayerState->GetScore()));
		PlayerJson->SetNumberField(TEXT("strikes"), PlayerState->GetSuspicionComponent()->GetStrikes());
		PlayerJson->SetBoolField(TEXT("evicted"), PlayerState->IsEvicted());

		TArray<TSharedPtr<FJsonValue>> TasksJson;
		for (const FTaskState& Task : PlayerState->GetTaskComponent()->GetTasks())
		{
			const TSharedRef<FJsonObject> TaskJson = MakeShared<FJsonObject>();
			TaskJson->SetStringField(TEXT("id"), Task.TaskId.ToString());
			TaskJson->SetBoolField(TEXT("main"), Task.bMain);
			TaskJson->SetBoolField(TEXT("completed"), Task.Status == ETaskStatus::Completed);
			TaskJson->SetNumberField(TEXT("reward"), Task.Reward);
			TasksJson.Add(MakeShared<FJsonValueObject>(TaskJson));
		}
		PlayerJson->SetArrayField(TEXT("tasks"), TasksJson);
		PlayersJson.Add(MakeShared<FJsonValueObject>(PlayerJson));
	}
	Root->SetArrayField(TEXT("players"), PlayersJson);

	// Весь журнал событий: по нему видно и поимки, и подставы, и кто куда ходил.
	int32 NumCaught = 0;
	TArray<TSharedPtr<FJsonValue>> EventsJson;
	for (const FGameEvent& Event : Bus->GetEventLog())
	{
		NumCaught += Event.Type == EGameEventType::PlayerCaught ? 1 : 0;

		const TSharedRef<FJsonObject> EventJson = MakeShared<FJsonObject>();
		EventJson->SetNumberField(TEXT("t"), FMath::RoundToInt32(Event.Time - RoundStartWorldTime));
		EventJson->SetStringField(TEXT("type"), EnumName(StaticEnum<EGameEventType>(), static_cast<int64>(Event.Type)));
		if (Event.Instigator)
		{
			EventJson->SetStringField(TEXT("by"), NameOf(Event.Instigator));
		}
		if (Event.Target)
		{
			EventJson->SetStringField(TEXT("target"), NameOf(Event.Target));
		}
		if (Event.Item)
		{
			EventJson->SetStringField(TEXT("item"), Event.Item->GetItemData()->ItemId.ToString());
		}
		if (!Event.RoomId.IsNone())
		{
			EventJson->SetStringField(TEXT("room"), Event.RoomId.ToString());
		}
		EventsJson.Add(MakeShared<FJsonValueObject>(EventJson));
	}
	Root->SetNumberField(TEXT("numCaught"), NumCaught);
	Root->SetArrayField(TEXT("events"), EventsJson);

	FString Text;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
	FJsonSerializer::Serialize(Root, Writer);

	const FString Path = FPaths::ProjectSavedDir() / TEXT("Telemetry") / FString::Printf(TEXT("round_%s.json"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
	if (FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogObshaga, Log, TEXT("Telemetry saved: %s"), *Path);
	}
	else
	{
		UE_LOG(LogObshaga, Warning, TEXT("Telemetry: cannot write %s"), *Path);
	}
}

void AObshagaGameMode::ResetWorldForRematch()
{
	UWorld* World = GetWorld();

	// Сначала всё из рук и из тайников, потом вещи и двери — на исходные места.
	for (TActorIterator<AObshagaCharacter> It(World); It; ++It)
	{
		It->GetCarryComponent()->ReleaseForHiding();
	}
	for (TActorIterator<AHidingSpot> It(World); It; ++It)
	{
		It->ResetSpot();
	}
	for (TActorIterator<AItemActor> It(World); It; ++It)
	{
		It->ResetToInitial();
	}
	for (TActorIterator<ADoorActor> It(World); It; ++It)
	{
		It->ResetDoor();
	}
	for (TActorIterator<ADeviceActor> It(World); It; ++It)
	{
		It->ResetDevice();
	}

	// Все игроки появляются заново и заново расселяются по комнатам: сначала все точки появления
	// освобождаются, потом каждому выбирается новая (ChoosePlayerStart смотрит, какие уже заняты).
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (APlayerController* Controller = It->Get())
		{
			Controller->StartSpot = nullptr;
		}
	}
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* Controller = It->Get();
		if (!Controller)
		{
			continue;
		}
		if (APawn* Pawn = Controller->GetPawn())
		{
			Controller->UnPossess();
			Pawn->Destroy();
		}
		Controller->StartSpot = ChoosePlayerStart(Controller);
		RestartPlayer(Controller);
		AssignHomeRoom(Controller);
	}
	UE_LOG(LogObshaga, Log, TEXT("World reset for rematch"));
}

#undef LOCTEXT_NAMESPACE
