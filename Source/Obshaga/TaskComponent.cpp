#include "TaskComponent.h"

#include "DeviceActor.h"
#include "GameEventSubsystem.h"
#include "ItemActor.h"
#include "Obshaga.h"
#include "ObshagaGameMode.h"
#include "ObshagaItemData.h"
#include "ObshagaPlayerState.h"
#include "ObshagaRoundConfig.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"

UTaskComponent::UTaskComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UTaskComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Чужие задания по сети не уходят вообще — прочитать их читом нельзя.
	DOREPLIFETIME_CONDITION(UTaskComponent, Tasks, COND_OwnerOnly);
}

AObshagaPlayerState* UTaskComponent::GetOwnerState() const
{
	return Cast<AObshagaPlayerState>(GetOwner());
}

bool UTaskComponent::IsConditionImplemented(ETaskCondition Condition)
{
	switch (Condition)
	{
	case ETaskCondition::ItemInRoomAtEnd:
	case ETaskCondition::ItemNotMovedFromZone:
	case ETaskCondition::ContrabandSurvivedInspection:
	case ETaskCondition::AlibiConfirmedSuccessfully:
	case ETaskCondition::NeverSpottedWholeRound:
	case ETaskCondition::NoiseLuredKomendantAndUnseen:
	case ETaskCondition::ItemPlantedInRoomOfPlayer:
	case ETaskCondition::PlayerCaughtWithItem:
	case ETaskCondition::LeftBuildingAndReturnedUnseen:
	case ETaskCondition::InspectionFoundContrabandInRoom:
	case ETaskCondition::NoteReadByDistinctPlayers:
	case ETaskCondition::DeviceBrokenAndNotSeen:
	case ETaskCondition::DeviceRepaired:
	case ETaskCondition::CorrectAccusation:
		return true;
	default:
		return false;
	}
}

const FTaskRow* UTaskComponent::FindRowByCondition(ETaskCondition Condition) const
{
	return Rows.FindByPredicate([Condition](const FTaskRow& Row) { return Row.SuccessCondition == Condition; });
}

AItemActor* UTaskComponent::FindStolenItem(const UObject* WorldContextObject, const APlayerState* Who, const FTaskRow& Row)
{
	// «Украл» = взял нужный предмет в его «родной» комнате и вынес оттуда: держит его в руках в другой комнате
	// либо уже положил, бросил, спрятал или попался с ним за её пределами. Переложить вещь внутри комнаты — не кража.
	const UGameEventSubsystem* Bus = UGameEventSubsystem::Get(WorldContextObject);
	if (!Bus || !Who)
	{
		return nullptr;
	}

	const TArray<FGameEvent>& Events = Bus->GetEventLog();
	for (const FGameEvent& Pick : Events)
	{
		AItemActor* Item = Pick.Item;
		if (Pick.Type != EGameEventType::ItemPickedUp || Pick.Instigator != Who || !Item
			|| (!Row.ItemId.IsNone() && Item->GetItemData()->ItemId != Row.ItemId)
			|| (!Row.RoomId.IsNone() && Pick.RoomId != Row.RoomId))
		{
			continue;
		}
		if (Row.RoomId.IsNone())
		{
			return Item;
		}

		const APawn* Holder = Cast<APawn>(Item->GetHolder());
		if (Item->GetItemState() == EItemState::Carried && Holder && Holder->GetPlayerState() == Who && Item->GetCurrentRoomId() != Row.RoomId)
		{
			return Item;
		}

		const bool bCarriedOut = Events.ContainsByPredicate([&](const FGameEvent& Event)
		{
			const bool bLetGo = Event.Type == EGameEventType::ItemDropped || Event.Type == EGameEventType::ItemThrown
				|| Event.Type == EGameEventType::ItemHidden || Event.Type == EGameEventType::PlayerCaught;
			return bLetGo && Event.Instigator == Who && Event.Item == Item && Event.Time >= Pick.Time && Event.RoomId != Row.RoomId;
		});
		if (bCarriedOut)
		{
			return Item;
		}
	}
	return nullptr;
}

bool UTaskComponent::HasTask(FName TaskId) const
{
	return Tasks.ContainsByPredicate([TaskId](const FTaskState& Task) { return Task.TaskId == TaskId; });
}

void UTaskComponent::AssignTask(FName TaskId, const FTaskRow& Row, bool bMain)
{
	if (!GetOwner()->HasAuthority() || HasTask(TaskId))
	{
		return;
	}

	FTaskState& State = Tasks.AddDefaulted_GetRef();
	State.TaskId = TaskId;
	State.Title = Row.Title;
	State.Description = Row.Description;
	State.Reward = Row.GetReward();
	State.bMain = bMain;
	Rows.Add(Row);

	UE_LOG(LogObshaga, Verbose, TEXT("Task %s (%s) assigned to %s"), *TaskId.ToString(), bMain ? TEXT("main") : TEXT("side"), *GetOwnerState()->GetPlayerName());
}

void UTaskComponent::ClearTasks()
{
	if (GetOwner()->HasAuthority())
	{
		Tasks.Reset();
		Rows.Reset();
	}
}

const FTaskState* UTaskComponent::GetMainTask() const
{
	return Tasks.FindByPredicate([](const FTaskState& Task) { return Task.bMain; });
}

void UTaskComponent::UpdateLiveStatus()
{
	for (int32 Index = 0; Index < Tasks.Num(); ++Index)
	{
		if (Tasks[Index].Status == ETaskStatus::InProgress)
		{
			Tasks[Index].bSatisfiedNow = EvaluateCondition(Rows[Index]);
		}
	}
}

int32 UTaskComponent::FinalizeTasks()
{
	int32 Earned = 0;
	for (int32 Index = 0; Index < Tasks.Num(); ++Index)
	{
		FTaskState& State = Tasks[Index];
		if (State.Status != ETaskStatus::InProgress)
		{
			continue;
		}

		State.bSatisfiedNow = EvaluateCondition(Rows[Index]);
		State.Status = State.bSatisfiedNow ? ETaskStatus::Completed : ETaskStatus::Failed;
		if (State.bSatisfiedNow)
		{
			Earned += State.Reward;
		}
		UE_LOG(LogObshaga, Verbose, TEXT("Task %s of %s: %s"), *State.TaskId.ToString(), *GetOwnerState()->GetPlayerName(),
			State.bSatisfiedNow ? TEXT("COMPLETED") : TEXT("FAILED"));
	}
	return Earned;
}

bool UTaskComponent::EvaluateCondition(const FTaskRow& Row) const
{
	const AObshagaPlayerState* OwnerState = GetOwnerState();
	const UWorld* World = GetWorld();
	if (!OwnerState || !World)
	{
		return false;
	}

	// Условия «по журналу» смотрят, что этот игрок делал с начала раунда.
	const UGameEventSubsystem* Bus = UGameEventSubsystem::Get(this);
	static const TArray<FGameEvent> NoEvents;
	const TArray<FGameEvent>& Events = Bus ? Bus->GetEventLog() : NoEvents;
	auto FindOwnEvent = [&Events, OwnerState](EGameEventType Type, float AfterTime = -1.f) -> const FGameEvent*
	{
		return Events.FindByPredicate([=](const FGameEvent& Event) { return Event.Type == Type && Event.Instigator == OwnerState && Event.Time > AfterTime; });
	};
	// «Незаметно» = комендант не видел и не ловил игрока в этом промежутке времени.
	auto WasSeenBetween = [&Events, OwnerState](float From, float To)
	{
		return Events.ContainsByPredicate([=](const FGameEvent& Event)
		{
			return (Event.Type == EGameEventType::PlayerSpotted || Event.Type == EGameEventType::PlayerCaught)
				&& Event.Instigator == OwnerState && Event.Time >= From && Event.Time <= To;
		});
	};
	const AObshagaGameMode* GameMode = World->GetAuthGameMode<AObshagaGameMode>();
	const float UnseenWindow = GameMode ? GameMode->GetRoundConfig()->UnseenWindowSeconds : 10.f;

	switch (Row.SuccessCondition)
	{
	case ETaskCondition::ItemInRoomAtEnd:
	case ETaskCondition::ItemNotMovedFromZone:
	{
		// «Предмет в нужной комнате»: лежит там, спрятан там или его там держат в руках.
		const FName TargetRoom = Row.bUseOwnRoom ? OwnerState->GetHomeRoomId() : Row.RoomId;
		if (TargetRoom.IsNone())
		{
			return false;
		}
		for (TActorIterator<AItemActor> It(World); It; ++It)
		{
			if (It->GetItemData()->ItemId == Row.ItemId && It->GetCurrentRoomId() == TargetRoom)
			{
				return true;
			}
		}
		return false;
	}

	case ETaskCondition::ContrabandSurvivedInspection:
	{
		// Запрещёнка лежит в тайнике, и спрятал её именно этот игрок. Если комендант её нашёл — она уже не в тайнике.
		for (TActorIterator<AItemActor> It(World); It; ++It)
		{
			const UObshagaItemData* Data = It->GetItemData();
			const bool bMatches = Row.ItemId.IsNone() ? Data->bContraband : (Data->ItemId == Row.ItemId);
			if (bMatches && It->GetItemState() == EItemState::Hidden && It->GetLastHiddenBy() == OwnerState)
			{
				return true;
			}
		}
		return false;
	}

	case ETaskCondition::AlibiConfirmedSuccessfully:
		return FindOwnEvent(EGameEventType::AlibiConfirmed) != nullptr;

	case ETaskCondition::NeverSpottedWholeRound:
		return FindOwnEvent(EGameEventType::PlayerSpotted) == nullptr;

	case ETaskCondition::NoiseLuredKomendantAndUnseen:
	{
		// Комендант пошёл на шум этого игрока, и после этого игрока не поймали.
		// Считается последняя приманка: ранняя неудача не портит более позднюю удачу.
		const int32 LureIndex = Events.FindLastByPredicate([OwnerState](const FGameEvent& Event)
			{ return Event.Type == EGameEventType::KomendantAlerted && Event.Instigator == OwnerState; });
		const FGameEvent* Lure = Events.IsValidIndex(LureIndex) ? &Events[LureIndex] : nullptr;
		return Lure && FindOwnEvent(EGameEventType::PlayerCaught, Lure->Time) == nullptr;
	}

	case ETaskCondition::ItemPlantedInRoomOfPlayer:
	case ETaskCondition::PlayerCaughtWithItem:
	{
		// Первый путь: комендант нашёл у соседа запрещёнку, которую спрятал этот игрок.
		if (FindOwnEvent(EGameEventType::PlayerFramed))
		{
			return true;
		}
		// Второй: игрок оставил вещь в чужой комнате, и хозяина комнаты потом поймали с ней в руках.
		for (const FGameEvent& Plant : Events)
		{
			const bool bPlanted = Plant.Type == EGameEventType::ItemDropped || Plant.Type == EGameEventType::ItemHidden;
			if (!bPlanted || Plant.Instigator != OwnerState || !Plant.Item || Plant.RoomId.IsNone()
				|| Plant.RoomId == OwnerState->GetHomeRoomId())
			{
				continue;
			}
			// Годится не любая вещь: запрещёнка или предмет, названный в задании.
			const UObshagaItemData* PlantedData = Plant.Item->GetItemData();
			if (Row.ItemId.IsNone() ? !PlantedData->bContraband : PlantedData->ItemId != Row.ItemId)
			{
				continue;
			}
			for (const FGameEvent& Caught : Events)
			{
				const AObshagaPlayerState* Victim = Cast<AObshagaPlayerState>(Caught.Instigator);
				if (Caught.Type == EGameEventType::PlayerCaught && Caught.Item == Plant.Item && Caught.Time > Plant.Time
					&& Victim && Victim != OwnerState && Victim->GetHomeRoomId() == Plant.RoomId)
				{
					return true;
				}
			}
		}
		return false;
	}

	case ETaskCondition::LeftBuildingAndReturnedUnseen:
	{
		// Вышел, погулял, вернулся — и комендант не видел его ни до, ни после.
		float LeftTime = -1.f;
		for (const FGameEvent& Event : Events)
		{
			if (Event.Instigator != OwnerState)
			{
				continue;
			}
			if (Event.Type == EGameEventType::LeftBuilding)
			{
				LeftTime = Event.Time;
			}
			else if (Event.Type == EGameEventType::ReturnedToBuilding && LeftTime >= 0.f
				&& !WasSeenBetween(LeftTime - UnseenWindow, Event.Time + UnseenWindow))
			{
				return true;
			}
		}
		return false;
	}

	case ETaskCondition::InspectionFoundContrabandInRoom:
	{
		// Настучал на комнату, и после этого комендант изъял там запрещёнку.
		for (const FGameEvent& Tip : Events)
		{
			if (Tip.Type != EGameEventType::RoomTipOff || Tip.Instigator != OwnerState)
			{
				continue;
			}
			if (Events.ContainsByPredicate([&Tip](const FGameEvent& Event)
				{ return Event.Type == EGameEventType::ContrabandConfiscated && Event.RoomId == Tip.RoomId && Event.Time > Tip.Time; }))
			{
				return true;
			}
		}
		return false;
	}

	case ETaskCondition::NoteReadByDistinctPlayers:
	{
		// Игрок оставил записку, и после этого её прочитали разные люди (сам он не в счёт).
		for (const FGameEvent& Plant : Events)
		{
			const bool bPlanted = Plant.Type == EGameEventType::ItemDropped || Plant.Type == EGameEventType::ItemThrown || Plant.Type == EGameEventType::ItemHidden;
			if (!bPlanted || Plant.Instigator != OwnerState || !Plant.Item || !Plant.Item->GetItemData()->bReadable)
			{
				continue;
			}
			TSet<const APlayerState*> Readers;
			for (const FGameEvent& Read : Events)
			{
				if (Read.Type == EGameEventType::NoteRead && Read.Item == Plant.Item && Read.Time > Plant.Time && Read.Instigator && Read.Instigator != OwnerState)
				{
					Readers.Add(Read.Instigator.Get());
				}
			}
			if (Readers.Num() >= FMath::Max(Row.Count, 1))
			{
				return true;
			}
		}
		return false;
	}

	case ETaskCondition::DeviceBrokenAndNotSeen:
	case ETaskCondition::DeviceRepaired:
	{
		for (TActorIterator<ADeviceActor> It(World); It; ++It)
		{
			if (!Row.RoomId.IsNone() && It->GetRoomId() != Row.RoomId)
			{
				continue;
			}
			if (Row.SuccessCondition == ETaskCondition::DeviceRepaired)
			{
				// Починил то, что сломал не сам, и оно до сих пор работает.
				if (!It->IsBroken() && It->GetRepairedBy() == OwnerState && It->GetBrokenBy() != OwnerState)
				{
					return true;
				}
			}
			else if (It->IsBroken() && It->GetBrokenBy() == OwnerState
				&& !WasSeenBetween(It->GetBrokenTime() - UnseenWindow, It->GetBrokenTime() + UnseenWindow))
			{
				// Сломал, комендант в это время его не видел, и прибор до сих пор сломан.
				return true;
			}
		}
		return false;
	}

	case ETaskCondition::CorrectAccusation:
	{
		// Верно ли обвинение, сервер решил в момент обвинения и записал украденный предмет в событие.
		const FGameEvent* Accusation = FindOwnEvent(EGameEventType::Accusation);
		return Accusation && Accusation->Item != nullptr;
	}

	default:
		return false;
	}
}
