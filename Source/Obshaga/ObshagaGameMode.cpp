#include "ObshagaGameMode.h"

#include "CarryComponent.h"
#include "DeviceActor.h"
#include "DoorActor.h"
#include "GameEventSubsystem.h"
#include "HidingSpot.h"
#include "InteractionComponent.h"
#include "ItemActor.h"
#include "KomendantAIController.h"
#include "KomendantCharacter.h"
#include "ObshagaStory.h"
#include "NightExitDoor.h"
#include "Obshaga.h"
#include "ObshagaCharacter.h"
#include "ObshagaGameState.h"
#include "ObshagaHUD.h"
#include "ObshagaItemData.h"
#include "ObshagaLookComponent.h"
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
#include "Engine/NetConnection.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "TimerManager.h"

#define LOCTEXT_NAMESPACE "ObshagaGameMode"

namespace
{
	// Соединение, молчащее дольше этого, считаем оборванным, если тот же игрок уже заходит снова.
	constexpr double StaleConnectionSeconds = 5.0;
}

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
#if !UE_BUILD_SHIPPING
	// Для проверок: -DebugMainTasks=start_rumor,expose_liar — основные задания по порядку игроков.
	FString DebugTasks;
	if (FParse::Value(FCommandLine::Get(), TEXT("DebugMainTasks="), DebugTasks, false))
	{
		TArray<FString> Ids;
		DebugTasks.ParseIntoArray(Ids, TEXT(","));
		TaskDirector->DebugMainTasks.Reset();
		for (const FString& Id : Ids)
		{
			TaskDirector->DebugMainTasks.Add(FName(*Id));
		}
	}
#endif

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

int32 AObshagaGameMode::GetKomendantStyle() const
{
	for (TActorIterator<AKomendantCharacter> It(GetWorld()); It; ++It)
	{
		if (const AKomendantAIController* Brain = Cast<AKomendantAIController>(It->GetController()))
		{
			return Brain->GetSpeechStyle();
		}
	}
	return 0;
}

void AObshagaGameMode::KomendantSay(EKomendantLine Line) const
{
	for (TActorIterator<AKomendantCharacter> It(GetWorld()); It; ++It)
	{
		It->Say(Line, GetKomendantStyle(), true);
	}
}

void AObshagaGameMode::NotifyAll(const FText& Text) const
{
	for (const AObshagaPlayerState* PlayerState : GetObshagaPlayers())
	{
		NotifyPlayer(PlayerState, Text);
	}
}

FString AObshagaGameMode::InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal)
{
	const FString Error = Super::InitNewPlayer(NewPlayerController, UniqueId, Options, Portal);

	// Ключ для повторного входа — номер игрока в Steam: он один и тот же при каждом запуске.
	if (AObshagaPlayerState* NewState = NewPlayerController ? NewPlayerController->GetPlayerState<AObshagaPlayerState>() : nullptr)
	{
		NewState->ReconnectKey = UniqueId.IsValid() ? UniqueId->ToString() : FString();
#if !UE_BUILD_SHIPPING
		// Для проверки двух копий на одном компьютере без Steam: там номер игрока при каждом запуске новый.
		const FString TestKey = UGameplayStatics::ParseOption(Options, TEXT("RKey"));
		if (!TestKey.IsEmpty())
		{
			NewState->ReconnectKey = TestKey;
		}
#endif
	}
	return Error;
}

void AObshagaGameMode::Logout(AController* Exiting)
{
	// Сначала обычный выход: движок освобождает место игрока в комнате, пока состояние ещё при нём.
	Super::Logout(Exiting);

	const AObshagaGameState* State = GetGameState<AObshagaGameState>();
	AObshagaPlayerState* Leaving = Exiting ? Exiting->GetPlayerState<AObshagaPlayerState>() : nullptr;
	if (!State || State->GetRoundState() != ERoundState::InProgress || !Leaving || Leaving->ReconnectKey.IsEmpty())
	{
		return;
	}

	// Посреди раунда состояние игрока не уничтожаем, а отцепляем от уходящего контроллера: роль, очки,
	// страйки и задания остаются на месте, остальные по-прежнему видят его в итогах. Вернётся — получит всё назад.
	Disconnected.Add(Leaving->ReconnectKey, Leaving);
	Exiting->PlayerState = nullptr;
	Leaving->SetOwner(nullptr);
	UE_LOG(LogObshaga, Log, TEXT("%s left mid-round; state kept for return"), *Leaving->GetPlayerName());
}

bool AObshagaGameMode::RestoreDisconnected(APlayerController* NewPlayer)
{
	AObshagaPlayerState* Fresh = NewPlayer ? NewPlayer->GetPlayerState<AObshagaPlayerState>() : nullptr;
	TObjectPtr<AObshagaPlayerState> Old = nullptr;
	if (!Fresh || Fresh->ReconnectKey.IsEmpty())
	{
		return false;
	}

	// Номер Steam игрок сообщает сам, а адрес соединения через Steam выдаёт сам Steam. Если они расходятся,
	// кто-то выдаёт себя за другого, чтобы забрать его роль и задания, — состояние не отдаём.
	const UNetConnection* Connection = NewPlayer->GetNetConnection();
	const FString Address = Connection && Connection->RemoteAddr.IsValid() ? Connection->RemoteAddr->ToString(false) : FString();
	if (Address.Contains(TEXT("7656119")) && !Address.Contains(Fresh->ReconnectKey))
	{
		UE_LOG(LogObshaga, Warning, TEXT("Reconnect key %s does not match connection address %s"), *Fresh->ReconnectKey, *Address);
		return false;
	}

	// Игра у гостя упала, а сервер ещё не заметил обрыва (на это уходит до минуты): тот же игрок уже
	// заходит снова, а старое соединение числится живым. Закрываем старое сами — его состояние уйдёт в Disconnected.
	for (AObshagaPlayerState* Other : GetObshagaPlayers())
	{
		APlayerController* Stale = Other != Fresh && Other->ReconnectKey == Fresh->ReconnectKey ? Cast<APlayerController>(Other->GetOwner()) : nullptr;
		// Живого игрока так выгнать нельзя: его соединение шлёт пакеты много раз в секунду.
		const UNetConnection* StaleConnection = Stale ? Stale->GetNetConnection() : nullptr;
		const bool bSilent = StaleConnection && FPlatformTime::Seconds() - StaleConnection->LastReceiveRealtime > StaleConnectionSeconds;
		if (Stale && Stale != NewPlayer && bSilent)
		{
			UE_LOG(LogObshaga, Log, TEXT("%s reconnects over a stale connection; closing the old one"), *Other->GetPlayerName());
			Stale->Destroy();
			break;
		}
	}

	if ( !Disconnected.RemoveAndCopyValue(Fresh->ReconnectKey, Old) || !IsValid(Old))
	{
		return false;
	}

	// Так же движок возвращает «неактивных» игроков: новое пустое состояние убираем, прежнее отдаём контроллеру.
	// Владельцем становится новый контроллер, поэтому секретные поля (COND_OwnerOnly) уйдут только ему.
	Old->SetUniqueId(Fresh->GetUniqueId());
	Fresh->Destroy();
	NewPlayer->PlayerState = Old;
	Old->SetOwner(NewPlayer);
	// Место появления движок выбрал ещё для пустого состояния; сбрасываем, чтобы игрок появился у себя в комнате.
	NewPlayer->StartSpot = nullptr;
	Old->ForceNetUpdate();
	UE_LOG(LogObshaga, Log, TEXT("%s is back; state restored"), *Old->GetPlayerName());
	return true;
}

void AObshagaGameMode::ForgetDisconnected()
{
	// Новый раунд — новые роли: кто не вернулся, в нём не участвует.
	for (const TPair<FString, TObjectPtr<AObshagaPlayerState>>& Pair : Disconnected)
	{
		if (IsValid(Pair.Value))
		{
			Pair.Value->Destroy();
		}
	}
	Disconnected.Reset();
}

void AObshagaGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	if (RestoreDisconnected(NewPlayer))
	{
		// Появляется у себя в комнате (ChoosePlayerStart), с прежними заданиями и очками.
		Super::HandleStartingNewPlayer_Implementation(NewPlayer);
		const AObshagaPlayerState* Returned = NewPlayer->GetPlayerState<AObshagaPlayerState>();
		NotifyPlayer(Returned, LOCTEXT("WelcomeBack", "Ты снова в игре: задания и очки на месте"));
		return;
	}

	// Внешность — до появления персонажа: свободная модель, а если все заняты — та, что встречается реже.
	// Номер игрока для этого не годится: после перезахода он новый, и модели начали бы совпадать.
	if (AObshagaPlayerState* NewState = NewPlayer ? NewPlayer->GetPlayerState<AObshagaPlayerState>() : nullptr)
	{
		const int32 NumLooks = FMath::Max(UObshagaLookConfig::Get()->ResidentMeshes.Num(), 1);
		TArray<int32> Used;
		Used.SetNumZeroed(NumLooks);
		for (const AObshagaPlayerState* Other : GetObshagaPlayers())
		{
			if (Other != NewState)
			{
				++Used[Other->GetLookSlot() % NumLooks];
			}
		}
		int32 Best = 0;
		for (int32 Slot = 1; Slot < NumLooks; ++Slot)
		{
			if (Used[Slot] < Used[Best])
			{
				Best = Slot;
			}
		}
		NewState->SetLookSlot(Best);
	}

	Super::HandleStartingNewPlayer_Implementation(NewPlayer);

	AObshagaPlayerState* PlayerState = NewPlayer ? NewPlayer->GetPlayerState<AObshagaPlayerState>() : nullptr;
	if (!PlayerState)
	{
		return;
	}

	// Пока нет лобби с настоящими именами (M7), игроки — «Жилец 1», «Жилец 2»...
	// У каждой модели жильца своё имя с прозвищем; ник Steam, если он есть, придёт следом и заменит его.
	FString ResidentName = ObshagaStory::ResidentName(PlayerState->GetLookSlot());
	for (const AObshagaPlayerState* Other : GetObshagaPlayers())
	{
		if (Other != PlayerState && Other->GetPlayerName() == ResidentName)
		{
			ResidentName += FString::Printf(TEXT(" %d"), NextPlayerNumber);
			break;
		}
	}
	++NextPlayerNumber;
	PlayerState->SetPlayerName(ResidentName);
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
	const FTaskRow* AccuseRow = Tasks->FindRowByCondition(ETaskCondition::CorrectAccusation);
	PlayerState->SetTaskAbilities(AccuseRow != nullptr, Tasks->FindRowByCondition(ETaskCondition::InspectionFoundContrabandInRoom) != nullptr);
	PlayerState->SetAccuseRumor(AccuseRow && UTaskComponent::IsRumorRow(this, *AccuseRow));
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

	// Вернувшийся после вылета появляется у себя в комнате, а не подселяется в новую.
	const AObshagaPlayerState* Returning = Player ? Player->GetPlayerState<AObshagaPlayerState>() : nullptr;
	if (Returning && !Returning->GetHomeRoomId().IsNone())
	{
		for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
		{
			const ARoomVolume* Room = ARoomVolume::FindRoomAt(this, It->GetActorLocation());
			if (!It->IsA<APlayerStartPIE>() && Room && Room->RoomId == Returning->GetHomeRoomId())
			{
				return *It;
			}
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
		AObshagaGameState* MutableState = GetGameState<AObshagaGameState>();
		if (MutableState->IsIntroActive())
		{
			// Хост нажал ещё раз во время заставки — пропускаем её для всех.
			GetWorldTimerManager().ClearTimer(IntroTimer);
			MutableState->SetIntro(0.f);
			StartRound();
		}
		else if (!bIntroShown && GetRoundConfig()->IntroSeconds > 0.f)
		{
			// Первый раунд комнаты начинается с заставки; раунд стартует, когда она кончится.
			bIntroShown = true;
			PrepareNight();
			MutableState->SetIntro(GetRoundConfig()->IntroSeconds);
			UE_LOG(LogObshaga, Log, TEXT("Intro started: %.0f s"), GetRoundConfig()->IntroSeconds);
			GetWorldTimerManager().SetTimer(IntroTimer, this, &AObshagaGameMode::StartRound, GetRoundConfig()->IntroSeconds, false);
		}
		else
		{
			StartRound();
		}
	}
	else if (State->GetRoundState() == ERoundState::Finished)
	{
		AObshagaGameState* MutableState = GetGameState<AObshagaGameState>();
		if (MutableState->IsIntroActive())
		{
			// Хост пропускает заставку реванша для всех.
			GetWorldTimerManager().ClearTimer(IntroTimer);
			MutableState->SetIntro(0.f);
			StartRound();
		}
		else if (GetRoundConfig()->RematchIntroSeconds > 0.f)
		{
			// Реванш — следующая ночь недели: короткая заставка с её карточкой.
			PrepareNight();
			MutableState->SetIntro(GetRoundConfig()->RematchIntroSeconds);
			GetWorldTimerManager().SetTimer(IntroTimer, this, &AObshagaGameMode::StartRound, GetRoundConfig()->RematchIntroSeconds, false);
		}
		else
		{
			StartRound();
		}
	}
}

void AObshagaGameMode::PrepareNight()
{
	AObshagaGameState* State = GetGameState<AObshagaGameState>();
	if (bNightPrepared || !State)
	{
		return;
	}
	bNightPrepared = true;

	bool bNewWeek = false;
	if (WeekNight < 0)
	{
		WeekNight = 0;
		bNewWeek = true;
#if !UE_BUILD_SHIPPING
		// Для проверок: -WeekNight=N — первая ночь комнаты N-я (0 — понедельник … 4 — пятница).
		int32 Forced = 0;
		if (FParse::Value(FCommandLine::Get(), TEXT("WeekNight="), Forced))
		{
			WeekNight = FMath::Clamp(Forced, 0, ObshagaStory::NumWeekNights - 1);
		}
#endif
	}
	else if (++WeekNight >= ObshagaStory::NumWeekNights)
	{
		WeekNight = 0;
		bNewWeek = true;
	}
	if (bNewWeek)
	{
		++WeekId;
		for (AObshagaPlayerState* PlayerState : GetObshagaPlayers())
		{
			PlayerState->StartWeek();
		}
	}
	// «Вчера в газете» — со вторника: заголовок прошлой ночи ещё лежит в GameState.
	State->SetWeek(WeekNight, WeekId, WeekNight > 0 ? State->GetHeadline() : FText::GetEmpty());
	UE_LOG(LogObshaga, Log, TEXT("Week %d, night %d (%s)"), WeekId, WeekNight, *ObshagaStory::WeekNight(WeekNight).Title.ToString());
}

int32 AObshagaGameMode::GetWeekKomendantStyle() const
{
	return WeekNight >= 0 ? ObshagaStory::WeekNight(WeekNight).KomendantStyle : -1;
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

	ForgetDisconnected();
	PrepareNight();

	// Каждый раунд, и первый тоже, начинается с чистого мира: то, что натворили в лобби, не считается.
	ResetWorldForRematch();

	const UObshagaRoundConfig* Config = GetRoundConfig();
	const TArray<AObshagaPlayerState*> Players = GetObshagaPlayers();

	CaughtLastRound = CaughtThisRound;
	CaughtThisRound.Reset();
	// В пятницу Палыч помнит всю неделю: с самого начала он присматривает за тем, кто попадался чаще всех.
	if (WeekNight == ObshagaStory::NumWeekNights - 1)
	{
		const AObshagaPlayerState* MostCaught = nullptr;
		for (const AObshagaPlayerState* PlayerState : Players)
		{
			if (PlayerState->GetWeekStrikes() > 0 && (!MostCaught || PlayerState->GetWeekStrikes() > MostCaught->GetWeekStrikes()))
			{
				MostCaught = PlayerState;
			}
		}
		if (MostCaught)
		{
			CaughtLastRound = { const_cast<AObshagaPlayerState*>(MostCaught) };
			UE_LOG(LogObshaga, Log, TEXT("Friday grudge: %s (%d strikes this week)"), *MostCaught->GetPlayerName(), MostCaught->GetWeekStrikes());
		}
	}
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
	GetWorldTimerManager().ClearTimer(NightEventTimer);
	GetWorldTimerManager().ClearTimer(NightEventEndTimer);
	GetWorldTimerManager().ClearTimer(FireAlarmTimer);
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
	State->SetFlashlight(Config->FlashlightIntensity, Config->FlashlightRadius, Config->FlashlightConeDegrees);
	BeginPhase(ERoundPhase::Evening);

	GetWorldTimerManager().SetTimer(LiveStatusTimer, this, &AObshagaGameMode::UpdateLiveTaskStatus, 1.f, true);
	if (Config->SmsCount > 0)
	{
		// Первое СМС — через полинтервала: так они приходят в середине фаз и не спорят с объявлением фазы.
		const float Interval = Config->GetTotalSeconds() / Config->SmsCount;
		GetWorldTimerManager().SetTimer(SmsTimer, this, &AObshagaGameMode::SendSms, Interval, true, Interval * 0.5f);
	}
	// Завязка вечера подбирается под характер коменданта этого раунда (его к этому моменту уже выбрали).
	State->SetStory(GetKomendantStyle() * ObshagaStory::StoriesPerStyle + FMath::RandRange(0, ObshagaStory::StoriesPerStyle - 1));

	// Новичкам сразу говорим, откуда ждать опасность.
	NotifyAll(LOCTEXT("KomendantWhere", "Комендант на вахте, 1 этаж. Скоро пойдёт в обход — не попадайся"));
	UE_LOG(LogObshaga, Log, TEXT("Round started: %.0f s, %d players"), Config->GetTotalSeconds(), Players.Num());
	// Следующий раунд выберет следующую ночь.
	bNightPrepared = false;
}

void AObshagaGameMode::StartNightEvent()
{
	AObshagaGameState* State = GetGameState<AObshagaGameState>();
	const UObshagaRoundConfig* Config = GetRoundConfig();
	if (!State || State->GetRoundState() != ERoundState::InProgress || State->GetPhase() != ERoundPhase::Night)
	{
		return;
	}

	ENightEvent Event = static_cast<ENightEvent>(FMath::RandRange(1, static_cast<int32>(ENightEvent::PipeBurst)));
	// Для проверок: -TestNightEvent=N включает событие с номером N (1 — пробки, 2 — тревога, 3 — деканат, 4 — день рождения, 5 — труба).
	int32 Forced = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("TestNightEvent="), Forced) && Forced > 0)
	{
		Event = static_cast<ENightEvent>(FMath::Clamp(Forced, 1, static_cast<int32>(ENightEvent::PipeBurst)));
	}
	// Ночь недели может задать своё событие или обойтись без него (понедельник).
	const int32 Planned = WeekNight >= 0 ? ObshagaStory::WeekNight(WeekNight).NightEvent : -1;
	if (Forced <= 0 && Planned == 0)
	{
		UE_LOG(LogObshaga, Log, TEXT("Night event: none tonight (week night %d)"), WeekNight);
		return;
	}
	if (Forced <= 0 && Planned > 0)
	{
		Event = static_cast<ENightEvent>(FMath::Clamp(Planned, 1, static_cast<int32>(ENightEvent::PipeBurst)));
	}

	const ARoomVolume* BirthdayRoom = ARoomVolume::FindRoomById(this, Config->BirthdayRoomId);
	const ARoomVolume* Bathroom = nullptr;
	for (TActorIterator<ARoomVolume> It(GetWorld()); It && !Bathroom; ++It)
	{
		Bathroom = It->RoomType == ERoomType::Bathroom ? *It : nullptr;
	}
	// Событию нужна комната, которой на карте нет, — берём то, что комнат не требует.
	if ((Event == ENightEvent::Birthday && !BirthdayRoom) || (Event == ENightEvent::PipeBurst && !Bathroom))
	{
		Event = ENightEvent::Blackout;
	}

	float Seconds = 0.f;
	float SkyScale = 1.f;
	FText Banner;
	FName EventName;
	switch (Event)
	{
	case ENightEvent::FireAlarm:
		Seconds = Config->FireAlarmSeconds;
		EventName = TEXT("FireAlarm");
		Banner = LOCTEXT("EventFireAlarm", "Пожарная тревога! Все в коридор — кто останется в комнате, тот под подозрением");
		break;
	case ENightEvent::Deanery:
		// Действует до конца раунда: предупреждение ночью, сама проверка утром.
		Seconds = Config->GetTotalSeconds();
		EventName = TEXT("Deanery");
		Banner = LOCTEXT("EventDeanery", "Утром проверка из деканата! Комендант будет обыскивать комнаты быстрее обычного");
		break;
	case ENightEvent::Birthday:
		Seconds = Config->BirthdaySeconds;
		EventName = TEXT("Birthday");
		Banner = FText::Format(LOCTEXT("EventBirthday", "День рождения! {0}: на шум оттуда комендант не идёт. Но заглянет поздравить"), BirthdayRoom->DisplayName);
		break;
	case ENightEvent::PipeBurst:
		Seconds = Config->PipeSeconds;
		EventName = TEXT("PipeBurst");
		Banner = LOCTEXT("EventPipe", "В санузле прорвало трубу! Комендант ушёл чинить — в коридорах пока свободно");
		break;
	default:
		Event = ENightEvent::Blackout;
		Seconds = Config->BlackoutSeconds;
		SkyScale = Config->BlackoutSkyScale;
		EventName = TEXT("Blackout");
		Banner = LOCTEXT("EventBlackout", "Выбило пробки! В общаге темно — комендант видит только перед собой");
		break;
	}

	// Ни одно событие, кроме проверки из деканата, не переживает ночь.
	if (Event != ENightEvent::Deanery)
	{
		Seconds = FMath::Min(Seconds, State->GetPhaseRemainingSeconds());
	}
	if (Seconds < 1.f)
	{
		return;
	}

	State->StartNightEvent(Event, Seconds, SkyScale);
	NotifyAll(Banner);
	for (AObshagaPlayerState* PlayerState : GetObshagaPlayers())
	{
		PlayerState->AddSms(Banner);
	}
	if (UGameEventSubsystem* Bus = UGameEventSubsystem::Get(this))
	{
		// В хронику и телеметрию: какое событие, пишем в поле комнаты.
		FGameEvent BusEvent;
		BusEvent.Type = EGameEventType::NightEvent;
		BusEvent.RoomId = EventName;
		Bus->Publish(BusEvent);
	}
	UE_LOG(LogObshaga, Log, TEXT("Night event: %s for %.0f s"), *EventName.ToString(), Seconds);

	for (TActorIterator<AKomendantCharacter> It(GetWorld()); It; ++It)
	{
		AKomendantAIController* Brain = Cast<AKomendantAIController>(It->GetController());
		if (!Brain)
		{
			continue;
		}
		if (Event == ENightEvent::Blackout)
		{
			Brain->RefreshSenses();
		}
		else if (Event == ENightEvent::Birthday)
		{
			Brain->InvestigateTip(BirthdayRoom->GetActorLocation());
		}
		else if (Event == ENightEvent::PipeBurst)
		{
			Brain->HoldAt(Bathroom->GetActorLocation(), Seconds);
		}
	}

	if (Event == ENightEvent::FireAlarm)
	{
		FireAlarmStartTime = GetWorld()->GetTimeSeconds();
		GetWorldTimerManager().SetTimer(FireAlarmTimer, this, &AObshagaGameMode::TickFireAlarm, 1.f, true);
	}
	if (Event != ENightEvent::Deanery)
	{
		GetWorldTimerManager().SetTimer(NightEventEndTimer, this, &AObshagaGameMode::EndNightEvent, Seconds, false);
	}
}

void AObshagaGameMode::EndNightEvent()
{
	GetWorldTimerManager().ClearTimer(FireAlarmTimer);

	// Сначала событие кончается в состоянии игры, потом комендант пересчитывает зрение — иначе таймер,
	// сработавший на кадр раньше часов, оставил бы ему узкий конус «пробок» до утра.
	AObshagaGameState* State = GetGameState<AObshagaGameState>();
	if (!State)
	{
		return;
	}
	const ENightEvent Event = State->GetNightEvent();
	State->EndNightEvent();
	for (TActorIterator<AKomendantCharacter> It(GetWorld()); It; ++It)
	{
		if (AKomendantAIController* Brain = Cast<AKomendantAIController>(It->GetController()))
		{
			Brain->RefreshSenses();
		}
	}

	if (State->GetRoundState() == ERoundState::InProgress)
	{
		switch (Event)
		{
		case ENightEvent::Blackout:
			NotifyAll(LOCTEXT("EventBlackoutEnd", "Свет дали"));
			break;
		case ENightEvent::FireAlarm:
			NotifyAll(LOCTEXT("EventFireAlarmEnd", "Отбой тревоги. По комнатам!"));
			break;
		case ENightEvent::Birthday:
			NotifyAll(LOCTEXT("EventBirthdayEnd", "Праздник кончился. Теперь шуметь нельзя нигде"));
			break;
		case ENightEvent::PipeBurst:
			NotifyAll(LOCTEXT("EventPipeEnd", "Трубу починили. Комендант снова в обходе"));
			break;
		default:
			break;
		}
	}
}
void AObshagaGameMode::TickFireAlarm()
{
	// Первые секунды — время выйти из комнаты; потом каждый, кто сидит в жилой комнате, копит подозрение.
	const UObshagaRoundConfig* Config = GetRoundConfig();
	if (GetWorld()->GetTimeSeconds() - FireAlarmStartTime < Config->FireAlarmGraceSeconds)
	{
		return;
	}
	for (AObshagaPlayerState* PlayerState : GetObshagaPlayers())
	{
		const AObshagaCharacter* Character = Cast<AObshagaCharacter>(PlayerState->GetPawn());
		const ARoomVolume* Room = Character && !Character->IsGhost() ? ARoomVolume::FindRoomAt(this, Character->GetActorLocation()) : nullptr;
		if (Room && Room->RoomType == ERoomType::Bedroom)
		{
			PlayerState->GetSuspicionComponent()->AddSuspicion(Config->FireAlarmSuspicionPerSecond);
			UE_LOG(LogObshaga, Verbose, TEXT("Fire alarm: %s stays in %s, suspicion %.0f"), *PlayerState->GetPlayerName(), *Room->RoomId.ToString(), PlayerState->GetSuspicionComponent()->GetSuspicion());
		}
	}
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
		GetWorldTimerManager().SetTimer(NightEventTimer, this, &AObshagaGameMode::StartNightEvent, Config->NightEventDelaySeconds, false);
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

	// Для проверок: -TestPhaseSeconds=20 у хоста укорачивает каждую фазу, не трогая настройки раунда.
	float TestSeconds = 0.f;
	if (FParse::Value(FCommandLine::Get(), TEXT("TestPhaseSeconds="), TestSeconds) && TestSeconds > 0.f)
	{
		Duration = TestSeconds;
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
		UE_LOG(LogObshaga, Verbose, TEXT("Accuse by %s rejected: round, accuser or suspect state (can accuse: %d)"), Accuser ? *Accuser->GetPlayerName() : TEXT("-"), Accuser ? Accuser->CanAccuse() : 0);
		return;
	}

	// Клиенту не верим: показать можно только на того, кто рядом и на виду.
	const float MaxDistance = Config->AccuseDistance + UInteractionComponent::ServerRangeSlack;
	const FTaskRow* Row = Accuser->GetTaskComponent()->FindRowByCondition(ETaskCondition::CorrectAccusation);
	if (!Row || FVector::Dist(AccuserCharacter->GetActorLocation(), Suspect->GetActorLocation()) > MaxDistance
		|| !AccuserCharacter->GetInteractionComponent()->HasLineOfSight(Suspect))
	{
		UE_LOG(LogObshaga, Verbose, TEXT("Accuse by %s rejected: %s"), *Accuser->GetPlayerName(),
			!Row ? TEXT("no accusation task") : (FVector::Dist(AccuserCharacter->GetActorLocation(), Suspect->GetActorLocation()) > MaxDistance ? TEXT("too far") : TEXT("no line of sight")));
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
		UE_LOG(LogObshaga, Verbose, TEXT("Accuse by %s postponed: komendant is busy"), *Accuser->GetPlayerName());
		NotifyPlayer(Accuser, LOCTEXT("AccuseBusy", "Комендант занят — скажи позже"));
		return;
	}

	// Обвинить можно один раз за раунд. Верно ли обвинение, решается сейчас:
	// украденный предмет (или ничего) записывается в событие, и задание читает его оттуда.
	Accuser->SetTaskAbilities(false, Accuser->CanTipRoom());
	// У задания «найди автора слуха» доказательство — записка, которую подкинул обвиняемый.
	AItemActor* StolenItem = UTaskComponent::IsRumorRow(this, *Row) ? UTaskComponent::FindRumorNote(this, SuspectState) : UTaskComponent::FindStolenItem(this, SuspectState, *Row);
	UE_LOG(LogObshaga, Verbose, TEXT("Accuse by %s of %s: %s (rumor task: %d, proof: %s)"), *Accuser->GetPlayerName(), *SuspectState->GetPlayerName(),
		StolenItem ? TEXT("right") : TEXT("wrong"), UTaskComponent::IsRumorRow(this, *Row), *GetNameSafe(StolenItem));
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
			// Target заполнен: отобрали из рук, а не нашли в тайнике (хроника пишет об этом по-разному).
			UGameEventSubsystem::PublishFrom(Suspect, EGameEventType::ContrabandConfiscated, Item, Suspect);
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
	KomendantSay(EKomendantLine::Caught);

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

		// Комендант отвечает на то, что услышал.
		EKomendantLine Reply = EKomendantLine::Silent;
		if (EventType == EGameEventType::InterrogationConfessed)
		{
			Reply = EKomendantLine::Confess;
		}
		else if (EventType == EGameEventType::InterrogationLieSucceeded)
		{
			Reply = EKomendantLine::LieOk;
		}
		else if (EventType == EGameEventType::InterrogationLieFailed)
		{
			Reply = EKomendantLine::LieFail;
		}
		KomendantSay(Reply);

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
	GetWorldTimerManager().ClearTimer(NightEventTimer);
	GetWorldTimerManager().ClearTimer(NightEventEndTimer);
	GetWorldTimerManager().ClearTimer(FireAlarmTimer);

	// Подводим итоги и раскрываем всем, кто кем был и что делал.
	const TArray<AObshagaPlayerState*> Players = GetObshagaPlayers();
	TArray<FRevealedTask> RevealedTasks;
	TArray<FRevealedPlayer> RevealedPlayers;
	for (AObshagaPlayerState* PlayerState : Players)
	{
		const int32 Earned = PlayerState->GetTaskComponent()->FinalizeTasks() + PlayerState->TakePendingRatBonus();
		PlayerState->SetScore(PlayerState->GetScore() + Earned);
		PlayerState->AddNightToWeek(PlayerState->GetSuspicionComponent()->GetStrikes());
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
		Revealed.WeekScore = PlayerState->GetWeekScore();
		Revealed.WeekStrikes = PlayerState->GetWeekStrikes();
	}
	AssignTitles(RevealedPlayers, Players);
	WriteTelemetry(Players);

	BuildNewspaper(Players);
	if (WeekNight == ObshagaStory::NumWeekNights - 1)
	{
		State->SetWeekTitles(BuildWeekTitles(Players));
	}

	State->FinishRound(RevealedTasks, RevealedPlayers, BuildChronicle());
	if (UGameEventSubsystem* Bus = UGameEventSubsystem::Get(this))
	{
		FGameEvent Event;
		Event.Type = EGameEventType::RoundEnded;
		Bus->Publish(Event);
	}
	UE_LOG(LogObshaga, Log, TEXT("Round ended"));
}

void AObshagaGameMode::BuildNewspaper(const TArray<AObshagaPlayerState*>& Players) const
{
	// Стенгазета: заголовок — по самому громкому событию раунда, под ним герой ночи и пара коротких заметок.
	AObshagaGameState* State = GetGameState<AObshagaGameState>();
	const UGameEventSubsystem* Bus = UGameEventSubsystem::Get(this);
	if (!State || !Bus)
	{
		return;
	}

	auto NameOf = [](const APlayerState* Who) { return Who ? FText::FromString(Who->GetPlayerName()).ToUpper() : LOCTEXT("PaperSomeone", "КТО-ТО"); };
	auto First = [Bus](EGameEventType Type) -> const FGameEvent*
	{
		return Bus->GetEventLog().FindByPredicate([Type](const FGameEvent& Event) { return Event.Type == Type; });
	};
	auto CountOf = [Bus](EGameEventType Type)
	{
		int32 Result = 0;
		for (const FGameEvent& Event : Bus->GetEventLog())
		{
			Result += Event.Type == Type ? 1 : 0;
		}
		return Result;
	};

	FString WinnerName;
	const AObshagaPlayerState* Winner = nullptr;
	int32 WinnerScore = MIN_int32;
	int32 TotalStrikes = 0;
	const AObshagaPlayerState* MostCaught = nullptr;
	for (const AObshagaPlayerState* PlayerState : Players)
	{
		const int32 Strikes = PlayerState->GetSuspicionComponent()->GetStrikes();
		TotalStrikes += Strikes;
		if (Strikes > 0 && (!MostCaught || Strikes > MostCaught->GetSuspicionComponent()->GetStrikes()))
		{
			MostCaught = PlayerState;
		}
		if (FMath::RoundToInt32(PlayerState->GetScore()) > WinnerScore)
		{
			WinnerScore = FMath::RoundToInt32(PlayerState->GetScore());
			WinnerName = PlayerState->GetPlayerName();
			Winner = PlayerState;
		}
	}

	// У каждого повода несколько заголовков — берётся случайный, чтобы газета не повторялась из раунда в раунд.
	auto PickHead = [](std::initializer_list<FText> Options) { return *(Options.begin() + FMath::RandRange(0, static_cast<int32>(Options.size()) - 1)); };

	const FGameEvent* Night = First(EGameEventType::NightEvent);
	bool bNightInHeadline = false;
	FText Headline;
	// Герой номера — тот, про кого заголовок; если он ни про кого, в газету попадает победитель.
	const APlayerState* Hero = nullptr;
	FText HeroNote;
	// Русские окончания после числа: 1 раз, 2 раза, 5 раз; 1 очко, 2 очка, 5 очков.
	auto Plural = [](int32 Number, const FText& One, const FText& Few, const FText& Many)
	{
		const int32 Tens = FMath::Abs(Number) % 100;
		const int32 Units = Tens % 10;
		return (Tens >= 11 && Tens <= 14) ? Many : (Units == 1 ? One : ((Units >= 2 && Units <= 4) ? Few : Many));
	};
	auto CaughtNote = [&Plural](const AObshagaPlayerState* Who)
	{
		const int32 Strikes = Who ? Who->GetSuspicionComponent()->GetStrikes() : 0;
		return FText::Format(LOCTEXT("HeroCaught", "пойман {0} {1}"), FText::AsNumber(Strikes),
			Plural(Strikes, LOCTEXT("TimesOne", "раз"), LOCTEXT("TimesFew", "раза"), LOCTEXT("TimesMany", "раз")));
	};
	if (const FGameEvent* Evicted = First(EGameEventType::PlayerEvicted))
	{
		Headline = FText::Format(PickHead({
			LOCTEXT("HeadEvicted", "{0}: ВЫСЕЛЕНИЕ! ЧЕМОДАН, ВОКЗАЛ, РОДИТЕЛИ"),
			LOCTEXT("HeadEvicted2", "ТРИ СТРАЙКА — И {0} БОЛЬШЕ ЗДЕСЬ НЕ ЖИВЁТ"),
			LOCTEXT("HeadEvicted3", "ПРОЩАЙ, ОБЩАГА: {0} СДАЁТ КЛЮЧИ И ПОСТЕЛЬНОЕ") }), NameOf(Evicted->Instigator));
		Hero = Evicted->Instigator;
		HeroNote = LOCTEXT("HeroEvicted", "выселен");
	}
	else if (const FGameEvent* Framed = First(EGameEventType::PlayerFramed))
	{
		Headline = FText::Format(PickHead({
			LOCTEXT("HeadFramed", "ПОДСТАВА ВЕКА: {0} ПОДКИНУЛ, {1} ОТВЕТИЛ"),
			LOCTEXT("HeadFramed2", "{1} НЕ ЗНАЕТ, ОТКУДА ЭТО У НЕГО. ЗАТО ЗНАЕТ {0}"),
			LOCTEXT("HeadFramed3", "ДРУЖБА ДРУЖБОЙ, А {0} ПОДСТАВИЛ СОСЕДА ({1})") }), NameOf(Framed->Instigator), NameOf(Framed->Target));
		Hero = Framed->Instigator;
		HeroNote = LOCTEXT("HeroFramed", "подставил соседа");
	}
	else if (const FGameEvent* Confiscated = First(EGameEventType::ContrabandConfiscated))
	{
		Headline = FText::Format(PickHead({
			LOCTEXT("HeadContraband", "ЗАПРЕЩЁНКА НАЙДЕНА! {0} УВЕРЯЕТ, ЧТО ЭТО НЕ ЕГО"),
			LOCTEXT("HeadContraband2", "ОБЫСК УДАЛСЯ: У {0} ИЗЪЯЛИ ТО, ЧЕГО В ОБЩАГЕ БЫТЬ НЕ ДОЛЖНО"),
			LOCTEXT("HeadContraband3", "{0}: «ЭТО ДЛЯ ЛАБОРАТОРНОЙ». ПАЛЫЧ НЕ ПОВЕРИЛ") }), NameOf(Confiscated->Instigator));
		Hero = Confiscated->Instigator;
		HeroNote = LOCTEXT("HeroContraband", "попался с запрещёнкой");
	}
	else if (const FGameEvent* Tip = First(EGameEventType::TipSucceeded))
	{
		Headline = FText::Format(PickHead({
			LOCTEXT("HeadTip", "СТУК СРАБОТАЛ: {0} ПОСТУЧАЛ, {1} ПОПАЛСЯ"),
			LOCTEXT("HeadTip2", "В ОБЩАГЕ ЗАВЕЛАСЬ КРЫСА. {1} УЖЕ В КУРСЕ"),
			LOCTEXT("HeadTip3", "{0} ШЕПНУЛ ПАЛЫЧУ ПАРУ СЛОВ. {1} ПИШЕТ ОБЪЯСНИТЕЛЬНУЮ") }), NameOf(Tip->Instigator), NameOf(Tip->Target));
		Hero = Tip->Target;
		HeroNote = LOCTEXT("HeroTipped", "на него настучали");
	}
	else if (const FGameEvent* Lie = First(EGameEventType::InterrogationLieSucceeded))
	{
		Headline = FText::Format(PickHead({
			LOCTEXT("HeadLie", "{0} СОВРАЛ ПАЛЫЧУ В ГЛАЗА — И ТОТ ПОВЕРИЛ"),
			LOCTEXT("HeadLie2", "ОСКАР ЗА ЛУЧШУЮ РОЛЬ: {0} ВЫКРУТИЛСЯ НА ДОПРОСЕ"),
			LOCTEXT("HeadLie3", "«Я ПРОСТО ШЁЛ ЗА ВОДОЙ»: {0} УХОДИТ ОТ ПАЛЫЧА ЧИСТЫМ") }), NameOf(Lie->Instigator));
		Hero = Lie->Instigator;
		HeroNote = LOCTEXT("HeroLied", "соврал и ушёл чистым");
	}
	else if (TotalStrikes >= 2)
	{
		Headline = FText::Format(PickHead({
			LOCTEXT("HeadRaid", "ОБЛАВА В ОБЩАГЕ №7: ЗА НОЧЬ ПАЛЫЧ ЛОВИЛ {0} РАЗА"),
			LOCTEXT("HeadRaid2", "ПАЛЫЧ В УДАРЕ: {0} ПОИМКИ ЗА ОДНУ НОЧЬ"),
			LOCTEXT("HeadRaid3", "НОЧЬ ДЛИННЫХ ОБЪЯСНИТЕЛЬНЫХ: ПОЙМАНО {0}") }), FText::AsNumber(TotalStrikes));
		Hero = MostCaught;
		HeroNote = CaughtNote(MostCaught);
	}
	else if (MostCaught)
	{
		Headline = FText::Format(PickHead({
			LOCTEXT("HeadCaught", "{0}: ПОЙМАН. ОСТАЛЬНЫЕ ДЕЛАЮТ ВИД, ЧТО НЕ ЗНАКОМЫ"),
			LOCTEXT("HeadCaught2", "{0} ПОПАЛСЯ ПАЛЫЧУ. ОДИН ЗА ВСЕХ"),
			LOCTEXT("HeadCaught3", "КОРИДОР, НОЧЬ, ПАЛЫЧ: {0} НЕ УСПЕЛ СВЕРНУТЬ") }), NameOf(MostCaught));
		Hero = MostCaught;
		HeroNote = CaughtNote(MostCaught);
	}
	else if (Night)
	{
		bNightInHeadline = true;
		if (Night->RoomId == TEXT("FireAlarm"))
		{
			Headline = LOCTEXT("HeadFire", "ЛОЖНАЯ ТРЕВОГА: ОБЩАГА ПОСТОЯЛА В КОРИДОРЕ И РАЗОШЛАСЬ");
		}
		else if (Night->RoomId == TEXT("Deanery"))
		{
			Headline = LOCTEXT("HeadDeanery", "ДЕКАНАТ ПРИЕХАЛ, НИЧЕГО НЕ НАШЁЛ И УЕХАЛ ДОВОЛЬНЫЙ");
		}
		else if (Night->RoomId == TEXT("Birthday"))
		{
			Headline = LOCTEXT("HeadBirthday", "ДЕНЬ РОЖДЕНИЯ УДАЛСЯ: ПАЛЫЧ ЗАШЁЛ, НО ТОРТА ЕМУ НЕ ДОСТАЛОСЬ");
		}
		else if (Night->RoomId == TEXT("PipeBurst"))
		{
			Headline = LOCTEXT("HeadPipe", "ПОТОП В САНУЗЛЕ: ПАЛЫЧ ПРОТИВ ТРУБЫ, СЧЁТ НЕИЗВЕСТЕН");
		}
		else
		{
			Headline = LOCTEXT("HeadBlackout", "ТЁМНАЯ НОЧЬ: СВЕТА НЕ БЫЛО, СВИДЕТЕЛЕЙ ТОЖЕ");
		}
	}
	else if (First(EGameEventType::DeviceBroken))
	{
		Headline = PickHead({
			LOCTEXT("HeadBroken", "НА КУХНЕ ЧТО-ТО СЛОМАЛОСЬ. ВИНОВАТЫХ, КАК ВСЕГДА, НЕТ"),
			LOCTEXT("HeadBroken2", "ПЛИТА ПАЛА СМЕРТЬЮ ХРАБРЫХ. ОБЩАГА ПЕРЕХОДИТ НА ДОШИРАК"),
			LOCTEXT("HeadBroken3", "КУХОННЫЙ БУНТ: ТЕХНИКА ОТКАЗАЛАСЬ РАБОТАТЬ") });
	}
	else
	{
		Headline = PickHead({
			LOCTEXT("HeadQuiet", "ТИХАЯ НОЧЬ. ПАЛЫЧ ЧТО-ТО ПОДОЗРЕВАЕТ, НО ДОКАЗАТЬ НЕ МОЖЕТ"),
			LOCTEXT("HeadQuiet2", "НИЧЕГО НЕ ПРОИЗОШЛО. ИМЕННО ЭТО И ПОДОЗРИТЕЛЬНО"),
			LOCTEXT("HeadQuiet3", "ОБЩАГА СПАЛА. ИЛИ ОЧЕНЬ ХОРОШО ПРИТВОРЯЛАСЬ") });
	}

	// Заметки: что ещё было за ночь. Не больше трёх.
	TArray<FText> Notes;
	if (Night && !bNightInHeadline)
	{
		if (Night->RoomId == TEXT("FireAlarm"))
		{
			Notes.Add(LOCTEXT("NoteFire", "Ночью выла пожарная сигнализация. Пожара не нашли, виноватого тоже."));
		}
		else if (Night->RoomId == TEXT("Deanery"))
		{
			Notes.Add(LOCTEXT("NoteDeanery", "Утром общагу проверял деканат. Палыч нервничал больше жильцов."));
		}
		else if (Night->RoomId == TEXT("Birthday"))
		{
			Notes.Add(LOCTEXT("NoteBirthday", "В одной из комнат отмечали день рождения. Громко. Палыч терпел."));
		}
		else if (Night->RoomId == TEXT("PipeBurst"))
		{
			Notes.Add(LOCTEXT("NotePipe", "В санузле прорвало трубу. Палыч чинил её лично и, говорят, победил."));
		}
		else
		{
			Notes.Add(LOCTEXT("NoteBlackout", "Ночью выбило пробки. Кто и что делал в темноте — редакции неизвестно."));
		}
	}
	const int32 Doors = CountOf(EGameEventType::DoorOpened);
	if (Doors >= 6)
	{
		Notes.Add(FText::Format(LOCTEXT("NoteDoors", "Двери за ночь открывали {0} раз. Петли просят масла, жильцы — тишины."), FText::AsNumber(Doors)));
	}
	const int32 Hides = CountOf(EGameEventType::PlayerHid);
	if (Hides >= 2 && Notes.Num() < 3)
	{
		Notes.Add(FText::Format(LOCTEXT("NoteHides", "В шкафах и кабинках прятались {0} раз. Мебель общаги выдерживает и не такое."), FText::AsNumber(Hides)));
	}
	if (Notes.Num() < 3)
	{
		static const FText Moods[] = {
			LOCTEXT("MoodStrict", "Палыч сегодня был строг и собран. Говорят, это из-за проверки."),
			LOCTEXT("MoodAbsent", "Палыч сегодня был рассеян и дважды искал собственные очки."),
			LOCTEXT("MoodVindictive", "Палыч сегодня был злопамятен. Тетрадка с фамилиями пополнилась.") };
		Notes.Add(Moods[FMath::Clamp(GetKomendantStyle(), 0, 2)]);
	}

	if (!Hero)
	{
		// Заголовок ни про кого — на фото победитель, и подпись про его очки.
		Hero = Winner;
		HeroNote = FText::Format(LOCTEXT("HeroWinner", "{0} {1}"), FText::AsNumber(WinnerScore),
			Plural(WinnerScore, LOCTEXT("PointsOne", "очко"), LOCTEXT("PointsFew", "очка"), LOCTEXT("PointsMany", "очков")));
	}
	State->SetNewspaper(Headline, ObshagaStory::Subhead(WinnerName, WinnerScore), Notes, const_cast<APlayerState*>(Hero), HeroNote);
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
				LOCTEXT("ChrPickedUp2", "{0} решил, что ему нужнее. Предмет: {1} ({2})"),
				LOCTEXT("ChrPickedUp3", "{2}. Пропажа: {1}. Кто унёс: {0}. Никто ничего не видел") }), Who, ItemName, RoomName);
			break;
		case EGameEventType::ItemHidden:
			Line.Priority = 2;
			Line.Text = FText::Format(Pick({
				LOCTEXT("ChrHidden1", "{0} спрятал: {1} ({2})"),
				LOCTEXT("ChrHidden2", "{0} спрятал поглубже: {1} ({2}). Лицо при этом честное"),
				LOCTEXT("ChrHidden3", "Новый жилец тайника ({2}): {1}. Поселил: {0}") }), Who, ItemName, RoomName);
			break;
		case EGameEventType::PlayerFoundHiding:
			Line.Priority = 3;
			Line.Text = FText::Format(Pick({
				LOCTEXT("ChrFoundHiding1", "{0} открыл шкаф, а там сюрприз: {1}"),
				LOCTEXT("ChrFoundHiding2", "{0} заглянул в укрытие. Внутри: {1}. Неловко вышло"),
				LOCTEXT("ChrFoundHiding3", "{1} сидел тихо, пока не пришёл {0}") }), Who, Whom);
			break;
		case EGameEventType::ContrabandConfiscated:
			Line.Priority = 4;
			Line.Text = Event.Target
				? FText::Format(Pick({
					LOCTEXT("ChrConfiscatedHands1", "Комендант отобрал запрещёнку прямо из рук. Руки: {0}"),
					LOCTEXT("ChrConfiscated3", "Запрещёнка переехала на вахту. Скорбит: {0}") }), Who, RoomName)
				: FText::Format(Pick({
					LOCTEXT("ChrConfiscated1", "Комендант изъял запрещёнку. Привет, {0}"),
					LOCTEXT("ChrConfiscated2", "Комендант нашёл тайник ({1}). {0}, это было хорошее место"),
					LOCTEXT("ChrConfiscated3", "Запрещёнка переехала на вахту. Скорбит: {0}") }), Who, RoomName);
			break;
		case EGameEventType::TipOff:
			Line.Priority = 4;
			Line.Text = FText::Format(Pick({
				LOCTEXT("ChrTipOff1", "{0} настучал коменданту. Жертва: {1}"),
				LOCTEXT("ChrTipOff2", "{0} шепнул коменданту пару слов. Тема разговора: {1}"),
				LOCTEXT("ChrTipOff3", "Анонимный звонок на вахту. Голос подозрительно похож: {0}") }), Who, Whom);
			break;
		case EGameEventType::TipSucceeded:
			Line.Priority = 5;
			Line.Text = FText::Format(Pick({
				LOCTEXT("ChrTipWorked1", "Стук сработал. Попался: {1}. Доволен: {0}"),
				LOCTEXT("ChrTipWorked2", "{0} сдал соседа и не прогадал. Сосед: {1}") }), Who, Whom);
			break;
		case EGameEventType::PlayerCaught:
			Line.Priority = 5;
			Line.Text = Event.Item
				? FText::Format(Pick({
					LOCTEXT("ChrCaughtItem1", "{0} пойман ({1}). В руках: {2}. Как оно там оказалось — загадка"),
					LOCTEXT("ChrCaughtItem2", "{0} встретил коменданта ({1}). При себе: {2}"),
					LOCTEXT("ChrCaughtItem3", "Немая сцена ({1}): комендант, {0} и улика — {2}") }), Who, RoomName, ItemName)
				: FText::Format(Pick({
					LOCTEXT("ChrCaught1", "{0} пойман комендантом ({1})"),
					LOCTEXT("ChrCaught2", "{0} не успел убежать ({1})"),
					LOCTEXT("ChrCaught3", "Комендант: «А ты куда собрался?» Собрался: {0} ({1})") }), Who, RoomName);
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
				LOCTEXT("ChrLieFail3", "{0}: легенда развалилась на втором предложении") }), Who);
			break;
		case EGameEventType::AlibiConfirmed:
			Line.Priority = 5;
			Line.Text = FText::Format(Pick({
				LOCTEXT("ChrAlibi1", "{0} подтвердил алиби, выручен: {1}. Вот это дружба"),
				LOCTEXT("ChrAlibi2", "{0}: «Он был со мной». Речь про: {1}. Комендант поверил обоим"),
				LOCTEXT("ChrAlibi3", "{0} спас от страйка, спасённый: {1}. Должок") }), Who, Whom);
			break;
		case EGameEventType::NightEvent:
			Line.Priority = 4;
			if (Event.RoomId == TEXT("FireAlarm"))
			{
				Line.Text = LOCTEXT("ChrFireAlarm", "Пожарная тревога. Горело только у Палыча — терпение");
			}
			else if (Event.RoomId == TEXT("Deanery"))
			{
				Line.Text = LOCTEXT("ChrDeanery", "Стало известно: утром проверка из деканата. Общага срочно прячет лишнее");
			}
			else if (Event.RoomId == TEXT("Birthday"))
			{
				Line.Text = LOCTEXT("ChrBirthday", "В общаге день рождения. Палыч сделал вид, что не слышит");
			}
			else if (Event.RoomId == TEXT("PipeBurst"))
			{
				Line.Text = LOCTEXT("ChrPipe", "В санузле прорвало трубу. Палыч ушёл чинить, общага — пользоваться моментом");
			}
			else
			{
				Line.Text = LOCTEXT("ChrBlackout", "Выбило пробки. В темноте все стали смелее");
			}
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
				LOCTEXT("ChrFramed1", "Запрещёнку нашли в чужой комнате. Пострадал: {1}. Подбросил: {0}"),
				LOCTEXT("ChrFramed2", "{1} клянётся, что это не его. Чистая правда: подбросил {0}"),
				LOCTEXT("ChrFramed3", "{0} оставил маленький подарок. Получатель: {1}. Комендант оценил") }), Who, Whom);
			break;
		case EGameEventType::Accusation:
			Line.Priority = 4;
			if (Event.Item && Event.Item->GetItemData()->bReadable)
			{
				Line.Text = FText::Format(Pick({
					LOCTEXT("ChrExpose1", "{0} вычислил автора слуха: это {1}. Почерк подвёл"),
					LOCTEXT("ChrExpose2", "{0} показал коменданту, кто пишет записки: {1}") }), Who, Whom);
				break;
			}
			Line.Text = Event.Item
				? FText::Format(Pick({
					LOCTEXT("ChrAccuseRight1", "{0} показал коменданту вора: {1}. И не ошибся"),
					LOCTEXT("ChrAccuseRight2", "Детектив {0} раскрыл дело: вор — {1}") }), Who, Whom)
				: FText::Format(Pick({
					LOCTEXT("ChrAccuseWrong1", "{0} обвинил невиновного: {1}. Мимо"),
					LOCTEXT("ChrAccuseWrong2", "{0} ткнул пальцем мимо (обвинён: {1}). Комендант запомнил, кто тыкал") }), Who, Whom);
			break;
		case EGameEventType::RoomTipOff:
			Line.Priority = 4;
			Line.Text = FText::Format(Pick({
				LOCTEXT("ChrRoomTip1", "{0} настучал коменданту: «Обыщите — {1}»"),
				LOCTEXT("ChrRoomTip2", "{0} намекнул коменданту, что стоит проверить: {1}") }), Who, RoomName);
			break;
		case EGameEventType::NoteRead:
		{
			// Тот, кто пускает слух, сам берёт записку в руки и этим её «читает» — про него строки не пишем.
			const AObshagaPlayerState* Reader = Cast<AObshagaPlayerState>(Event.Instigator);
			if (Reader && Reader->GetTaskComponent()->FindRowByCondition(ETaskCondition::NoteReadByDistinctPlayers))
			{
				continue;
			}
		}
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
				LOCTEXT("ChrRepaired2", "{0} починил поломку ({1}) изолентой и добрым словом") }), Who, RoomName);
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

TArray<FText> AObshagaGameMode::BuildWeekTitles(const TArray<AObshagaPlayerState*>& Players) const
{
	TArray<FText> Titles;
	const AObshagaPlayerState* Legend = nullptr;
	const AObshagaPlayerState* Regular = nullptr;
	FString Shadows;
	for (const AObshagaPlayerState* PlayerState : Players)
	{
		if (!Legend || PlayerState->GetWeekScore() > Legend->GetWeekScore())
		{
			Legend = PlayerState;
		}
		if (PlayerState->GetWeekStrikes() > 0 && (!Regular || PlayerState->GetWeekStrikes() > Regular->GetWeekStrikes()))
		{
			Regular = PlayerState;
		}
		if (PlayerState->GetWeekStrikes() == 0)
		{
			Shadows += (Shadows.IsEmpty() ? TEXT("") : TEXT(", ")) + PlayerState->GetPlayerName();
		}
	}
	// Легенда — только с плюсом за неделю: «легенда с минус двадцатью» звучит как издёвка.
	if (Legend && Legend->GetWeekScore() > 0)
	{
		Titles.Add(FText::Format(ObshagaStory::WeekTitleFormat(0), FText::FromString(Legend->GetPlayerName()), FText::AsNumber(Legend->GetWeekScore())));
	}
	if (Regular)
	{
		Titles.Add(FText::Format(ObshagaStory::WeekTitleFormat(1), FText::FromString(Regular->GetPlayerName()), FText::AsNumber(Regular->GetWeekStrikes())));
	}
	if (!Shadows.IsEmpty())
	{
		Titles.Add(FText::Format(ObshagaStory::WeekTitleFormat(2), FText::FromString(Shadows)));
	}
	return Titles;
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
	Root->SetNumberField(TEXT("weekNight"), WeekNight);
	Root->SetNumberField(TEXT("weekId"), WeekId);

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
