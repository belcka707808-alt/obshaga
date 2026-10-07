#include "ObshagaGameMode.h"

#include "CarryComponent.h"
#include "DoorActor.h"
#include "GameEventSubsystem.h"
#include "HidingSpot.h"
#include "ItemActor.h"
#include "KomendantAIController.h"
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
#include "EngineUtils.h"
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
		TaskDirector->AssignTasksTo(PlayerState, GetNumPlayers());
		NotifyPlayer(PlayerState, LOCTEXT("NewTask", "Новые секретные задания — открой телефон [Tab]"));
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
		UE_LOG(LogObshaga, Verbose, TEXT("%s lives in %s"), *PlayerState->GetPlayerName(), *PlayerState->GetHomeRoomId().ToString());
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
		ResetWorldForRematch();
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
		TaskDirector->AssignTasksTo(PlayerState, Players.Num());
		if (PlayerState->GetTrueRole() != EPlayerRole::Rat)
		{
			NotifyPlayer(PlayerState, LOCTEXT("NewTask", "Новые секретные задания — открой телефон [Tab]"));
		}
	}

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

	State->StartRound(Config->AlibiRadius);
	BeginPhase(ERoundPhase::Evening);

	GetWorldTimerManager().SetTimer(LiveStatusTimer, this, &AObshagaGameMode::UpdateLiveTaskStatus, 1.f, true);
	if (Config->SmsCount > 0)
	{
		const float Interval = Config->GetTotalSeconds() / (Config->SmsCount + 1);
		GetWorldTimerManager().SetTimer(SmsTimer, this, &AObshagaGameMode::SendSms, Interval, true);
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
		TArray<const ARoomVolume*> Rooms;
		for (TActorIterator<ARoomVolume> It(GetWorld()); It; ++It)
		{
			Rooms.Add(*It);
		}
		if (Rooms.IsEmpty())
		{
			return Quiet;
		}
		return Compose(Others[FMath::RandRange(0, Others.Num() - 1)], FMath::RandRange(0, 3), Rooms[FMath::RandRange(0, Rooms.Num() - 1)]);
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

	Rat->MarkTipUsed();
	TipRat = Rat;
	TipTarget = Target;
	TipExpireTime = GetWorld()->GetTimeSeconds() + GetRoundConfig()->RatTipWindowSeconds;

	// Комендант идёт туда, где жертва находится прямо сейчас.
	for (TActorIterator<AKomendantAIController> It(GetWorld()); It; ++It)
	{
		It->InvestigateTip(TargetCharacter->GetActorLocation());
	}

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
	if (AItemActor* Item = Carry->GetCarriedItem())
	{
		bSuspectHadContraband = Item->GetItemData()->bContraband;
		if (bSuspectHadContraband)
		{
			Carry->ReleaseForHiding();
			Item->ReleaseToWorld(EvidenceLocation, FVector::ZeroVector);
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
	UGameEventSubsystem::PublishFrom(Suspect, EGameEventType::PlayerCaught);
	UE_LOG(LogObshaga, Verbose, TEXT("Interrogation started: %s (contraband=%d)"), *PlayerState->GetPlayerName(), bSuspectHadContraband ? 1 : 0);

	// Поймали того, на кого недавно стучала Крыса, — ей бонус.
	AObshagaPlayerState* Rat = TipRat.Get();
	if (Rat && TipTarget == PlayerState && GetWorld()->GetTimeSeconds() <= TipExpireTime)
	{
		Rat->SetScore(Rat->GetScore() + GetRoundConfig()->RatTipBonus);
		NotifyPlayer(Rat, FText::Format(LOCTEXT("TipWorked", "Стук сработал: +{0} очков"), FText::AsNumber(GetRoundConfig()->RatTipBonus)));
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

	// Подводим итоги и раскрываем всем, кто кем был и что делал.
	const TArray<AObshagaPlayerState*> Players = GetObshagaPlayers();
	TArray<FRevealedTask> RevealedTasks;
	TArray<FRevealedPlayer> RevealedPlayers;
	for (AObshagaPlayerState* PlayerState : Players)
	{
		const int32 Earned = PlayerState->GetTaskComponent()->FinalizeTasks();
		PlayerState->SetScore(PlayerState->GetScore() + Earned);

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

		if (Player.bEvicted)
		{
			Player.Title = LOCTEXT("TitleEvicted", "Выселен с вещами");
		}
		else if (Player.Score == BestScore && NumBest == 1 && BestScore > 0)
		{
			Player.Title = LOCTEXT("TitleKing", "Король общаги");
		}
		else if (Count(State, EGameEventType::TipSucceeded) > 0)
		{
			Player.Title = LOCTEXT("TitleSnitch", "Главный стукач");
		}
		else if (Caught == 0 && Count(State, EGameEventType::PlayerSpotted) == 0)
		{
			Player.Title = LOCTEXT("TitleSaint", "Святой");
		}
		else if (Caught >= 2)
		{
			Player.Title = LOCTEXT("TitleFavorite", "Любимчик коменданта");
		}
		else if (Count(State, EGameEventType::Noise) >= 3)
		{
			Player.Title = LOCTEXT("TitlePanic", "Паникёр");
		}
		else if (Count(State, EGameEventType::ItemPickedUp) >= 3)
		{
			Player.Title = LOCTEXT("TitleThief", "Лучший вор");
		}
		else if (Count(State, EGameEventType::AlibiConfirmed) > 0)
		{
			Player.Title = LOCTEXT("TitleFriend", "Настоящий друг");
		}
		else
		{
			Player.Title = LOCTEXT("TitleQuiet", "Тихоня");
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
		switch (Event.Type)
		{
		case EGameEventType::ItemPickedUp:
			// В хронику идут только заметные кражи: тяжёлое и запрещёнка.
			if (!Event.Item || !(Event.Item->GetItemData()->bHeavy || Event.Item->GetItemData()->bContraband))
			{
				continue;
			}
			Line.Priority = 1;
			Line.Text = FText::Format(LOCTEXT("ChrPickedUp", "{0} утащил: {1} ({2})"), Who, ItemName, RoomName);
			break;
		case EGameEventType::ItemHidden:
			Line.Priority = 2;
			Line.Text = FText::Format(LOCTEXT("ChrHidden", "{0} спрятал: {1} ({2})"), Who, ItemName, RoomName);
			break;
		case EGameEventType::PlayerFoundHiding:
			Line.Priority = 3;
			Line.Text = FText::Format(LOCTEXT("ChrFoundHiding", "{0} открыл шкаф, а там {1}"), Who, Whom);
			break;
		case EGameEventType::ContrabandConfiscated:
			Line.Priority = 4;
			Line.Text = FText::Format(LOCTEXT("ChrConfiscated", "Комендант изъял запрещёнку. Привет, {0}"), Who);
			break;
		case EGameEventType::TipOff:
			Line.Priority = 4;
			Line.Text = FText::Format(LOCTEXT("ChrTipOff", "{0} настучал коменданту на {1}"), Who, Whom);
			break;
		case EGameEventType::PlayerCaught:
			Line.Priority = 5;
			Line.Text = FText::Format(LOCTEXT("ChrCaught", "{0} пойман комендантом ({1})"), Who, RoomName);
			break;
		case EGameEventType::InterrogationConfessed:
			Line.Priority = 3;
			Line.Text = FText::Format(LOCTEXT("ChrConfessed", "{0} во всём сознался"), Who);
			break;
		case EGameEventType::InterrogationSilent:
			Line.Priority = 3;
			Line.Text = FText::Format(LOCTEXT("ChrSilent", "{0} молчал как партизан"), Who);
			break;
		case EGameEventType::InterrogationLieSucceeded:
			Line.Priority = 5;
			Line.Text = FText::Format(LOCTEXT("ChrLieOk", "{0} соврал коменданту — и тот поверил"), Who);
			break;
		case EGameEventType::InterrogationLieFailed:
			Line.Priority = 4;
			Line.Text = FText::Format(LOCTEXT("ChrLieFail", "{0} соврал, но комендант не поверил"), Who);
			break;
		case EGameEventType::AlibiConfirmed:
			Line.Priority = 5;
			Line.Text = FText::Format(LOCTEXT("ChrAlibi", "{0} прикрыл {1}. Вот это дружба"), Who, Whom);
			break;
		case EGameEventType::PlayerEvicted:
			Line.Priority = 6;
			Line.Text = FText::Format(LOCTEXT("ChrEvicted", "{0} выселен из общаги"), Who);
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

	// Все игроки появляются заново (и заново получают домашнюю комнату).
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
		RestartPlayer(Controller);
		AssignHomeRoom(Controller);
	}
	UE_LOG(LogObshaga, Log, TEXT("World reset for rematch"));
}

#undef LOCTEXT_NAMESPACE
