#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameEventSubsystem.generated.h"

class AItemActor;
class AObshagaCharacter;
class APlayerState;

UENUM(BlueprintType)
enum class EGameEventType : uint8
{
	ItemPickedUp,
	ItemDropped,
	ItemThrown,
	ItemHidden,
	ItemFound,
	RoomEntered,
	RoomLeft,
	DoorOpened,
	DoorClosed,
	Noise,
	PlayerHid,
	PlayerLeftHiding,
	PlayerFoundHiding,
	RoundStarted,
	RoundEnded
};

/** Одна запись на «доске объявлений»: кто, что сделал, с каким предметом, где и когда. */
USTRUCT(BlueprintType)
struct FGameEvent
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	EGameEventType Type = EGameEventType::Noise;

	/** Кто это сделал. */
	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<APlayerState> Instigator = nullptr;

	/** Над кем это сделали (например, кого нашли в шкафу). */
	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<APlayerState> Target = nullptr;

	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<AItemActor> Item = nullptr;

	UPROPERTY(BlueprintReadOnly)
	FName RoomId;

	/** Секунды от старта мира на сервере. */
	UPROPERTY(BlueprintReadOnly)
	float Time = 0.f;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FOnGameEvent, const FGameEvent&);

/**
 * Шина событий. Живёт только на сервере: всё интересное в игре публикуется сюда,
 * а задания, хроника и СМС-слухи читают отсюда. На клиенте публикация ничего не делает.
 */
UCLASS()
class OBSHAGA_API UGameEventSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UGameEventSubsystem* Get(const UObject* WorldContextObject);

	/** Публикует событие от имени персонажа. Если комната не указана, берётся та, где он стоит. */
	static void PublishFrom(AObshagaCharacter* By, EGameEventType Type, AItemActor* Item = nullptr, AObshagaCharacter* Target = nullptr);

	void Publish(const FGameEvent& Event);

	/** Все события с начала раунда, по порядку. */
	const TArray<FGameEvent>& GetEventLog() const { return EventLog; }
	void ClearEventLog() { EventLog.Reset(); }

	FOnGameEvent OnGameEvent;

private:
	UPROPERTY()
	TArray<FGameEvent> EventLog;
};
