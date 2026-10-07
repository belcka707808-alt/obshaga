#include "ObshagaGameMode.h"

#include "GameEventSubsystem.h"
#include "Obshaga.h"
#include "ObshagaCharacter.h"
#include "ObshagaGameState.h"
#include "ObshagaHUD.h"
#include "ObshagaPlayerController.h"
#include "ObshagaPlayerState.h"
#include "ObshagaRoundConfig.h"
#include "RoomVolume.h"
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

	if (AObshagaPlayerController* Controller = Cast<AObshagaPlayerController>(PlayerState->GetOwner()))
	{
		Controller->ClientShowNotice(LOCTEXT("NewTask", "Новое секретное задание — открой телефон [Tab]"));
	}
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
	State->StartRound(Duration);
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

#undef LOCTEXT_NAMESPACE
