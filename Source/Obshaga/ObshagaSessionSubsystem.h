#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ObshagaSessionSubsystem.generated.h"

class FOnlineSessionSearch;
class IOnlineSubsystem;
class UNetDriver;

/** Чем сейчас занят слой комнат (меню показывает это текстом и не даёт нажать второй раз). */
UENUM()
enum class ERoomBusy : uint8
{
	Idle,
	Creating,
	Searching,
	Joining,
	Leaving,
};

/**
 * Комнаты для игры с друзьями: создать, найти по коду, войти, выйти.
 * Живёт всё время работы игры (переживает смену карт). Работает поверх Online Subsystem:
 * со Steam это лобби Steam, без Steam — поиск в локальной сети (OnlineSubsystemNull).
 * Игровая логика сюда не заходит: подсистема только приводит игрока на карту хоста и обратно в меню.
 */
UCLASS()
class OBSHAGA_API UObshagaSessionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Создать комнату: сессия + карта общаги в режиме listen server. */
	void CreateRoom();
	/** Найти комнату по коду и подключиться. */
	void JoinByCode(const FString& Code);
	/** Выйти в меню. Хост этим закрывает комнату для всех. Reason покажется в меню (пусто — ничего). */
	void LeaveRoom(const FText& Reason = FText::GetEmpty());

	ERoomBusy GetBusy() const { return Busy; }
	bool IsBusy() const { return Busy != ERoomBusy::Idle; }
	/** Код комнаты, в которой мы сейчас (пусто — не в комнате, например в PIE). */
	const FString& GetRoomCode() const { return RoomCode; }
	/** Последняя ошибка или причина возврата в меню; меню показывает её, пока игрок не начнёт что-то новое. */
	const FText& GetMessage() const { return Message; }
	void SetMessage(const FText& Text) { Message = Text; }

	/** true — работает Steam (имена берём из него); false — запасной путь по локальной сети. */
	bool IsSteam() const;
	/** Имя игрока для показа другим: из командной строки (-PlayerName=), иначе из Steam, иначе пусто. */
	FString GetLocalPlayerName() const;

	/** Приводит введённый код к виду кодов комнат: верхний регистр, только буквы и цифры алфавита кодов. */
	static FString NormalizeCode(const FString& Raw);
	/** Можно ли набрать этот символ в коде комнаты. */
	static bool IsCodeChar(TCHAR Char);
	static constexpr int32 CodeLength = 5;
	static constexpr int32 MaxPlayers = 8;

private:
	IOnlineSessionPtr GetSessions() const;
	/** Уничтожает оставшуюся сессию (если есть) и после этого зовёт Then. */
	void DestroyThen(TFunction<void()> Then);
	void Fail(const FText& Text);
	void OpenMenu();

	void StartCreate();
	void StartSearch();
	/** Запасной поиск напрямую через Steam, без ограничения по расстоянию. */
	void StartWorldSearch();
	void HandleWorldSearchDone(const FString& Address);
	void TravelToRoom(FString Address);

	void HandleCreateComplete(FName SessionName, bool bSuccess);
	void HandleFindComplete(bool bSuccess);
	void HandleJoinComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result);
	void HandleDestroyComplete(FName SessionName, bool bSuccess);
	void HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString);
	void HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString);

	ERoomBusy Busy = ERoomBusy::Idle;
	FString RoomCode;
	FString WantedCode;
	FText Message;

	TSharedPtr<FOnlineSessionSearch> Search;
	TFunction<void()> AfterDestroy;
	TSharedPtr<struct FObshagaLobbyFinder, ESPMode::ThreadSafe> WorldFinder;
	FTimerHandle WorldSearchTimer;

	FDelegateHandle CreateHandle;
	FDelegateHandle FindHandle;
	FDelegateHandle JoinHandle;
	FDelegateHandle DestroyHandle;
	FDelegateHandle NetworkFailureHandle;
	FDelegateHandle TravelFailureHandle;
};
