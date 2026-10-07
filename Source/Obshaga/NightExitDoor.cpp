#include "NightExitDoor.h"

#include "CarryComponent.h"
#include "GameEventSubsystem.h"
#include "Obshaga.h"
#include "ObshagaCharacter.h"
#include "ObshagaGameMode.h"
#include "ObshagaGameState.h"
#include "ObshagaPlayerController.h"
#include "ObshagaRoundConfig.h"

#define LOCTEXT_NAMESPACE "NightExitDoor"

namespace
{
	void NotifyWalker(AObshagaCharacter* Character, const FText& Text)
	{
		if (AObshagaPlayerController* Controller = Character ? Cast<AObshagaPlayerController>(Character->GetController()) : nullptr)
		{
			Controller->ClientShowNotice(Text);
		}
	}
}

ANightExitDoor::ANightExitDoor()
{
	DisplayName = LOCTEXT("DefaultName", "Ночной выход");
	BoxSize = FVector(20.f, 110.f, 210.f);
	bCanHidePlayer = true;
	ApplySize();
}

bool ANightExitDoor::IsNight() const
{
	const AObshagaGameState* GameState = GetWorld()->GetGameState<AObshagaGameState>();
	return GameState && GameState->GetRoundState() == ERoundState::InProgress && GameState->GetPhase() == ERoundPhase::Night;
}

FText ANightExitDoor::GetInteractionPrompt(const AObshagaCharacter* By) const
{
	if (!By)
	{
		return FText::GetEmpty();
	}
	if (By->GetHidingSpot() == this)
	{
		return LOCTEXT("Return", "Вернуться в общагу");
	}
	if (!IsNight())
	{
		return LOCTEXT("Locked", "Заперто до отбоя");
	}
	if (By->GetCarryComponent()->IsCarrying())
	{
		return LOCTEXT("HandsFull", "С вещами не выпустят");
	}
	return LOCTEXT("GoOut", "Выйти на улицу");
}

FText ANightExitDoor::GetSecondaryPrompt(const AObshagaCharacter* By) const
{
	return FText::GetEmpty();
}

void ANightExitDoor::SecondaryInteract(AObshagaCharacter* By)
{
}

FVector ANightExitDoor::GetHiddenPlayerLocation(float HalfHeight) const
{
	// «Лицо» двери (+X) смотрит в общагу, улица — с обратной стороны.
	return GetActorLocation() - GetActorForwardVector() * (BoxSize.X * 0.5f + 60.f) + FVector(0.f, 0.f, HalfHeight + 2.f);
}

void ANightExitDoor::ForceReturn()
{
	if (HasAuthority() && HiddenPlayer)
	{
		NotifyWalker(HiddenPlayer, LOCTEXT("ForcedBack", "Светает — пора обратно в общагу"));
		EjectHiddenPlayer();
	}
}

void ANightExitDoor::Interact(AObshagaCharacter* By)
{
	if (!HasAuthority() || !By)
	{
		return;
	}

	// Тот, кто был на улице, вышел из игры — дверь снова свободна.
	if (HiddenPlayer && !IsValid(HiddenPlayer))
	{
		HiddenPlayer = nullptr;
	}

	if (HiddenPlayer == By)
	{
		EjectHiddenPlayer();
		return;
	}
	if (!IsNight() || By->IsHiding() || By->GetCarryComponent()->IsCarrying())
	{
		return;
	}
	if (HiddenPlayer)
	{
		NotifyWalker(By, LOCTEXT("Occupied", "На улице уже кто-то есть — подожди"));
		return;
	}

	HiddenPlayer = By;
	LeftTime = GetWorld()->GetTimeSeconds();
	// Событие — до входа в укрытие, пока персонаж ещё числится в комнате выхода.
	UGameEventSubsystem::PublishFrom(By, EGameEventType::LeftBuilding);
	By->EnterHidingSpot(this);
	NotifyWalker(By, LOCTEXT("Out", "Ты на улице. Погуляй и возвращайся [E]"));
	UE_LOG(LogObshaga, Verbose, TEXT("%s left the building"), *By->GetName());
}

void ANightExitDoor::EjectHiddenPlayer()
{
	AObshagaCharacter* Player = HiddenPlayer;
	Super::EjectHiddenPlayer();
	if (!Player)
	{
		return;
	}

	// Вылазка засчитывается, только если гулял достаточно долго.
	const AObshagaGameMode* GameMode = GetWorld()->GetAuthGameMode<AObshagaGameMode>();
	const float Needed = GameMode ? GameMode->GetRoundConfig()->CurfewOutsideSeconds : 0.f;
	if (GetWorld()->GetTimeSeconds() - LeftTime >= Needed)
	{
		UGameEventSubsystem::PublishFrom(Player, EGameEventType::ReturnedToBuilding);
		NotifyWalker(Player, LOCTEXT("Back", "Ты вернулся с улицы. Теперь не попадись"));
	}
	else
	{
		NotifyWalker(Player, FText::Format(LOCTEXT("TooFast", "Слишком быстро вернулся: гулять надо {0} с"), FText::AsNumber(FMath::RoundToInt32(Needed))));
	}
}

#undef LOCTEXT_NAMESPACE
