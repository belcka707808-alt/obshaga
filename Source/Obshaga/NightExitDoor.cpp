#include "NightExitDoor.h"

#include "CarryComponent.h"
#include "GameEventSubsystem.h"
#include "Obshaga.h"
#include "ObshagaCharacter.h"
#include "ObshagaGameMode.h"
#include "ObshagaGameState.h"
#include "ObshagaPlayerController.h"
#include "ObshagaPlayerState.h"
#include "ObshagaRoundConfig.h"
#include "TaskComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

#define LOCTEXT_NAMESPACE "NightExitDoor"

namespace
{
	// Сколько после возвращения повторное нажатие E не выпускает обратно на улицу (с запасом на задержку сети).
	constexpr float ReturnGuardSeconds = 1.f;

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
	// Зелёная, как табличка «Выход».
	Color = FLinearColor(0.05f, 0.35f, 0.12f);
	ApplySize();
}

bool ANightExitDoor::IsNight() const
{
	const AObshagaGameState* GameState = GetWorld()->GetGameState<AObshagaGameState>();
	return GameState && GameState->GetRoundState() == ERoundState::InProgress && GameState->GetPhase() == ERoundPhase::Night;
}

bool ANightExitDoor::IsOnStreetSide(const AObshagaCharacter* By) const
{
	// «Лицо» двери (+X) смотрит в общагу, улица — с обратной стороны.
	return By && !By->IsHiding() && FVector::DotProduct(By->GetActorLocation() - GetActorLocation(), GetActorForwardVector()) < -BoxSize.X * 0.5f;
}

FText ANightExitDoor::GetInteractionPrompt(const AObshagaCharacter* By) const
{
	if (!By)
	{
		return FText::GetEmpty();
	}
	if (By->GetHidingSpot() == this || IsOnStreetSide(By))
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

void ANightExitDoor::LetIn(AObshagaCharacter* Character, const FText& Notice)
{
	if (!HasAuthority() || !Character || Character->IsHiding())
	{
		return;
	}

	// Встаёт там же, где выходит вернувшийся гуляка: перед дверью со стороны общаги.
	const FVector Inside = ExitPoint->GetComponentLocation();
	Character->GetCharacterMovement()->StopMovementImmediately();
	if (!Character->TeleportTo(Inside, Character->GetActorRotation()))
	{
		Character->SetActorLocation(Inside, false, nullptr, ETeleportType::TeleportPhysics);
	}
	Character->ForceNetUpdate();
	RememberReturned(Character);
	NotifyWalker(Character, Notice);
	// Ночная вылазка засчитывается только через саму дверь (выйти и вернуться); прыжок с балкона — нет.
	// Говорим об этом сразу и только тому, у кого такое задание, а не на итогах, где оно окажется проваленным.
	const AObshagaPlayerState* State = Character->GetPlayerState<AObshagaPlayerState>();
	if (IsNight() && !Character->IsGhost() && State && State->GetTaskComponent()->FindRowByCondition(ETaskCondition::LeftBuildingAndReturnedUnseen))
	{
		NotifyWalker(Character, LOCTEXT("NotASortie", "Прыжок с балкона — не вылазка: на улицу надо выходить через ночной выход"));
	}
	UE_LOG(LogObshaga, Verbose, TEXT("%s let in from the street through %s"), *Character->GetName(), *GetName());
}

bool ANightExitDoor::TryLetInFromStreet(AObshagaCharacter* By)
{
	if (!HasAuthority() || !IsOnStreetSide(By))
	{
		return false;
	}
	LetIn(By, LOCTEXT("CameIn", "Ты вернулся в общагу через ночной выход"));
	return true;
}

void ANightExitDoor::RememberReturned(AObshagaCharacter* Character)
{
	LastReturned = Character;
	LastReturnedTime = GetWorld()->GetTimeSeconds();
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
	// Оказался на улице не через эту дверь (спрыгнул с балкона): снаружи она открыта всегда, иначе назад не попасть.
	if (TryLetInFromStreet(By))
	{
		return;
	}
	if (!IsNight() || By->IsHiding() || By->GetCarryComponent()->IsCarrying())
	{
		return;
	}
	// Только что вернулся, а клиент ещё не узнал об этом и нажал «Вернуться» второй раз.
	if (LastReturned.Get() == By && GetWorld()->GetTimeSeconds() - LastReturnedTime < ReturnGuardSeconds)
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
	RememberReturned(Player);

	// Вылазка засчитывается, только если гулял достаточно долго.
	const AObshagaGameMode* GameMode = GetWorld()->GetAuthGameMode<AObshagaGameMode>();
	const float Needed = GameMode ? GameMode->GetRoundConfig()->CurfewOutsideSeconds : 0.f;
	// Для автопроверки задания «ночная вылазка»: сколько гулял и засчитано ли.
	UE_LOG(LogObshaga, Verbose, TEXT("Sortie of %s (%s): outside %.1f s of %.1f, counted %d"),
		Player->GetPlayerState() ? *Player->GetPlayerState()->GetPlayerName() : TEXT("None"), *Player->GetName(),
		GetWorld()->GetTimeSeconds() - LeftTime, Needed, GetWorld()->GetTimeSeconds() - LeftTime >= Needed ? 1 : 0);
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
