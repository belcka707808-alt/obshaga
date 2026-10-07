#include "GameEventSubsystem.h"

#include "ItemActor.h"
#include "Obshaga.h"
#include "ObshagaCharacter.h"
#include "RoomVolume.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"

UGameEventSubsystem* UGameEventSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UGameEventSubsystem>() : nullptr;
}

void UGameEventSubsystem::PublishFrom(AObshagaCharacter* By, EGameEventType Type, AItemActor* Item, AObshagaCharacter* Target)
{
	UGameEventSubsystem* Bus = Get(By);
	if (!Bus || !By)
	{
		return;
	}

	FGameEvent Event;
	Event.Type = Type;
	Event.Instigator = By->GetPlayerState();
	Event.Target = Target ? Target->GetPlayerState() : nullptr;
	Event.Item = Item;
	if (const ARoomVolume* Room = By->GetCurrentRoom())
	{
		Event.RoomId = Room->RoomId;
	}
	Bus->Publish(Event);
}

void UGameEventSubsystem::Publish(const FGameEvent& Event)
{
	const UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		return;
	}

	FGameEvent& Stored = EventLog.Add_GetRef(Event);
	Stored.Time = World->GetTimeSeconds();

	UE_LOG(LogObshaga, Verbose, TEXT("Event %s by %s item=%s room=%s target=%s"),
		*UEnum::GetValueAsString(Stored.Type),
		Stored.Instigator ? *Stored.Instigator->GetPlayerName() : TEXT("-"),
		*GetNameSafe(Stored.Item),
		*Stored.RoomId.ToString(),
		Stored.Target ? *Stored.Target->GetPlayerName() : TEXT("-"));

	OnGameEvent.Broadcast(Stored);
}
