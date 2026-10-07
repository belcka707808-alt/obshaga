#include "ObshagaGameMode.h"

#include "CarryComponent.h"
#include "GameEventSubsystem.h"
#include "ItemActor.h"
#include "Obshaga.h"
#include "ObshagaCharacter.h"
#include "ObshagaGameState.h"
#include "ObshagaHUD.h"
#include "ObshagaPlayerController.h"
#include "ObshagaPlayerState.h"
#include "ObshagaRoundConfig.h"
#include "ObshagaItemData.h"
#include "RoomVolume.h"
#include "SuspicionComponent.h"
#include "TaskComponent.h"
#include "TaskDirector.h"
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

	const float Delay = FMath::Max(GetRoundConfig()->StartDelaySeconds, 0.1f);
	GetWorldTimerManager().SetTimer(RoundTimer, this, &AObshagaGameMode::StartRound, Delay, false);
}

void AObshagaGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	Super::HandleStartingNewPlayer_Implementation(NewPlayer);

	// Комната, в которой игрок появился, становится его домашней.
	AObshagaPlayerState* PlayerState = NewPlayer ? NewPlayer->GetPlayerState<AObshagaPlayerState>() : nullptr;
	const APawn* Pawn = NewPlayer ? NewPlayer->GetPawn() : nullptr;
	if (PlayerState && Pawn)
	{
		const ARoomVolume* Room = ARoomVolume::FindRoomAt(this, Pawn->GetActorLocation());
		PlayerState->SetHomeRoomId(Room ? Room->RoomId : NAME_None);
		UE_LOG(LogObshaga, Verbose, TEXT("%s lives in %s"), *PlayerState->GetPlayerName(), *PlayerState->GetHomeRoomId().ToString());
	}

	// Кто вошёл уже во время раунда, тоже получает задание.
	const AObshagaGameState* State = GetGameState<AObshagaGameState>();
	if (PlayerState && State && State->GetRoundState() == ERoundState::InProgress)
	{
		GiveTask(PlayerState);
	}
}

void AObshagaGameMode::GiveTask(AObshagaPlayerState* PlayerState)
{
	const FName TaskId = TaskDirector->AssignTaskTo(PlayerState, GetNumPlayers());
	if (TaskId.IsNone())
	{
		return;
	}

	NotifyPlayer(PlayerState, LOCTEXT("NewTask", "Новое секретное задание — открой телефон [Tab]"));
}

void AObshagaGameMode::StartRound()
{
	AObshagaGameState* State = GetGameState<AObshagaGameState>();
	if (!State)
	{
		return;
	}

	TaskDirector->Reset();
	if (UGameEventSubsystem* Bus = UGameEventSubsystem::Get(this))
	{
		Bus->ClearEventLog();
		FGameEvent Event;
		Event.Type = EGameEventType::RoundStarted;
		Bus->Publish(Event);
	}

	for (APlayerState* Player : State->PlayerArray)
	{
		if (AObshagaPlayerState* PlayerState = Cast<AObshagaPlayerState>(Player))
		{
			GiveTask(PlayerState);
		}
	}

	const float Duration = GetRoundConfig()->RoundSeconds;
	State->StartRound(Duration, GetRoundConfig()->AlibiRadius);
	GetWorldTimerManager().SetTimer(RoundTimer, this, &AObshagaGameMode::EndRound, Duration, false);
	GetWorldTimerManager().SetTimer(LiveStatusTimer, this, &AObshagaGameMode::UpdateLiveTaskStatus, 1.f, true);
	UE_LOG(LogObshaga, Log, TEXT("Round started: %.0f s, %d players"), Duration, State->PlayerArray.Num());
}

void AObshagaGameMode::UpdateLiveTaskStatus()
{
	const AObshagaGameState* State = GetGameState<AObshagaGameState>();
	for (APlayerState* Player : State->PlayerArray)
	{
		if (const AObshagaPlayerState* PlayerState = Cast<AObshagaPlayerState>(Player))
		{
			PlayerState->GetTaskComponent()->UpdateLiveStatus();
		}
	}
}

void AObshagaGameMode::EndRound()
{
	// Незаконченный допрос закрываем до подсчёта очков.
	ResolveInterrogation();

	AObshagaGameState* State = GetGameState<AObshagaGameState>();
	GetWorldTimerManager().ClearTimer(LiveStatusTimer);

	// Подводим итоги и раскрываем всем, у кого какое задание было.
	TArray<FRevealedTask> Reveal;
	for (APlayerState* Player : State->PlayerArray)
	{
		AObshagaPlayerState* PlayerState = Cast<AObshagaPlayerState>(Player);
		if (!PlayerState)
		{
			continue;
		}

		const int32 Earned = PlayerState->GetTaskComponent()->FinalizeTasks();
		PlayerState->SetScore(PlayerState->GetScore() + Earned);

		for (const FTaskState& Task : PlayerState->GetTaskComponent()->GetTasks())
		{
			FRevealedTask& Line = Reveal.AddDefaulted_GetRef();
			Line.PlayerName = PlayerState->GetPlayerName();
			Line.TaskTitle = Task.Title;
			Line.Status = Task.Status;
			Line.Reward = Task.Reward;
		}
	}

	State->FinishRound(Reveal);
	if (UGameEventSubsystem* Bus = UGameEventSubsystem::Get(this))
	{
		FGameEvent Event;
		Event.Type = EGameEventType::RoundEnded;
		Bus->Publish(Event);
	}
	UE_LOG(LogObshaga, Log, TEXT("Round ended"));
}

void AObshagaGameMode::NotifyPlayer(const APlayerState* PlayerState, const FText& Text) const
{
	if (AObshagaPlayerController* Controller = PlayerState ? Cast<AObshagaPlayerController>(PlayerState->GetOwner()) : nullptr)
	{
		Controller->ClientShowNotice(Text);
	}
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

	const float Duration = GetRoundConfig()->InterrogationSeconds;
	FInterrogationInfo Info;
	Info.bActive = true;
	Info.Suspect = PlayerState;
	Info.EndServerTime = static_cast<float>(State->GetServerWorldTimeSeconds()) + Duration;
	State->SetInterrogation(Info);

	GetWorldTimerManager().SetTimer(InterrogationTimer, this, &AObshagaGameMode::ResolveInterrogation, Duration, false);
	UGameEventSubsystem::PublishFrom(Suspect, EGameEventType::PlayerCaught);
	UE_LOG(LogObshaga, Verbose, TEXT("Interrogation started: %s (contraband=%d)"), *PlayerState->GetPlayerName(), bSuspectHadContraband ? 1 : 0);
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
	if (!Info.bActive || !Suspect || !By || By == Suspect || By->IsHiding() || AlibiBy.Contains(By)
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
}

#undef LOCTEXT_NAMESPACE
