#include "ObshagaSessionSubsystem.h"

#include "Obshaga.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Online/OnlineSessionNames.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "TimerManager.h"
#include "UObject/UObjectGlobals.h"

#if OBSHAGA_WITH_STEAM
THIRD_PARTY_INCLUDES_START
#pragma push_macro("ARRAY_COUNT")
#undef ARRAY_COUNT
#include "steam/steam_api.h"
#pragma pop_macro("ARRAY_COUNT")
THIRD_PARTY_INCLUDES_END
#endif

#define LOCTEXT_NAMESPACE "ObshagaSession"

namespace
{
	// Без похожих друг на друга букв и цифр: нет I, O, 0, 1.
	const TCHAR* CodeAlphabet = TEXT("ABCDEFGHJKLMNPQRSTUVWXYZ23456789");

	// Ключ нашей игры. Тестовый номер приложения Steam (480) общий для всех разработчиков,
	// и в поиске попадаются чужие комнаты — отбираем только свои.
	const FName GameKeyName(TEXT("OBSHAGAKEY"));
	const TCHAR* GameKeyValue = TEXT("ne_spalimsya_1");
	const FName RoomCodeName(TEXT("ROOMCODE"));

	const TCHAR* GameMap = TEXT("/Game/Obshaga/Map/L_Obshaga");
	const TCHAR* MenuMap = TEXT("/Engine/Maps/Entry");
	const TCHAR* MenuOptions = TEXT("game=/Script/Obshaga.ObshagaMenuGameMode");

	// Столько ждём перед закрытием комнаты, чтобы гости успели получить «Хост закрыл комнату».
	constexpr float HostCloseDelaySeconds = 0.4f;

	// Сколько ждём ответа Steam на поиск по всему миру.
	constexpr float WorldSearchTimeoutSeconds = 15.f;

	FString MakeCode()
	{
		FString Code;
		const int32 AlphabetLength = FCString::Strlen(CodeAlphabet);
		for (int32 Index = 0; Index < UObshagaSessionSubsystem::CodeLength; ++Index)
		{
			Code.AppendChar(CodeAlphabet[FMath::RandRange(0, AlphabetLength - 1)]);
		}
		return Code;
	}
}

/**
 * Поиск комнаты по коду по всему миру, напрямую через Steam.
 * Зачем: поиск движка (OnlineSubsystemSteam) всегда ставит фильтр расстояния «по умолчанию» — только свой и
 * соседние регионы, и поменять его снаружи нельзя. Друг из далёкого региона комнату бы не нашёл.
 * Как: тот же запрос списка лобби, но с фильтром «весь мир» и нашими полями (их имена и адрес хоста
 * записывает сам движок при создании комнаты). К хосту подключаемся по его номеру Steam, как это сделал бы движок.
 */
struct FObshagaLobbyFinder : public TSharedFromThis<FObshagaLobbyFinder, ESPMode::ThreadSafe>
{
	FString Code;
	TFunction<void(const FString&)> Done;

#if OBSHAGA_WITH_STEAM
	CCallResult<FObshagaLobbyFinder, LobbyMatchList_t> Call;

	bool Start()
	{
		ISteamMatchmaking* Matchmaking = SteamMatchmaking();
		if (!Matchmaking)
		{
			return false;
		}
		// Имена полей — как их пишет движок: имя настройки + «_s» для строк.
		Matchmaking->AddRequestLobbyListDistanceFilter(k_ELobbyDistanceFilterWorldwide);
		Matchmaking->AddRequestLobbyListStringFilter("OBSHAGAKEY_s", TCHAR_TO_UTF8(GameKeyValue), k_ELobbyComparisonEqual);
		Matchmaking->AddRequestLobbyListStringFilter("ROOMCODE_s", TCHAR_TO_UTF8(*Code), k_ELobbyComparisonEqual);
		Matchmaking->AddRequestLobbyListResultCountFilter(10);
		const SteamAPICall_t Handle = Matchmaking->RequestLobbyList();
		if (Handle == k_uAPICallInvalid)
		{
			return false;
		}
		Call.Set(Handle, this, &FObshagaLobbyFinder::OnList);
		return true;
	}

	void OnList(LobbyMatchList_t* Result, bool bIOFailure)
	{
		// Ответ Steam приходит не в игровом потоке: здесь только читаем данные лобби, остальное — в игровом.
		FString Address;
		ISteamMatchmaking* Matchmaking = SteamMatchmaking();
		for (uint32 Index = 0; Matchmaking && !bIOFailure && Index < Result->m_nLobbiesMatching && Address.IsEmpty(); ++Index)
		{
			const CSteamID Lobby = Matchmaking->GetLobbyByIndex(Index);
			const FString FoundCode = UTF8_TO_TCHAR(Matchmaking->GetLobbyData(Lobby, "ROOMCODE_s"));
			const FString Host = UTF8_TO_TCHAR(Matchmaking->GetLobbyData(Lobby, "P2PADDR"));
			const FString Port = UTF8_TO_TCHAR(Matchmaking->GetLobbyData(Lobby, "P2PPORT"));
			if (FoundCode == Code && !Host.IsEmpty())
			{
				Address = FString::Printf(TEXT("steam.%s:%s"), *Host, Port.IsEmpty() ? TEXT("7777") : *Port);
			}
		}
		AsyncTask(ENamedThreads::GameThread, [Self = AsShared(), Address]
		{
			if (Self->Done)
			{
				Self->Done(Address);
			}
		});
	}
#else
	bool Start() { return false; }
#endif
};

void UObshagaSessionSubsystem::StartWorldSearch()
{
	WorldFinder = MakeShared<FObshagaLobbyFinder, ESPMode::ThreadSafe>();
	WorldFinder->Code = WantedCode;
	WorldFinder->Done = [WeakThis = TWeakObjectPtr<UObshagaSessionSubsystem>(this)](const FString& Address)
	{
		if (WeakThis.IsValid())
		{
			WeakThis->HandleWorldSearchDone(Address);
		}
	};
	if (!WorldFinder->Start())
	{
		HandleWorldSearchDone(FString());
		return;
	}
	// Если Steam не ответит, не висеть на «Ищем комнату…» вечно.
	GetGameInstance()->GetTimerManager().SetTimer(WorldSearchTimer, FTimerDelegate::CreateWeakLambda(this, [this] { HandleWorldSearchDone(FString()); }), WorldSearchTimeoutSeconds, false);
}

void UObshagaSessionSubsystem::HandleWorldSearchDone(const FString& Address)
{
	GetGameInstance()->GetTimerManager().ClearTimer(WorldSearchTimer);
	if (WorldFinder.IsValid())
	{
		WorldFinder->Done = nullptr;
		WorldFinder.Reset();
	}
	if (Busy != ERoomBusy::Searching)
	{
		return;
	}

	UE_LOG(LogObshaga, Log, TEXT("Worldwide room search for %s: %s"), *WantedCode, Address.IsEmpty() ? TEXT("not found") : *Address);
	if (Address.IsEmpty())
	{
		Fail(FText::Format(LOCTEXT("NotFound", "Комната с кодом {0} не найдена. Проверь код: хост видит его у себя на экране."), FText::FromString(WantedCode)));
		return;
	}
	Busy = ERoomBusy::Joining;
	TravelToRoom(Address);
}

void UObshagaSessionSubsystem::TravelToRoom(FString Address)
{
	APlayerController* Controller = GetGameInstance()->GetFirstLocalPlayerController();
	if (!Controller)
	{
		Fail(LOCTEXT("JoinFailed", "Не получилось войти в комнату. Попробуй ещё раз."));
		return;
	}

	RoomCode = WantedCode;
	WantedCode.Reset();
#if !UE_BUILD_SHIPPING
	// Для проверки повторного входа без Steam: -ReconnectKey=abc (см. AObshagaGameMode::InitNewPlayer).
	FString TestKey;
	if (FParse::Value(FCommandLine::Get(), TEXT("ReconnectKey="), TestKey) && !TestKey.IsEmpty())
	{
		Address += TEXT("?RKey=") + TestKey;
	}
#endif
	UE_LOG(LogObshaga, Log, TEXT("Joining room %s at %s"), *RoomCode, *Address);
	Controller->ClientTravel(Address, TRAVEL_Absolute);
}

void UObshagaSessionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (GEngine)
	{
		NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &UObshagaSessionSubsystem::HandleNetworkFailure);
		TravelFailureHandle = GEngine->OnTravelFailure().AddUObject(this, &UObshagaSessionSubsystem::HandleTravelFailure);
	}
	// Карта загрузилась — значит, создание, вход или выход закончились.
	FCoreUObjectDelegates::PostLoadMapWithWorld.AddWeakLambda(this, [this](UWorld* World)
	{
		if (World && World->GetGameInstance() == GetGameInstance())
		{
			Busy = ERoomBusy::Idle;
		}
	});
}

void UObshagaSessionSubsystem::Deinitialize()
{
	if (GEngine)
	{
		GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
		GEngine->OnTravelFailure().Remove(TravelFailureHandle);
	}
	FCoreUObjectDelegates::PostLoadMapWithWorld.RemoveAll(this);

	// Игру закрыли, не выходя из комнаты: снимаем объявление, чтобы по коду не находили мёртвую комнату.
	const IOnlineSessionPtr Sessions = GetSessions();
	if (Sessions.IsValid() && Sessions->GetNamedSession(NAME_GameSession))
	{
		Sessions->DestroySession(NAME_GameSession);
	}

	Super::Deinitialize();
}

IOnlineSessionPtr UObshagaSessionSubsystem::GetSessions() const
{
	const IOnlineSubsystem* Online = Online::GetSubsystem(GetGameInstance()->GetWorld());
	return Online ? Online->GetSessionInterface() : nullptr;
}

bool UObshagaSessionSubsystem::IsSteam() const
{
	const IOnlineSubsystem* Online = Online::GetSubsystem(GetGameInstance()->GetWorld());
	return Online && Online->GetSubsystemName() == STEAM_SUBSYSTEM;
}

FString UObshagaSessionSubsystem::GetLocalPlayerName() const
{
	// Для проверок без Steam: -PlayerName="Имя".
	FString Name;
	if (FParse::Value(FCommandLine::Get(), TEXT("PlayerName="), Name) && !Name.IsEmpty())
	{
		return Name;
	}
	if (IsSteam())
	{
		const IOnlineSubsystem* Online = Online::GetSubsystem(GetGameInstance()->GetWorld());
		const IOnlineIdentityPtr Identity = Online->GetIdentityInterface();
		if (Identity.IsValid())
		{
			return Identity->GetPlayerNickname(0);
		}
	}
	return FString();
}

bool UObshagaSessionSubsystem::IsCodeChar(TCHAR Char)
{
	return Char != 0 && FCString::Strchr(CodeAlphabet, FChar::ToUpper(Char)) != nullptr;
}

FString UObshagaSessionSubsystem::NormalizeCode(const FString& Raw)
{
	FString Code;
	for (const TCHAR Char : Raw)
	{
		if (IsCodeChar(Char) && Code.Len() < CodeLength)
		{
			Code.AppendChar(FChar::ToUpper(Char));
		}
	}
	return Code;
}

void UObshagaSessionSubsystem::Fail(const FText& Text)
{
	UE_LOG(LogObshaga, Warning, TEXT("Room: %s"), *Text.ToString());
	Message = Text;
	Busy = ERoomBusy::Idle;
	WantedCode.Reset();
	Search.Reset();
}

void UObshagaSessionSubsystem::DestroyThen(TFunction<void()> Then)
{
	const IOnlineSessionPtr Sessions = GetSessions();
	if (!Sessions.IsValid() || !Sessions->GetNamedSession(NAME_GameSession))
	{
		Then();
		return;
	}

	AfterDestroy = MoveTemp(Then);
	DestroyHandle = Sessions->AddOnDestroySessionCompleteDelegate_Handle(FOnDestroySessionCompleteDelegate::CreateUObject(this, &UObshagaSessionSubsystem::HandleDestroyComplete));
	if (!Sessions->DestroySession(NAME_GameSession))
	{
		Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyHandle);
		TFunction<void()> Next = MoveTemp(AfterDestroy);
		AfterDestroy = nullptr;
		Next();
	}
}

void UObshagaSessionSubsystem::HandleDestroyComplete(FName SessionName, bool bSuccess)
{
	const IOnlineSessionPtr Sessions = GetSessions();
	if (Sessions.IsValid())
	{
		Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyHandle);
	}
	if (AfterDestroy)
	{
		TFunction<void()> Next = MoveTemp(AfterDestroy);
		AfterDestroy = nullptr;
		Next();
	}
}

void UObshagaSessionSubsystem::CreateRoom()
{
	if (IsBusy())
	{
		return;
	}
	if (!GetSessions().IsValid())
	{
		Fail(LOCTEXT("NoOnline", "Сетевая часть игры не запустилась. Перезапусти игру."));
		return;
	}

	Message = FText::GetEmpty();
	Busy = ERoomBusy::Creating;
	DestroyThen([this] { StartCreate(); });
}

void UObshagaSessionSubsystem::StartCreate()
{
	const IOnlineSessionPtr Sessions = GetSessions();
	if (!Sessions.IsValid())
	{
		Fail(LOCTEXT("NoOnline", "Сетевая часть игры не запустилась. Перезапусти игру."));
		return;
	}

	const bool bSteam = IsSteam();
	WantedCode = MakeCode();

	FOnlineSessionSettings Settings;
	Settings.NumPublicConnections = MaxPlayers;
	Settings.bShouldAdvertise = true;
	Settings.bAllowJoinInProgress = true;
	Settings.bIsLANMatch = !bSteam;
	// У Steam эти два флага обязаны совпадать: оба означают «комната — это лобби Steam».
	Settings.bUsesPresence = bSteam;
	Settings.bUseLobbiesIfAvailable = bSteam;
	Settings.bAllowJoinViaPresence = bSteam;
	Settings.Set(GameKeyName, FString(GameKeyValue), EOnlineDataAdvertisementType::ViaOnlineService);
	Settings.Set(RoomCodeName, WantedCode, EOnlineDataAdvertisementType::ViaOnlineService);

	CreateHandle = Sessions->AddOnCreateSessionCompleteDelegate_Handle(FOnCreateSessionCompleteDelegate::CreateUObject(this, &UObshagaSessionSubsystem::HandleCreateComplete));
	if (!Sessions->CreateSession(0, NAME_GameSession, Settings))
	{
		Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateHandle);
		Fail(LOCTEXT("CreateFailed", "Не получилось создать комнату. Попробуй ещё раз."));
	}
}

void UObshagaSessionSubsystem::HandleCreateComplete(FName SessionName, bool bSuccess)
{
	const IOnlineSessionPtr Sessions = GetSessions();
	if (Sessions.IsValid())
	{
		Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateHandle);
	}
	if (!bSuccess)
	{
		Fail(LOCTEXT("CreateFailed", "Не получилось создать комнату. Попробуй ещё раз."));
		return;
	}

	RoomCode = WantedCode;
	WantedCode.Reset();
	UE_LOG(LogObshaga, Log, TEXT("Room created, code %s (%s)"), *RoomCode, IsSteam() ? TEXT("Steam") : TEXT("LAN"));
	// Хост сам загружает карту и начинает слушать подключения.
	UGameplayStatics::OpenLevel(GetGameInstance()->GetWorld(), FName(GameMap), true, FString::Printf(TEXT("listen?MaxPlayers=%d"), MaxPlayers));
}

void UObshagaSessionSubsystem::JoinByCode(const FString& Code)
{
	if (IsBusy())
	{
		return;
	}

	const FString Clean = NormalizeCode(Code);
	if (Clean.Len() != CodeLength)
	{
		Fail(FText::Format(LOCTEXT("CodeShort", "В коде комнаты {0} символов"), FText::AsNumber(CodeLength)));
		return;
	}
	if (!GetSessions().IsValid())
	{
		Fail(LOCTEXT("NoOnline", "Сетевая часть игры не запустилась. Перезапусти игру."));
		return;
	}

	Message = FText::GetEmpty();
	Busy = ERoomBusy::Searching;
	WantedCode = Clean;
	// После вылета могла остаться запись о старой комнате — с ней войти заново не дадут.
	DestroyThen([this] { StartSearch(); });
}

void UObshagaSessionSubsystem::StartSearch()
{
	const IOnlineSessionPtr Sessions = GetSessions();
	if (!Sessions.IsValid())
	{
		Fail(LOCTEXT("NoOnline", "Сетевая часть игры не запустилась. Перезапусти игру."));
		return;
	}

	const bool bSteam = IsSteam();
	Search = MakeShared<FOnlineSessionSearch>();
	Search->bIsLanQuery = !bSteam;
	Search->MaxSearchResults = 50;
	if (bSteam)
	{
		Search->QuerySettings.Set(SEARCH_LOBBIES, true, EOnlineComparisonOp::Equals);
	}
	// Steam отбирает по этим полям у себя; поиск по локальной сети их не учитывает, поэтому ниже проверяем ещё раз сами.
	Search->QuerySettings.Set(GameKeyName, FString(GameKeyValue), EOnlineComparisonOp::Equals);
	Search->QuerySettings.Set(RoomCodeName, WantedCode, EOnlineComparisonOp::Equals);

	FindHandle = Sessions->AddOnFindSessionsCompleteDelegate_Handle(FOnFindSessionsCompleteDelegate::CreateUObject(this, &UObshagaSessionSubsystem::HandleFindComplete));
	if (!Sessions->FindSessions(0, Search.ToSharedRef()))
	{
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindHandle);
		Fail(LOCTEXT("SearchFailed", "Поиск комнаты не запустился. Проверь интернет и попробуй ещё раз."));
	}
}

void UObshagaSessionSubsystem::HandleFindComplete(bool bSuccess)
{
	const IOnlineSessionPtr Sessions = GetSessions();
	if (Sessions.IsValid())
	{
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindHandle);
	}
	if (Busy != ERoomBusy::Searching || !Search.IsValid() || !Sessions.IsValid())
	{
		return;
	}

	FOnlineSessionSearchResult* Found = nullptr;
	for (FOnlineSessionSearchResult& Result : Search->SearchResults)
	{
		FString Key;
		FString Code;
		Result.Session.SessionSettings.Get(GameKeyName, Key);
		Result.Session.SessionSettings.Get(RoomCodeName, Code);
		if (Key == GameKeyValue && Code == WantedCode)
		{
			Found = &Result;
			break;
		}
	}
	UE_LOG(LogObshaga, Log, TEXT("Room search for %s: %d results, match %d"), *WantedCode, Search->SearchResults.Num(), Found ? 1 : 0);

	if (!Found && IsSteam())
	{
		// Рядом не нашлось — ищем по всему миру (хост может быть в далёком регионе).
		Search.Reset();
		StartWorldSearch();
		return;
	}
	if (!Found)
	{
		Fail(FText::Format(LOCTEXT("NotFound", "Комната с кодом {0} не найдена. Проверь код: хост видит его у себя на экране."), FText::FromString(WantedCode)));
		return;
	}
	if (Found->Session.NumOpenPublicConnections <= 0)
	{
		Fail(LOCTEXT("Full", "Комната полна: в ней уже 8 игроков."));
		return;
	}

	if (IsSteam())
	{
		// Steam возвращает результат с разными значениями этих флагов и потом сам же отказывается по нему входить.
		Found->Session.SessionSettings.bUsesPresence = true;
		Found->Session.SessionSettings.bUseLobbiesIfAvailable = true;
	}

	Busy = ERoomBusy::Joining;
	JoinHandle = Sessions->AddOnJoinSessionCompleteDelegate_Handle(FOnJoinSessionCompleteDelegate::CreateUObject(this, &UObshagaSessionSubsystem::HandleJoinComplete));
	if (!Sessions->JoinSession(0, NAME_GameSession, *Found))
	{
		Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinHandle);
		Fail(LOCTEXT("JoinFailed", "Не получилось войти в комнату. Попробуй ещё раз."));
	}
}

void UObshagaSessionSubsystem::HandleJoinComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result)
{
	const IOnlineSessionPtr Sessions = GetSessions();
	if (Sessions.IsValid())
	{
		Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinHandle);
	}
	Search.Reset();

	if (Result == EOnJoinSessionCompleteResult::SessionIsFull)
	{
		Fail(LOCTEXT("Full", "Комната полна: в ней уже 8 игроков."));
		return;
	}

	FString Address;
	if (Result != EOnJoinSessionCompleteResult::Success || !Sessions.IsValid() || !Sessions->GetResolvedConnectString(NAME_GameSession, Address))
	{
		DestroyThen([this] { Fail(LOCTEXT("JoinFailed", "Не получилось войти в комнату. Попробуй ещё раз.")); });
		return;
	}
	TravelToRoom(Address);
}

void UObshagaSessionSubsystem::LeaveRoom(const FText& Reason)
{
	if (Busy == ERoomBusy::Leaving)
	{
		return;
	}

	Busy = ERoomBusy::Leaving;
	Message = Reason;
	RoomCode.Reset();

	UWorld* World = GetGameInstance()->GetWorld();
	if (World && World->GetNetMode() == NM_ListenServer)
	{
		// Хост уходит — комнаты больше нет. Говорим гостям прямо, иначе они увидят просто «связь потеряна».
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* Controller = It->Get();
			if (Controller && !Controller->IsLocalController())
			{
				Controller->ClientReturnToMainMenuWithTextReason(LOCTEXT("HostClosed", "Хост закрыл комнату."));
			}
		}
		FTimerHandle Handle;
		GetGameInstance()->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this]
		{
			DestroyThen([this] { OpenMenu(); });
		}), HostCloseDelaySeconds, false);
		return;
	}

	DestroyThen([this] { OpenMenu(); });
}

void UObshagaSessionSubsystem::OpenMenu()
{
	UGameplayStatics::OpenLevel(GetGameInstance()->GetWorld(), FName(MenuMap), true, MenuOptions);
}

void UObshagaSessionSubsystem::HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString)
{
	// В редакторе у каждого окна PIE своя подсистема; чужие обрывы не наши.
	if (!World || World->GetGameInstance() != GetGameInstance() || World->GetNetMode() == NM_ListenServer || Busy == ERoomBusy::Leaving)
	{
		return;
	}

	UE_LOG(LogObshaga, Warning, TEXT("Network failure %s: %s"), ENetworkFailure::ToString(FailureType), *ErrorString);
	FText Text;
	if (ErrorString.Contains(TEXT("full"), ESearchCase::IgnoreCase))
	{
		Text = LOCTEXT("Full", "Комната полна: в ней уже 8 игроков.");
	}
	else if (FailureType == ENetworkFailure::ConnectionLost || FailureType == ENetworkFailure::ConnectionTimeout)
	{
		Text = LOCTEXT("Lost", "Связь с хостом потеряна. Если хост ещё в игре, зайди снова по тому же коду.");
	}
	else if (FailureType == ENetworkFailure::OutdatedClient || FailureType == ENetworkFailure::OutdatedServer)
	{
		Text = LOCTEXT("Version", "У тебя и у хоста разные версии игры.");
	}
	else
	{
		Text = LOCTEXT("ConnectFailed", "Не получилось подключиться к комнате. Попробуй ещё раз.");
	}

	// В меню движок вернёт сам (на карту по умолчанию); нам остаётся объяснить причину и убрать запись о комнате.
	RoomCode.Reset();
	Fail(Text);
	DestroyThen([] {});
}

void UObshagaSessionSubsystem::HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString)
{
	if (!World || World->GetGameInstance() != GetGameInstance() || Busy == ERoomBusy::Idle)
	{
		return;
	}

	UE_LOG(LogObshaga, Warning, TEXT("Travel failure %s: %s"), ETravelFailure::ToString(FailureType), *ErrorString);
	RoomCode.Reset();
	Fail(LOCTEXT("TravelFailed", "Не получилось загрузить комнату. Попробуй ещё раз."));
	DestroyThen([] {});
}

#undef LOCTEXT_NAMESPACE
