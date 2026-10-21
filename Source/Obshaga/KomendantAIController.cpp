#include "KomendantAIController.h"

#include "CarryComponent.h"
#include "DoorActor.h"
#include "GameEventSubsystem.h"
#include "HidingSpot.h"
#include "ItemActor.h"
#include "KomendantCharacter.h"
#include "KomendantPersonality.h"
#include "KomendantWaypoint.h"
#include "NightExitDoor.h"
#include "Obshaga.h"
#include "ObshagaCharacter.h"
#include "ObshagaGameMode.h"
#include "Components/CapsuleComponent.h"
#include "ObshagaGameState.h"
#include "ObshagaItemData.h"
#include "ObshagaPlayerController.h"
#include "ObshagaPlayerState.h"
#include "ObshagaRoundConfig.h"
#include "RoomVolume.h"
#include "SuspicionComponent.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISense_Hearing.h"
#include "Perception/AISense_Sight.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#define LOCTEXT_NAMESPACE "Komendant"

namespace
{
	// Геометрия маршрута, а не баланс: насколько далеко могут быть соседние точки и когда точка считается достигнутой.
	constexpr float MaxLinkDistance = 750.f;
	// Как часто комендант бормочет себе под нос, возвращаясь к обходу.
	constexpr float PatrolMutterChance = 0.4f;
	// Доля радиуса тела коменданта, которой проверяется проход между точками (чуть уже тела: он протискивается).
	constexpr float BodyRadiusScale = 0.8f;
	// Если комендант упёрся ближе этого расстояния от последней точки пути, он считается дошедшим.
	constexpr float StuckNearGoalDistance = 160.f;
	constexpr float ArriveDistance = 70.f;
	constexpr float ArriveHeight = 220.f;
	constexpr float DoorOpenDistance = 190.f;
	constexpr float StuckSeconds = 2.5f;
	// Сколько раз подряд по StuckSeconds он ждёт, пока жилец уйдёт с дороги (или у точки освободится место для переноса),
	// прежде чем бросить путь.
	constexpr int32 StuckBehindPawnWaits = 3;
	constexpr float DirectChaseDistance = 900.f;
	constexpr float SameFloorHeight = 150.f;
	// Обыск: с какого расстояния начинает рыться, сколько секунд подходит и дальше чего не дотянется.
	constexpr float InspectApproachDistance = 170.f;
	constexpr float InspectApproachSeconds = 4.f;
	// Нарушитель нырнул в укрытие у него на глазах: к такому тайнику он идёт дольше, в обход стен и с лестницы.
	constexpr float InspectSeenHideApproachSeconds = 12.f;
	// Идёт к тайнику по прямой и столько секунд не приближается (кровать или косяк на пути) — дальше по точкам маршрута.
	constexpr float InspectNoProgressSeconds = 1.f;
	constexpr float InspectReachDistance = 250.f;

	void NotifyPlayer(const APlayerState* TargetState, const FText& Text)
	{
		if (AObshagaPlayerController* Controller = TargetState ? Cast<AObshagaPlayerController>(TargetState->GetOwner()) : nullptr)
		{
			Controller->ClientShowNotice(Text);
		}
		else if (TargetState)
		{
			// У бота экрана нет — пишем в журнал, чтобы проверки видели сообщение.
			UE_LOG(LogObshaga, Verbose, TEXT("Notice for %s (no screen): %s"), *TargetState->GetPlayerName(), *Text.ToString());
		}
	}

	/** Имя игрока для журнала; None — никого. */
	FString KomendantLogName(const APlayerState* State)
	{
		return State ? State->GetPlayerName() : FString(TEXT("None"));
	}

	/** ItemId предмета для журнала (как в телеметрии); «-» — предмета нет. */
	FString KomendantItemId(const AItemActor* Item)
	{
		const UObshagaItemData* Data = Item ? Item->GetItemData() : nullptr;
		return Data ? Data->ItemId.ToString() : FString(TEXT("-"));
	}
}

AKomendantAIController::AKomendantAIController()
{
	PrimaryActorTick.bCanEverTick = true;

	Perception = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("Perception"));

	SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SightConfig"));
	SightConfig->DetectionByAffiliation.bDetectEnemies = true;
	SightConfig->DetectionByAffiliation.bDetectNeutrals = true;
	SightConfig->DetectionByAffiliation.bDetectFriendlies = true;
	SightConfig->SetMaxAge(1.f);

	HearingConfig = CreateDefaultSubobject<UAISenseConfig_Hearing>(TEXT("HearingConfig"));
	HearingConfig->DetectionByAffiliation.bDetectEnemies = true;
	HearingConfig->DetectionByAffiliation.bDetectNeutrals = true;
	HearingConfig->DetectionByAffiliation.bDetectFriendlies = true;

	Perception->ConfigureSense(*SightConfig);
	Perception->ConfigureSense(*HearingConfig);
	Perception->SetDominantSense(UAISense_Sight::StaticClass());
	SetPerceptionComponent(*Perception);
}

void AKomendantAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	Komendant = Cast<AKomendantCharacter>(InPawn);
	if (!Komendant)
	{
		return;
	}

	SpawnTransform = Komendant->GetActorTransform();
	Personality = Komendant->PickPersonality();
	ApplyPersonality();
	Perception->OnTargetPerceptionUpdated.AddUniqueDynamic(this, &AKomendantAIController::OnPerceptionUpdated);

	// Точки маршрута и двери должны успеть появиться в мире.
	FTimerHandle Handle;
	GetWorldTimerManager().SetTimer(Handle, this, &AKomendantAIController::BuildGraph, 0.5f, false);
}

void AKomendantAIController::ApplyPersonality()
{
	// Ночью комендант видит и слышит лучше.
	const UObshagaRoundConfig* Config = GetRoundConfig();
	const AObshagaGameState* GameState = GetWorld()->GetGameState<AObshagaGameState>();
	const bool bNight = GameState && GameState->GetRoundState() == ERoundState::InProgress && GameState->GetPhase() == ERoundPhase::Night;
	const float SightRadius = Personality->SightRadius * (bNight ? Config->NightSightMultiplier : 1.f);

	SightConfig->SightRadius = SightRadius;
	SightConfig->LoseSightRadius = SightRadius * 1.15f;
	// Выбило пробки: в темноте он видит только прямо перед собой.
	const bool bBlackout = IsNightEvent(ENightEvent::Blackout);
	SightConfig->PeripheralVisionAngleDegrees = Personality->SightHalfAngle * (bBlackout ? Config->BlackoutSightScale : 1.f);
	HearingConfig->HearingRange = Personality->HearingRange * (bNight ? Config->NightHearingMultiplier : 1.f);
	Perception->ConfigureSense(*SightConfig);
	Perception->ConfigureSense(*HearingConfig);

	// Числа личности — для автопроверки: терпение нужно проверке побега, grudge — проверке мстительного.
	UE_LOG(LogObshaga, Log, TEXT("Komendant personality: %s (night=%d), patience=%.1f, chase=%.0f, sight=%.0f/%.0f, bluff=%.2f, inspect=%.2f, grudge=%d, suspicionMult=%.2f"),
		*Personality->GetName(), bNight ? 1 : 0, Personality->PatienceSeconds, Personality->ChaseSpeed, SightRadius, SightConfig->PeripheralVisionAngleDegrees,
		Personality->BluffChance, Personality->InspectChance, Personality->bHoldsGrudge ? 1 : 0, Personality->SuspicionMultiplier);
}

void AKomendantAIController::ResetForRound()
{
	if (!Komendant)
	{
		return;
	}

	Komendant->SetActorLocationAndRotation(SpawnTransform.GetLocation(), SpawnTransform.GetRotation(), false, nullptr, ETeleportType::TeleportPhysics);
	// Ночь недели может задать манеру Палыча (рассеянный в понедельник, мстительный в пятницу).
	const AObshagaGameMode* WeekMode = GetWorld()->GetAuthGameMode<AObshagaGameMode>();
	Personality = Komendant->PickPersonality(WeekMode ? WeekMode->GetWeekKomendantStyle() : -1);
	LastPhase = 255;
	ApplyPersonality();

	RoomHeat.Reset();
	ImmuneUntil.Reset();
	LastSuspiciousEventTime.Reset();
	LastSpottedEventTime.Reset();
	InspectionQueue.Reset();
	QueuedSpot.Reset();
	InterruptedQueuedSpot.Reset();
	ChaseTarget.Reset();
	if (bGraphReady)
	{
		SetState(EKomendantState::Patrol);
	}

	// Мстительный помнит, кого ловил в прошлом раунде: они с самого начала под подозрением.
	const AObshagaGameMode* GameMode = GetWorld()->GetAuthGameMode<AObshagaGameMode>();
	UE_LOG(LogObshaga, Verbose, TEXT("Komendant grudge: personality %s, holds grudge %d, caught last round %d"), *Personality->GetName(),
		Personality->bHoldsGrudge ? 1 : 0, GameMode ? GameMode->GetCaughtLastRound().Num() : 0);
	if (GameMode && Personality->bHoldsGrudge)
	{
		for (const TWeakObjectPtr<APlayerState>& Caught : GameMode->GetCaughtLastRound())
		{
			if (const AObshagaPlayerState* TargetState = Cast<AObshagaPlayerState>(Caught.Get()))
			{
				TargetState->GetSuspicionComponent()->SetSuspicion(GetRoundConfig()->GrudgeSuspicion);
				RoomHeat.FindOrAdd(TargetState->GetHomeRoomId()) += 20.f;
				UE_LOG(LogObshaga, Verbose, TEXT("Komendant grudge on %s: suspicion %.0f, room %s heat %.0f"), *TargetState->GetPlayerName(),
					TargetState->GetSuspicionComponent()->GetSuspicion(), *TargetState->GetHomeRoomId().ToString(), RoomHeat.FindRef(TargetState->GetHomeRoomId()));
			}
		}
	}

	bDebugStuckDone = false;
	DebugStuckUntil = 0.f;
#if !UE_BUILD_SHIPPING
	// Для проверок: -TestKomendantStuck=N — раз за раунд на N-й секунде Палыч «упирается» посреди обхода (см. TickPatrol).
	FParse::Value(FCommandLine::Get(), TEXT("TestKomendantStuck="), DebugStuckAtSeconds);
#endif
}

bool AKomendantAIController::CanTakeTip() const
{
	const bool bBusy = State == EKomendantState::Chase || State == EKomendantState::Interrogate;
	return bGraphReady && Komendant && IsRoundInProgress() && !bBusy;
}

bool AKomendantAIController::InvestigateTip(const FVector& Location)
{
	if (!CanTakeTip())
	{
		return false;
	}
	StartInvestigate(Location);
	return true;
}

void AKomendantAIController::InspectRoom(FName RoomId)
{
	// Тайники этой комнаты встают в начало очереди обыска (очередь разбирается с конца).
	int32 NumQueued = 0;
	for (AHidingSpot* Spot : HidingSpots)
	{
		const ARoomVolume* Room = ARoomVolume::FindRoomAt(this, Spot->GetActorLocation() + FVector(0.f, 0.f, 50.f));
		if (Room && Room->RoomId == RoomId)
		{
			InspectionQueue.Remove(Spot);
			InspectionQueue.Add(Spot);
			++NumQueued;
		}
	}
	RoomHeat.FindOrAdd(RoomId) += 20.f;

	// Спокойный обход бросает сразу; погоню и допрос сначала заканчивает.
	if (State == EKomendantState::Patrol && !QueuedSpot.IsValid())
	{
		Path.Reset();
		WaitTime = 0.f;
	}
	UE_LOG(LogObshaga, Verbose, TEXT("Komendant tipped about room %s: %d hiding spots queued"), *RoomId.ToString(), NumQueued);
}

void AKomendantAIController::OnPhaseChanged()
{
	ApplyPersonality();

	// Утром — проверка: обойти жилые комнаты и обыскать в них все тайники.
	// Очередь не сбрасываем: в ней могут ждать комнаты, на которые настучали.
	const AObshagaGameState* GameState = GetWorld()->GetGameState<AObshagaGameState>();
	if (GameState->GetPhase() == ERoundPhase::Morning)
	{
		for (AHidingSpot* Spot : HidingSpots)
		{
			const ARoomVolume* Room = ARoomVolume::FindRoomAt(this, Spot->GetActorLocation() + FVector(0.f, 0.f, 50.f));
			// В начало списка: он разбирается с конца, и комнаты, на которые настучали, идут первыми.
			if (Room && Room->RoomType == ERoomType::Bedroom && !InspectionQueue.Contains(Spot))
			{
				InspectionQueue.Insert(Spot, 0);
			}
		}
		UE_LOG(LogObshaga, Verbose, TEXT("Komendant morning inspection: %d hiding spots"), InspectionQueue.Num());
	}
}

const UObshagaRoundConfig* AKomendantAIController::GetRoundConfig() const
{
	const AObshagaGameMode* GameMode = GetWorld()->GetAuthGameMode<AObshagaGameMode>();
	return GameMode ? GameMode->GetRoundConfig() : GetDefault<UObshagaRoundConfig>();
}

bool AKomendantAIController::IsRoundInProgress() const
{
	const AObshagaGameState* GameState = GetWorld()->GetGameState<AObshagaGameState>();
	return GameState && GameState->GetRoundState() == ERoundState::InProgress;
}

void AKomendantAIController::BuildGraph()
{
	UWorld* World = GetWorld();

	Waypoints.Reset();
	Doors.Reset();
	HidingSpots.Reset();
	for (TActorIterator<AKomendantWaypoint> It(World); It; ++It)
	{
		Waypoints.Add(*It);
	}
	for (TActorIterator<ADoorActor> It(World); It; ++It)
	{
		Doors.Add(*It);
	}
	for (TActorIterator<AHidingSpot> It(World); It; ++It)
	{
		HidingSpots.Add(*It);
	}

	// Двери, мебель, предметы и люди прямой видимости между точками не мешают.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(KomendantGraph), false);
	for (ADoorActor* Door : Doors)
	{
		Params.AddIgnoredActor(Door);
	}
	for (AHidingSpot* Spot : HidingSpots)
	{
		Params.AddIgnoredActor(Spot);
	}
	for (TActorIterator<AItemActor> It(World); It; ++It)
	{
		Params.AddIgnoredActor(*It);
	}
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		Params.AddIgnoredActor(*It);
	}

	// Для проверки прохода: мешает всё, кроме дверей (он их открывает), предметов (их уносят) и людей.
	FCollisionQueryParams BodyParams(SCENE_QUERY_STAT(KomendantBody), false);
	for (ADoorActor* Door : Doors)
	{
		BodyParams.AddIgnoredActor(Door);
	}
	for (TActorIterator<AItemActor> It(World); It; ++It)
	{
		BodyParams.AddIgnoredActor(*It);
	}
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		BodyParams.AddIgnoredActor(*It);
	}
	const float BodyRadius = Komendant ? Komendant->GetSimpleCollisionRadius() * BodyRadiusScale : 30.f;
	int32 NumBlocked = 0;

	int32 NumLinks = 0;
	for (AKomendantWaypoint* Waypoint : Waypoints)
	{
		const ARoomVolume* Room = ARoomVolume::FindRoomAt(this, Waypoint->GetActorLocation());
		Waypoint->RoomId = Room ? Room->RoomId : NAME_None;
		Waypoint->Neighbors.Reset();
	}
	for (int32 A = 0; A < Waypoints.Num(); ++A)
	{
		for (int32 B = A + 1; B < Waypoints.Num(); ++B)
		{
			const FVector LocationA = Waypoints[A]->GetActorLocation();
			const FVector LocationB = Waypoints[B]->GetActorLocation();
			// На пандус заходят только с торца: связь с перепадом высоты допустима, лишь если точки стоят на одной прямой.
			const FVector Delta = LocationB - LocationA;
			const bool bSlope = FMath::Abs(Delta.Z) > 50.f;
			const bool bAligned = FMath::Abs(Delta.X) < 60.f || FMath::Abs(Delta.Y) < 60.f;
			FHitResult Hit;
			if ((!bSlope || bAligned) && FVector::Dist(LocationA, LocationB) <= MaxLinkDistance
				&& !World->LineTraceSingleByChannel(Hit, LocationA, LocationB, ECC_Visibility, Params))
			{
				// Точки видят друг друга, но пройдёт ли между ними комендант? Луч шёл сквозь мебель и тайники,
				// и связь могла вести прямо через шкаф — там он и застревал. Проверяем проход шаром с его ширину.
				FHitResult BodyHit;
				if (World->SweepSingleByChannel(BodyHit, LocationA, LocationB, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(BodyRadius), BodyParams))
				{
					++NumBlocked;
					UE_LOG(LogObshaga, Log, TEXT("Komendant graph: link %s - %s dropped, blocked by %s at %s"), *Waypoints[A]->GetName(), *Waypoints[B]->GetName(),
						*GetNameSafe(BodyHit.GetActor()), *BodyHit.ImpactPoint.ToCompactString());
					continue;
				}
				Waypoints[A]->Neighbors.Add(Waypoints[B]);
				Waypoints[B]->Neighbors.Add(Waypoints[A]);
				++NumLinks;
			}
		}
	}

	bGraphReady = Waypoints.Num() > 0;
	UE_LOG(LogObshaga, Log, TEXT("Komendant graph: %d waypoints, %d links, %d doors, %d hiding spots"), Waypoints.Num(), NumLinks, Doors.Num(), HidingSpots.Num());

	// Самопроверка карты: все ли точки обхода достижимы друг из друга после того, как заблокированные связи убраны.
	if (!Waypoints.IsEmpty())
	{
		TSet<const AKomendantWaypoint*> Reached;
		TArray<const AKomendantWaypoint*> Queue;
		Queue.Add(Waypoints[0]);
		Reached.Add(Waypoints[0]);
		while (!Queue.IsEmpty())
		{
			const AKomendantWaypoint* Current = Queue.Pop();
			for (const TWeakObjectPtr<AKomendantWaypoint>& Neighbor : Current->Neighbors)
			{
				const AKomendantWaypoint* Next = Neighbor.Get();
				if (Next && !Reached.Contains(Next))
				{
					Reached.Add(Next);
					Queue.Add(Next);
				}
			}
		}
		for (const AKomendantWaypoint* Waypoint : Waypoints)
		{
			if (!Reached.Contains(Waypoint))
			{
				UE_LOG(LogObshaga, Warning, TEXT("Komendant graph: waypoint %s at %s is cut off from the rest"), *Waypoint->GetName(), *Waypoint->GetActorLocation().ToCompactString());
			}
		}
		UE_LOG(LogObshaga, Log, TEXT("Komendant graph check: %d links dropped as blocked, %d of %d waypoints reachable"), NumBlocked, Reached.Num(), Waypoints.Num());
	}
}

int32 AKomendantAIController::GetSpeechStyle() const
{
	return Personality ? ObshagaStory::StyleFromPersonalityName(Personality->GetName()) : 0;
}

void AKomendantAIController::SetState(EKomendantState NewState)
{
	// Обыск тайника из списка прервали до того, как он порылся, — тайник возвращается в список.
	if (AHidingSpot* Interrupted = InterruptedQueuedSpot.Get())
	{
		InspectionQueue.Add(Interrupted);
	}
	InterruptedQueuedSpot.Reset();

	// Погоня кончилась без допроса — цель больше не цель, её подозрение снова спадает.
	if (NewState != EKomendantState::Chase && NewState != EKomendantState::Interrogate)
	{
		ChaseTarget.Reset();
	}

	const EKomendantState PreviousState = State;
	State = NewState;
	StateTime = 0.f;
	Path.Reset();
	PathIndex = 0;
	WaitTime = 0.f;
	SearchTime = 0.f;
	bInspectSeenHide = false;
	bInspectViaPath = false;
	InspectBestDistance = TNumericLimits<float>::Max();
	InspectNoProgressTime = 0.f;
	LookBaseYaw = Komendant->GetActorRotation().Yaw;

	EKomendantAlert Alert = EKomendantAlert::Calm;
	if (NewState == EKomendantState::Chase || NewState == EKomendantState::Interrogate)
	{
		Alert = EKomendantAlert::Chasing;
	}
	else if (NewState == EKomendantState::Investigate || NewState == EKomendantState::Inspect || NewState == EKomendantState::Bluff)
	{
		// Ложная тревога снаружи выглядит так же, как настоящая.
		Alert = EKomendantAlert::Suspicious;
	}
	Komendant->SetAlert(Alert);

	// Комендант комментирует, что делает: по реплике игроки за стеной понимают, что происходит.
	switch (NewState)
	{
	case EKomendantState::Investigate:
		// Оторвавшийся нарушитель уводит его осмотреться на последнем месте: это «ушёл», а не «что-то слышал».
		Komendant->Say(PreviousState == EKomendantState::Chase ? EKomendantLine::Lost : EKomendantLine::Heard, GetSpeechStyle(), PreviousState == EKomendantState::Chase);
		break;
	case EKomendantState::Bluff:
		Komendant->Say(EKomendantLine::Bluff, GetSpeechStyle());
		break;
	case EKomendantState::Chase:
		Komendant->Say(EKomendantLine::Chase, GetSpeechStyle(), true);
		break;
	case EKomendantState::Inspect:
	{
		// Про «утреннюю проверку» он говорит только утром; в остальное время это просто осмотр тайника.
		const AObshagaGameState* GameState = GetWorld()->GetGameState<AObshagaGameState>();
		const bool bMorning = GameState && GameState->GetPhase() == ERoundPhase::Morning;
		Komendant->Say(bMorning ? EKomendantLine::Inspect : EKomendantLine::Search, GetSpeechStyle());
		break;
	}
	case EKomendantState::Patrol:
		if (PreviousState == EKomendantState::Chase)
		{
			// Погоня кончилась ничем.
			Komendant->Say(EKomendantLine::Lost, GetSpeechStyle(), true);
		}
		else if (FMath::FRand() < PatrolMutterChance)
		{
			Komendant->Say(EKomendantLine::Patrol, GetSpeechStyle());
		}
		break;
	default:
		break;
	}

	UE_LOG(LogObshaga, Verbose, TEXT("Komendant state: %s"), *UEnum::GetValueAsString(NewState));
}

void AKomendantAIController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bGraphReady || !Komendant)
	{
		return;
	}

	// Конус зрения смотрит туда же, куда повёрнуто тело.
	SetControlRotation(Komendant->GetActorRotation());
	StateTime += DeltaSeconds;

	if (!IsRoundInProgress())
	{
		// До старта и после конца раунда комендант просто стоит.
		Komendant->SetAlert(EKomendantAlert::Calm);
		return;
	}

	SeenPlayers.Reset();
	TArray<AActor*> Perceived;
	Perception->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), Perceived);
	for (AActor* Actor : Perceived)
	{
		AObshagaCharacter* Player = Cast<AObshagaCharacter>(Actor);
		// Спрятавшихся и выселенных призраков комендант не видит.
		const AObshagaPlayerState* SeenState = Player ? Player->GetPlayerState<AObshagaPlayerState>() : nullptr;
		if (Player && !Player->IsHiding() && !Player->IsGhost() && !(SeenState && SeenState->IsEvicted()))
		{
			SeenPlayers.Add(Player);
		}
	}

	UpdateVision(DeltaSeconds);

	SlowTimer += DeltaSeconds;
	if (SlowTimer >= 1.f)
	{
		SlowTimer -= 1.f;
		UpdateSlowTimers();
	}

	DoorTimer += DeltaSeconds;
	if (DoorTimer >= 0.25f)
	{
		DoorTimer = 0.f;
		OpenDoorsNearby();
	}

	switch (State)
	{
	case EKomendantState::Patrol:
		TickPatrol(DeltaSeconds);
		break;
	case EKomendantState::Investigate:
		TickInvestigate(DeltaSeconds);
		break;
	case EKomendantState::Chase:
		TickChase(DeltaSeconds);
		break;
	case EKomendantState::Inspect:
		TickInspect(DeltaSeconds);
		break;
	case EKomendantState::Bluff:
		TickBluff(DeltaSeconds);
		break;
	case EKomendantState::Interrogate:
		TickInterrogate(DeltaSeconds);
		break;
	}
}

void AKomendantAIController::UpdateVision(float DeltaSeconds)
{
	const UObshagaRoundConfig* Config = GetRoundConfig();
	const float Now = GetWorld()->GetTimeSeconds();

	// «Заметил» записываем всегда, даже на допросе и про тех, кого сейчас не трогаем:
	// задания «сделай незаметно» смотрят именно на эти записи. Не чаще раза в несколько секунд на игрока.
	for (AObshagaCharacter* Player : SeenPlayers)
	{
		float& LastEvent = LastSpottedEventTime.FindOrAdd(Player, -100.f);
		if (Now - LastEvent > 5.f)
		{
			LastEvent = Now;
			UGameEventSubsystem::PublishFrom(Player, EGameEventType::PlayerSpotted);
		}
	}

	if (State == EKomendantState::Interrogate)
	{
		return;
	}

	for (AObshagaCharacter* Player : SeenPlayers)
	{
		AObshagaPlayerState* TargetState = Player->GetPlayerState<AObshagaPlayerState>();
		if (!TargetState || Player->IsFrozen() || ImmuneUntil.FindRef(Player) > Now)
		{
			continue;
		}

		// Подозрительно всё, что несут в руках: запрещёнка — сразу, тяжёлое — быстро, мелочь — понемногу.
		float Rate = 0.f;
		if (const AItemActor* Item = Player->GetCarryComponent()->GetCarriedItem())
		{
			const UObshagaItemData* Data = Item->GetItemData();
			Rate = Data->bContraband ? Config->ContrabandSuspicionPerSecond
				: (Data->bHeavy ? Config->HeavyCarrySuspicionPerSecond : Config->CarrySuspicionPerSecond);
		}

		// Ночью подозрительно уже то, что ты не у себя и не в туалете.
		const ARoomVolume* Room = Player->GetCurrentRoom();
		const AObshagaGameState* GameState = GetWorld()->GetGameState<AObshagaGameState>();
		const bool bCurfew = GameState->GetPhase() == ERoundPhase::Night && Room && Room->bForbiddenAfterCurfew;
		if (bCurfew)
		{
			Rate += Config->CurfewSuspicionPerSecond;
		}

		USuspicionComponent* Suspicion = TargetState->GetSuspicionComponent();
		if (Rate > 0.f)
		{
			Suspicion->AddSuspicion(Rate * Personality->SuspicionMultiplier * DeltaSeconds);
			// «Заметил за подозрительным» — отдельная запись: задание «не попадись на глаза» и титул «Святой»
			// смотрят на неё, а не на то, что Палыч просто увидел жильца у себя в комнате.
			float& LastSuspicious = LastSuspiciousEventTime.FindOrAdd(Player, -100.f);
			if (Now - LastSuspicious > 5.f)
			{
				LastSuspicious = Now;
				UGameEventSubsystem::PublishFrom(Player, EGameEventType::PlayerSpottedSuspicious);
				// Для автопроверки: за что растёт подозрение — ноша, отбой в запретной зоне или и то и другое.
				const AItemActor* Carried = Player->GetCarryComponent()->GetCarriedItem();
				const UObshagaItemData* CarriedData = Carried ? Carried->GetItemData() : nullptr;
				UE_LOG(LogObshaga, Verbose, TEXT("Komendant sees %s suspicious in %s: carry=%s curfew=%d rate=%.1f suspicion=%.0f, t=%.1f"), *TargetState->GetPlayerName(),
					Room ? *Room->RoomId.ToString() : TEXT("None"),
					!CarriedData ? TEXT("none") : (CarriedData->bContraband ? TEXT("contraband") : (CarriedData->bHeavy ? TEXT("heavy") : TEXT("normal"))),
					bCurfew ? 1 : 0, Rate * Personality->SuspicionMultiplier, Suspicion->GetSuspicion(), GetRoundSeconds());
			}
		}

		if (Suspicion->GetSuspicion() >= 100.f && State != EKomendantState::Chase)
		{
			StartChase(Player);
		}
	}
}

void AKomendantAIController::UpdateSlowTimers()
{
	const UObshagaRoundConfig* Config = GetRoundConfig();

	const uint8 Phase = static_cast<uint8>(GetWorld()->GetGameState<AObshagaGameState>()->GetPhase());
	if (Phase != LastPhase)
	{
		LastPhase = Phase;
		OnPhaseChanged();
	}

	// Подозрение понемногу спадает у тех, кого комендант сейчас не видит.
	const AObshagaGameState* GameState = GetWorld()->GetGameState<AObshagaGameState>();
	for (APlayerState* Player : GameState->PlayerArray)
	{
		const AObshagaPlayerState* TargetState = Cast<AObshagaPlayerState>(Player);
		const AObshagaCharacter* PlayerCharacter = TargetState ? Cast<AObshagaCharacter>(TargetState->GetPawn()) : nullptr;
		if (TargetState && !SeenPlayers.Contains(PlayerCharacter) && ChaseTarget.Get() != PlayerCharacter)
		{
			TargetState->GetSuspicionComponent()->AddSuspicion(-Config->SuspicionDecayPerSecond);
		}
	}

	for (TPair<FName, float>& Heat : RoomHeat)
	{
		Heat.Value *= 0.97f;
	}
}

void AKomendantAIController::OnPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
	if (!bGraphReady || !IsRoundInProgress() || !Stimulus.WasSuccessfullySensed()
		|| Stimulus.Type != UAISense::GetSenseID<UAISense_Hearing>())
	{
		return;
	}

	const ARoomVolume* NoiseRoom = ARoomVolume::FindRoomAt(this, Stimulus.StimulusLocation);
	// Для автопроверки: чей шум он услышал и пошёл ли смотреть (задание «ложная тревога», титул «Паникёр»).
	const AObshagaCharacter* NoiseMaker = Cast<AObshagaCharacter>(Actor);
	const FString HeardWho = NoiseMaker ? KomendantLogName(NoiseMaker->GetPlayerState()) : GetNameSafe(Actor);
	const float HeardDistance = Komendant ? FVector::Dist(Stimulus.StimulusLocation, Komendant->GetActorLocation()) : 0.f;
	// День рождения: в комнате именинника шуметь можно — комендант знает, что там празднуют.
	if (NoiseRoom && IsNightEvent(ENightEvent::Birthday) && NoiseRoom->RoomId == GetRoundConfig()->BirthdayRoomId)
	{
		UE_LOG(LogObshaga, Verbose, TEXT("Komendant heard %s: loudness %.2f at %s, %.0f cm away, investigating 0 (birthday), t=%.1f"), *HeardWho, Stimulus.Strength,
			*Stimulus.StimulusLocation.ToCompactString(), HeardDistance, GetRoundSeconds());
		return;
	}
	if (NoiseRoom)
	{
		RoomHeat.FindOrAdd(NoiseRoom->RoomId) += Stimulus.Strength * 10.f;
	}

	// Тихий шум (скрип двери) только «греет» комнату; на громкий он идёт смотреть.
	const bool bBusy = State == EKomendantState::Chase || State == EKomendantState::Interrogate;
	const bool bGoLook = !bBusy && Stimulus.Strength >= GetRoundConfig()->KomendantMinNoise;
	UE_LOG(LogObshaga, Verbose, TEXT("Komendant heard %s: loudness %.2f at %s, %.0f cm away, investigating %d (%s), t=%.1f"), *HeardWho, Stimulus.Strength,
		*Stimulus.StimulusLocation.ToCompactString(), HeardDistance, bGoLook ? 1 : 0, bBusy ? TEXT("busy") : (bGoLook ? TEXT("loud") : TEXT("too quiet")), GetRoundSeconds());
	if (bGoLook)
	{
		StartInvestigate(Stimulus.StimulusLocation);
		if (AObshagaCharacter* Culprit = Cast<AObshagaCharacter>(Actor))
		{
			UGameEventSubsystem::PublishFrom(Culprit, EGameEventType::KomendantAlerted);
		}
	}
}

void AKomendantAIController::StartInvestigate(const FVector& Location)
{
	SetState(EKomendantState::Investigate);
	InvestigateLocation = Location;
	bInvestigateArrived = !PlanPathTo(Location);
	InvestigateLookTime = 0.f;
	InvestigateHoldSeconds = GetRoundConfig()->InvestigateSeconds;
}

bool AKomendantAIController::HoldAt(const FVector& Location, float Seconds)
{
	if (!CanTakeTip())
	{
		return false;
	}
	StartInvestigate(Location);
	InvestigateHoldSeconds = Seconds;
	return true;
}

bool AKomendantAIController::IsNightEvent(ENightEvent Event) const
{
	const AObshagaGameState* GameState = GetWorld()->GetGameState<AObshagaGameState>();
	return GameState && GameState->IsNightEventActive() && GameState->GetNightEvent() == Event;
}

bool AKomendantAIController::IsDeaneryMorning() const
{
	const AObshagaGameState* GameState = GetWorld()->GetGameState<AObshagaGameState>();
	return IsNightEvent(ENightEvent::Deanery) && GameState->GetPhase() == ERoundPhase::Morning;
}

void AKomendantAIController::StartChase(AObshagaCharacter* Target)
{
	SetState(EKomendantState::Chase);
	ChaseTarget = Target;
	LastKnownLocation = Target->GetActorLocation();
	LastSeenTime = GetWorld()->GetTimeSeconds();
	ReplanTime = 0.f;

	// Для проверок по журналу: кто, с чем и откуда; конец погони пишет LogChaseEnd.
	ChaseStartTime = LastSeenTime;
	ChaseRun = 0.f;
	ChaseSeen = 0.f;
	ChaseRoomChanges = 0;
	ChaseStuck = 0;
	ChaseStartRoom = Target->GetCurrentRoom() ? Target->GetCurrentRoom()->RoomId : NAME_None;
	ChaseLastRoom = ChaseStartRoom;
	ChasePrevLocation = Komendant->GetActorLocation();
	const AItemActor* Item = Target->GetCarryComponent()->GetCarriedItem();
	const AObshagaPlayerState* ChasedState = Target->GetPlayerState<AObshagaPlayerState>();
	UE_LOG(LogObshaga, Verbose, TEXT("Komendant chase start: %s (%s), item=%s (%s), room=%s, z=%.0f, dist=%.0f, suspicion=%.0f, t=%.1f"),
		*KomendantLogName(Target->GetPlayerState()), *Target->GetName(), *GetNameSafe(Item), *KomendantItemId(Item),
		*ChaseStartRoom.ToString(), Target->GetActorLocation().Z, FVector::Dist(Target->GetActorLocation(), Komendant->GetActorLocation()),
		ChasedState ? ChasedState->GetSuspicionComponent()->GetSuspicion() : 0.f, GetRoundSeconds());
	NotifyPlayer(Target->GetPlayerState(), LOCTEXT("ChaseNotice", "Комендант идёт за тобой!"));
}

void AKomendantAIController::TickPatrol(float DeltaSeconds)
{
	if (WaitTime > 0.f)
	{
		WaitTime -= DeltaSeconds;
		return;
	}

	// Обыск по списку важнее обычного обхода. Если по дороге к тайнику отвлекли (шум, погоня),
	// сначала возвращается к нему же, и только потом берёт следующий.
	if (Path.IsEmpty() && (QueuedSpot.IsValid() || !InspectionQueue.IsEmpty()))
	{
		if (!QueuedSpot.IsValid())
		{
			QueuedSpot = InspectionQueue.Pop();
			if (const AHidingSpot* Next = QueuedSpot.Get())
			{
				const ARoomVolume* NextRoom = ARoomVolume::FindRoomAt(this, Next->GetActorLocation() + FVector(0.f, 0.f, 50.f));
				UE_LOG(LogObshaga, Verbose, TEXT("Komendant inspection next: %s (room %s), %d left"), *Next->GetName(),
					NextRoom ? *NextRoom->RoomId.ToString() : TEXT("None"), InspectionQueue.Num());
			}
		}
		if (!QueuedSpot.IsValid() || !PlanPathTo(GetSpotStandLocation(QueuedSpot.Get()), true))
		{
			QueuedSpot.Reset();
		}
		return;
	}

	if (Path.IsEmpty())
	{
		const AKomendantWaypoint* Target = PickPatrolTarget();
		if (!Target || !PlanPathTo(Target->GetActorLocation()))
		{
			WaitTime = 1.f;
		}
		else
		{
			// Для автопроверки «жара»: куда пошёл и насколько горячо там было.
			UE_LOG(LogObshaga, Verbose, TEXT("Komendant patrol to %s (room %s, heat %.0f), t=%.1f"), *Target->GetName(), *Target->RoomId.ToString(),
				RoomHeat.FindRef(Target->RoomId), GetRoundSeconds());
		}
		return;
	}

#if !UE_BUILD_SHIPPING
	// -TestKomendantStuck=N: раз за раунд посреди обычного обхода он «упирается» (MoveToward стоит на месте),
	// чтобы сработала страховка FollowPath — перенос на точку. Последнюю точку пути не берём: там упор считается приходом.
	if (!bDebugStuckDone && DebugStuckAtSeconds > 0.f && !QueuedSpot.IsValid() && PathIndex < Path.Num() - 1 && GetRoundSeconds() >= DebugStuckAtSeconds)
	{
		bDebugStuckDone = true;
		DebugStuckUntil = GetWorld()->GetTimeSeconds() + StuckSeconds + 1.5f;
		UE_LOG(LogObshaga, Log, TEXT("Komendant test stuck: holding still on the way to %s (point %d of %d), t=%.1f"),
			*Path[PathIndex].ToCompactString(), PathIndex + 1, Path.Num(), GetRoundSeconds());
	}
#endif

	// К тайнику не идём вплотную: его точка лежит внутри самой мебели, комендант упирался в неё и «застревал».
	// Подошёл на расстояние вытянутой руки и видит тайник — значит, пришёл.
	bool bAtSpot = false;
	if (const AHidingSpot* Queued = QueuedSpot.Get())
	{
		const FVector SpotLocation = GetSpotStandLocation(Queued);
		const FVector ToSpot = SpotLocation - Komendant->GetActorLocation();
		bAtSpot = ToSpot.Size2D() < InspectReachDistance && FMath::Abs(ToSpot.Z) < SameFloorHeight && HasClearLine(Komendant->GetActorLocation(), SpotLocation);
	}
	const float DeanerySpeed = IsDeaneryMorning() ? GetRoundConfig()->DeanerySpeedScale : 1.f;
	if (!bAtSpot && !FollowPath(Personality->PatrolSpeed * DeanerySpeed, DeltaSeconds))
	{
		return;
	}

	// Дошёл до точки обхода: ложная тревога, обыск тайника или короткая пауза.
	Path.Reset();
	if (AHidingSpot* Queued = QueuedSpot.Get())
	{
		QueuedSpot.Reset();
		SetState(EKomendantState::Inspect);
		InspectSpot = Queued;
		InterruptedQueuedSpot = Queued;
		return;
	}
	if (FMath::FRand() < Personality->BluffChance)
	{
		SetState(EKomendantState::Bluff);
		BluffDuration = FMath::FRandRange(2.f, 4.f);
	}
	else if (AHidingSpot* Spot = (FMath::FRand() < Personality->InspectChance) ? PickSpotNear(Komendant->GetActorLocation()) : nullptr)
	{
		SetState(EKomendantState::Inspect);
		InspectSpot = Spot;
	}
	else
	{
		WaitTime = FMath::FRandRange(0.5f, 2.f);
	}
}

void AKomendantAIController::TickInvestigate(float DeltaSeconds)
{
	if (!bInvestigateArrived)
	{
		bInvestigateArrived = FollowPath(Personality->PatrolSpeed * 1.4f, DeltaSeconds);
		return;
	}

	LookAround();
	InvestigateLookTime += DeltaSeconds;
	if (InvestigateLookTime >= InvestigateHoldSeconds)
	{
		SetState(EKomendantState::Patrol);
	}
}

void AKomendantAIController::TickBluff(float DeltaSeconds)
{
	LookAround();
	if (StateTime >= BluffDuration)
	{
		SetState(EKomendantState::Patrol);
	}
}

void AKomendantAIController::TickChase(float DeltaSeconds)
{
	AObshagaCharacter* Target = ChaseTarget.Get();
	const float Now = GetWorld()->GetTimeSeconds();
	if (!Target)
	{
		LogChaseEnd(nullptr, TEXT("target_gone"), nullptr, nullptr);
		SetState(EKomendantState::Patrol);
		return;
	}

	// Для журнала: сколько пробежал, сколько секунд видел цель и через сколько комнат она уходила.
	ChaseRun += FVector::Dist2D(Komendant->GetActorLocation(), ChasePrevLocation);
	ChasePrevLocation = Komendant->GetActorLocation();
	if (SeenPlayers.Contains(Target))
	{
		ChaseSeen += DeltaSeconds;
	}
	if (const ARoomVolume* TargetRoom = Target->GetCurrentRoom())
	{
		if (TargetRoom->RoomId != ChaseLastRoom)
		{
			ChaseLastRoom = TargetRoom->RoomId;
			++ChaseRoomChanges;
		}
	}

	if (Target->IsHiding())
	{
		// Видел, как нарушитель залез в укрытие, — идёт открывать. Не видел — ищет на месте.
		if (Now - LastSeenTime < 1.f && Target->GetHidingSpot())
		{
			AHidingSpot* Spot = Target->GetHidingSpot();
			LogChaseEnd(Target, TEXT("hid_seen"), Spot, Target->GetCarryComponent()->GetCarriedItem());
			SetState(EKomendantState::Inspect);
			InspectSpot = Spot;
			bInspectSeenHide = true;
		}
		else
		{
			// Не видел, куда тот делся, — это такой же удачный побег, как если бы нарушитель просто убежал.
			if (const AObshagaPlayerState* TargetState = Target->GetPlayerState<AObshagaPlayerState>())
			{
				TargetState->GetSuspicionComponent()->SetSuspicion(GetRoundConfig()->SuspicionAfterStrike);
				NotifyPlayer(TargetState, LOCTEXT("LostHiddenNotice", "Палыч тебя потерял — посиди тихо"));
			}
			LogChaseEnd(Target, TEXT("hid_unseen"), Target->GetHidingSpot(), Target->GetCarryComponent()->GetCarriedItem());
			StartInvestigate(LastKnownLocation);
		}
		return;
	}

	const bool bSeen = SeenPlayers.Contains(Target);
	if (bSeen)
	{
		LastKnownLocation = Target->GetActorLocation();
		LastSeenTime = Now;
	}
	else if (Now - LastSeenTime > Personality->PatienceSeconds)
	{
		// Потерял из виду и не дождался: нарушитель ушёл.
		if (const AObshagaPlayerState* TargetState = Target->GetPlayerState<AObshagaPlayerState>())
		{
			TargetState->GetSuspicionComponent()->SetSuspicion(GetRoundConfig()->SuspicionAfterStrike);
			NotifyPlayer(TargetState, LOCTEXT("EscapedNotice", "Оторвался! Комендант тебя потерял"));
		}
		LogChaseEnd(Target, TEXT("lost_sight"), nullptr, Target->GetCarryComponent()->GetCarriedItem());
		StartInvestigate(LastKnownLocation);
		return;
	}

	const FVector ToTarget = LastKnownLocation - Komendant->GetActorLocation();
	const bool bSameFloor = FMath::Abs(ToTarget.Z) < SameFloorHeight;
	if (bSeen && bSameFloor && ToTarget.Size2D() <= GetRoundConfig()->KomendantCatchDistance)
	{
		// Ношу запоминаем до допроса: там её отбирают или роняют.
		const AItemActor* CaughtWith = Target->GetCarryComponent()->GetCarriedItem();
		AObshagaGameMode* GameMode = GetWorld()->GetAuthGameMode<AObshagaGameMode>();
		if (GameMode && GameMode->StartInterrogation(Target, GetEvidenceLocation()))
		{
			LogChaseEnd(Target, TEXT("caught"), nullptr, CaughtWith);
			SetState(EKomendantState::Interrogate);
			ChaseTarget = Target;
		}
		else
		{
			LogChaseEnd(Target, TEXT("refused"), nullptr, CaughtWith);
			SetState(EKomendantState::Patrol);
		}
		return;
	}

	if (bSeen && bSameFloor && ToTarget.Size2D() <= DirectChaseDistance)
	{
		// Видит цель рядом — бежит по прямой.
		Path.Reset();
		MoveToward(LastKnownLocation, Personality->ChaseSpeed);
		return;
	}

	ReplanTime -= DeltaSeconds;
	if (Path.IsEmpty() || ReplanTime <= 0.f)
	{
		PlanPathTo(LastKnownLocation);
		ReplanTime = 1.f;
	}
	if (FollowPath(Personality->ChaseSpeed, DeltaSeconds))
	{
		MoveToward(LastKnownLocation, Personality->ChaseSpeed);
	}
}

void AKomendantAIController::TickInspect(float DeltaSeconds)
{
	AHidingSpot* Spot = InspectSpot.Get();
	if (!Spot)
	{
		SetState(EKomendantState::Patrol);
		return;
	}

	// Сначала подходит к тайнику (не дольше нескольких секунд), потом роется.
	const FVector SpotLocation = GetSpotStandLocation(Spot);
	const FVector ToSpot = SpotLocation - Komendant->GetActorLocation();
	const float Distance = ToSpot.Size2D();
	const bool bClear = SearchTime <= 0.f && FMath::Abs(ToSpot.Z) <= SameFloorHeight && HasClearLine(Komendant->GetActorLocation(), SpotLocation);
	const float ApproachSeconds = bInspectSeenHide ? InspectSeenHideApproachSeconds : InspectApproachSeconds;
	bool bApproach = SearchTime <= 0.f && (Distance > InspectApproachDistance || !bClear) && StateTime < ApproachSeconds;
	// Идёт по прямой: тайник на виду или путь по точкам уже пройден.
	if (bApproach && (!bInspectViaPath || !Path.IsValidIndex(PathIndex)))
	{
		if (Distance < InspectBestDistance - 15.f)
		{
			InspectBestDistance = Distance;
			InspectNoProgressTime = 0.f;
		}
		else
		{
			InspectNoProgressTime += DeltaSeconds;
		}
		// Упёрся, но рукой уже дотягивается — роется отсюда, как и раньше. Упёрся уже после пути по точкам —
		// обходить больше нечем: бросает тайник сразу, а не толкается в мебель до конца отведённых секунд.
		if (InspectNoProgressTime >= InspectNoProgressSeconds && ((bClear && Distance <= InspectReachDistance) || bInspectViaPath))
		{
			bApproach = false;
		}
	}
	if (bApproach)
	{
		const float Speed = Personality->PatrolSpeed * 1.4f * (IsDeaneryMorning() ? GetRoundConfig()->DeanerySpeedScale : 1.f);
		if (!bInspectViaPath)
		{
			// Тайник на виду — по прямой.
			if (bClear && InspectNoProgressTime < InspectNoProgressSeconds)
			{
				MoveToward(SpotLocation, Speed);
				return;
			}
			// Тайник за стеной или на другом этаже (нарушитель залез в шкаф, пока Палыч был на лестнице) или прямо не пройти —
			// по точкам маршрута до места, откуда тайник видно, а там по прямой. Близко по карте ещё не значит «рядом».
			// Путь строится один раз: тайник не двигается, а перестройка каждую секунду снова начинала бы его
			// с ближайшей точки — часто уже пройденной, и Палыч ходил бы туда-обратно по лестнице.
			bInspectViaPath = true;
			InspectBestDistance = TNumericLimits<float>::Max();
			InspectNoProgressTime = 0.f;
			PlanPathTo(SpotLocation, true, true);
		}
		if (FollowPath(Speed, DeltaSeconds))
		{
			MoveToward(SpotLocation, Speed);
		}
		return;
	}

	// Сквозь стены и с другого этажа не обыскивает: не дошёл — бросает этот тайник.
	if (SearchTime <= 0.f && (Distance > InspectReachDistance || !bClear))
	{
		// Для автопроверки: почему не дошёл — далеко, другой этаж или стена (clear=0) и сколько шёл.
		UE_LOG(LogObshaga, Verbose, TEXT("Komendant could not reach %s, skipping: dist=%.0f dz=%.0f clear=%d, approach %.1f of %.1f s, seen hide %d, t=%.1f"),
			*Spot->GetName(), Distance, ToSpot.Z, bClear ? 1 : 0, StateTime, ApproachSeconds, bInspectSeenHide ? 1 : 0, GetRoundSeconds());
		InterruptedQueuedSpot.Reset();
		SetState(EKomendantState::Patrol);
		return;
	}

	FaceLocation(SpotLocation);
	SearchTime += DeltaSeconds;
	if (SearchTime >= GetRoundConfig()->InspectSeconds * (IsDeaneryMorning() ? GetRoundConfig()->DeaneryInspectScale : 1.f))
	{
		SearchSpot(Spot);
	}
}

void AKomendantAIController::SearchSpot(AHidingSpot* Spot)
{
	const UObshagaRoundConfig* Config = GetRoundConfig();

	// Тайник из утреннего списка или случайный (для проверки «утренняя проверка нашла запрещёнку»).
	const bool bQueued = InterruptedQueuedSpot.Get() == Spot;
	InterruptedQueuedSpot.Reset();
	AObshagaCharacter* FoundPlayer = nullptr;
	AItemActor* Contraband = Spot->KomendantSearch(FoundPlayer);
	{
		const ARoomVolume* SpotRoom = ARoomVolume::FindRoomAt(this, Spot->GetActorLocation() + FVector(0.f, 0.f, 50.f));
		const AObshagaGameState* SearchState = GetWorld()->GetGameState<AObshagaGameState>();
		UE_LOG(LogObshaga, Verbose, TEXT("Komendant searched %s: contraband=%s player=%s, room=%s, phase=%s, queued=%d, t=%.1f"), *Spot->GetName(),
			*GetNameSafe(Contraband), FoundPlayer ? *KomendantLogName(FoundPlayer->GetPlayerState()) : TEXT("None"),
			SpotRoom ? *SpotRoom->RoomId.ToString() : TEXT("None"), SearchState ? *UEnum::GetValueAsString(SearchState->GetPhase()) : TEXT("-"),
			bQueued ? 1 : 0, GetRoundSeconds());
	}

	if (Contraband)
	{
		// Запрещёнку уносит на вахту. Кто прятал, комендант не знает: в жилой комнате виноваты её жильцы.
		APlayerState* Hider = Contraband->GetLastHiddenBy();
		Contraband->ReleaseToWorld(GetEvidenceLocation(), FVector::ZeroVector);
		Contraband->ForgetCarrier();

		const ARoomVolume* Room = ARoomVolume::FindRoomAt(this, Spot->GetActorLocation() + FVector(0.f, 0.f, 50.f));
		const FName RoomId = Room ? Room->RoomId : NAME_None;
		UGameEventSubsystem* Bus = UGameEventSubsystem::Get(this);
		if (Bus)
		{
			FGameEvent Event;
			Event.Type = EGameEventType::ContrabandConfiscated;
			Event.Instigator = Hider;
			Event.Item = Contraband;
			Event.RoomId = RoomId;
			Bus->Publish(Event);
		}

		TArray<AObshagaPlayerState*> Residents;
		if (Room && Room->RoomType == ERoomType::Bedroom)
		{
			for (APlayerState* Player : GetWorld()->GetGameState<AObshagaGameState>()->PlayerArray)
			{
				AObshagaPlayerState* Resident = Cast<AObshagaPlayerState>(Player);
				if (Resident && Resident->GetHomeRoomId() == RoomId && !Resident->IsEvicted())
				{
					Residents.Add(Resident);
				}
			}
		}

		AObshagaPlayerState* HiderState = Cast<AObshagaPlayerState>(Hider);
		// Прятал у себя (даже если его уже выселили) или сам попался в этом же тайнике — отвечает он.
		const bool bHiderLivesHere = HiderState && HiderState->GetHomeRoomId() == RoomId;
		const bool bCaughtInside = HiderState && FoundPlayer && FoundPlayer->GetPlayerState() == HiderState;
		{
			// Для автопроверки: на кого записали находку — на прятавшего или на жильцов комнаты (подстава).
			const bool bFramed = !(Residents.IsEmpty() || bHiderLivesHere || bCaughtInside);
			FString ResidentNames;
			for (const AObshagaPlayerState* Resident : Residents)
			{
				ResidentNames += (ResidentNames.IsEmpty() ? TEXT("") : TEXT("+")) + Resident->GetPlayerName();
			}
			UE_LOG(LogObshaga, Verbose, TEXT("Contraband %s in %s (room %s): hider %s, residents %s, blamed %s, framed %d"), *GetNameSafe(Contraband),
				*Spot->GetName(), *RoomId.ToString(), *KomendantLogName(Hider), ResidentNames.IsEmpty() ? TEXT("-") : *ResidentNames,
				bFramed ? (ResidentNames.IsEmpty() ? TEXT("-") : *ResidentNames) : *KomendantLogName(Hider), bFramed ? 1 : 0);
		}
		if (Residents.IsEmpty() || bHiderLivesHere || bCaughtInside)
		{
			// Общая комната или своя собственная: отвечает тот, кто прятал.
			if (HiderState)
			{
				HiderState->GetSuspicionComponent()->AddSuspicion(Config->HiddenContrabandSuspicion);
				NotifyPlayer(HiderState, LOCTEXT("ConfiscatedNotice", "Комендант нашёл твою запрещёнку!"));
			}
		}
		else
		{
			// Подбросили в чужую комнату: попадает жильцам, а не тому, кто прятал.
			for (AObshagaPlayerState* Resident : Residents)
			{
				Resident->GetSuspicionComponent()->AddSuspicion(Config->HiddenContrabandSuspicion);
				NotifyPlayer(Resident, LOCTEXT("FramedNotice", "Комендант нашёл запрещёнку в твоей комнате! Но ты её туда не клал..."));
				if (Bus && HiderState)
				{
					FGameEvent Event;
					Event.Type = EGameEventType::PlayerFramed;
					Event.Instigator = HiderState;
					Event.Target = Resident;
					Event.Item = Contraband;
					Event.RoomId = RoomId;
					Bus->Publish(Event);
				}
			}
			NotifyPlayer(HiderState, LOCTEXT("FrameWorked", "Подстава удалась: комендант нашёл твой «подарок»"));
		}
	}

	if (FoundPlayer)
	{
		// Вытащил из укрытия — сразу ловит.
		if (const AObshagaPlayerState* FoundState = FoundPlayer->GetPlayerState<AObshagaPlayerState>())
		{
			FoundState->GetSuspicionComponent()->SetSuspicion(100.f);
		}
		ImmuneUntil.Remove(FoundPlayer);
		StartChase(FoundPlayer);
		return;
	}

	SetState(EKomendantState::Patrol);
}

void AKomendantAIController::TickInterrogate(float DeltaSeconds)
{
	if (const AObshagaCharacter* Target = ChaseTarget.Get())
	{
		FaceLocation(Target->GetActorLocation());
	}

	const AObshagaGameState* GameState = GetWorld()->GetGameState<AObshagaGameState>();
	if (!GameState->GetInterrogation().bActive)
	{
		// После допроса игрока какое-то время не трогаем, чтобы не ловить его по кругу.
		if (ChaseTarget.IsValid())
		{
			ImmuneUntil.Add(ChaseTarget, GetWorld()->GetTimeSeconds() + GetRoundConfig()->ImmunitySeconds);
		}
		ChaseTarget.Reset();
		SetState(EKomendantState::Patrol);
	}
}

void AKomendantAIController::LogChaseEnd(const AObshagaCharacter* Target, const TCHAR* Reason, const AHidingSpot* Spot, const AItemActor* Item) const
{
	const float Now = GetWorld()->GetTimeSeconds();
	const ARoomVolume* Room = Target ? Target->GetCurrentRoom() : nullptr;
	const ARoomVolume* SpotRoom = Spot ? ARoomVolume::FindRoomAt(this, Spot->GetActorLocation() + FVector(0.f, 0.f, 50.f)) : nullptr;
	const AObshagaPlayerState* TargetState = Target ? Target->GetPlayerState<AObshagaPlayerState>() : nullptr;
	UE_LOG(LogObshaga, Verbose, TEXT("Komendant chase end: %s (%s), reason=%s, %.1f s, seen %.1f s, unseen for %.1f s, ran %.0f cm, rooms %s -> %s (%d changes), z=%.0f, item=%s (%s), spot=%s (%s), exit=%d, suspicion=%.0f, stuck=%d, t=%.1f"),
		*KomendantLogName(TargetState), *GetNameSafe(Target), Reason, Now - ChaseStartTime, ChaseSeen, Now - LastSeenTime, ChaseRun,
		*ChaseStartRoom.ToString(), Room ? *Room->RoomId.ToString() : *ChaseLastRoom.ToString(), ChaseRoomChanges, Target ? Target->GetActorLocation().Z : 0.f,
		*GetNameSafe(Item), *KomendantItemId(Item), *GetNameSafe(Spot), SpotRoom ? *SpotRoom->RoomId.ToString() : TEXT("None"),
		(Spot && Spot->IsA<ANightExitDoor>()) ? 1 : 0, TargetState ? TargetState->GetSuspicionComponent()->GetSuspicion() : 0.f, ChaseStuck, GetRoundSeconds());
}

float AKomendantAIController::GetRoundSeconds() const
{
	const AObshagaGameMode* GameMode = GetWorld()->GetAuthGameMode<AObshagaGameMode>();
	return GameMode ? GameMode->GetRoundSeconds() : GetWorld()->GetTimeSeconds();
}

bool AKomendantAIController::MoveToward(const FVector& Goal, float Speed)
{
	Komendant->GetCharacterMovement()->MaxWalkSpeed = Speed;

	const FVector ToGoal = Goal - Komendant->GetActorLocation();
	if (ToGoal.Size2D() < ArriveDistance && FMath::Abs(ToGoal.Z) < ArriveHeight)
	{
		return true;
	}

#if !UE_BUILD_SHIPPING
	// -TestKomendantStuck: стоит на месте, будто упёрся (только на обходе — погоню и обыск не трогаем).
	if (State == EKomendantState::Patrol && GetWorld()->GetTimeSeconds() < DebugStuckUntil)
	{
		return false;
	}
#endif

	Komendant->AddMovementInput(FVector(ToGoal.X, ToGoal.Y, 0.f).GetSafeNormal());
	return false;
}

bool AKomendantAIController::FollowPath(float Speed, float DeltaSeconds)
{
	if (!Path.IsValidIndex(PathIndex))
	{
		return true;
	}

	const FVector Point = Path[PathIndex];
	if (MoveToward(Point, Speed))
	{
		++PathIndex;
		BestDistanceToPoint = TNumericLimits<float>::Max();
		NoProgressTime = 0.f;
		PawnBlockedCount = 0;
		return !Path.IsValidIndex(PathIndex);
	}

	// Страховка: если упёрся и не приближается к точке, переносим его на неё.
	const float Distance = FVector::Dist2D(Point, Komendant->GetActorLocation());
	if (Distance < BestDistanceToPoint - 15.f)
	{
		BestDistanceToPoint = Distance;
		NoProgressTime = 0.f;
		PawnBlockedCount = 0;
	}
	else
	{
		NoProgressTime += DeltaSeconds;
		if (NoProgressTime > StuckSeconds)
		{
			NoProgressTime = 0.f;
			// Упёрся почти у цели (последняя точка пути стоит в мебели или вплотную к ней) — считаем, что дошёл:
			// переносить его в саму точку значило бы засунуть его в мебель.
			if (PathIndex == Path.Num() - 1 && Distance < StuckNearGoalDistance)
			{
				UE_LOG(LogObshaga, Verbose, TEXT("Komendant blocked %.0f cm short of the last path point %s, treating as arrived"), Distance, *Point.ToCompactString());
				PathIndex = Path.Num();
				return true;
			}
			// Что мешает: капсулой на полметра вперёд, к точке (с теми же исключениями, что и при ходьбе).
			FHitResult Block;
			FCollisionQueryParams BlockParams(SCENE_QUERY_STAT(KomendantStuck), false, Komendant);
			FCollisionResponseParams BlockResponse;
			Komendant->GetCapsuleComponent()->InitSweepCollisionParams(BlockParams, BlockResponse);
			const FVector From = Komendant->GetActorLocation();
			GetWorld()->SweepSingleByChannel(Block, From, From + (Point - From).GetSafeNormal2D() * 60.f, FQuat::Identity, ECC_Pawn,
				FCollisionShape::MakeCapsule(Komendant->GetCapsuleComponent()->GetScaledCapsuleRadius(), Komendant->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 0.8f), BlockParams, BlockResponse);

			// Дорогу загородил жилец (встал в дверях) — сквозь него не переносим, иначе оба застревали друг в друге.
			if (Cast<APawn>(Block.GetActor()))
			{
				UE_LOG(LogObshaga, Verbose, TEXT("Komendant blocked by %s on the way to %s, waiting (%d of %d)"),
					*GetNameSafe(Block.GetActor()), *Point.ToCompactString(), PawnBlockedCount + 1, StuckBehindPawnWaits);
			}
			else
			{
				// t= — секунды раунда: автопроверка сверяет их с проверочным упором (-TestKomendantStuck) и ищет повторы на одной точке.
				UE_LOG(LogObshaga, Warning, TEXT("Komendant stuck near %s on the way to %s (point %d of %d), blocked by %s, snapping to waypoint, t=%.1f"),
					*Komendant->GetActorLocation().ToCompactString(), *Point.ToCompactString(), PathIndex + 1, Path.Num(), *GetNameSafe(Block.GetActor()), GetRoundSeconds());
				if (AObshagaGameMode* StuckMode = GetWorld()->GetAuthGameMode<AObshagaGameMode>())
				{
					StuckMode->ReportStuck(true);
				}
				if (State == EKomendantState::Chase)
				{
					++ChaseStuck;
				}
				// TeleportTo не ставит внутрь мебели или чужой капсулы; свободного места у точки нет — переносить некуда.
				if (Komendant->TeleportTo(Point, Komendant->GetActorRotation()))
				{
					DebugStuckUntil = 0.f;
					UE_LOG(LogObshaga, Verbose, TEXT("Komendant snapped to %s, state %s, t=%.1f"), *Point.ToCompactString(),
						*UEnum::GetValueAsString(State), GetRoundSeconds());
					return false;
				}
				UE_LOG(LogObshaga, Verbose, TEXT("Komendant: no free room at %s, snap skipped"), *Point.ToCompactString());
			}

			// Ждём, пока жилец отойдёт или у точки освободится место. Не выходит несколько раз подряд — бросаем этот путь,
			// как будто дошли, и идём дальше по своим делам: иначе тот, кто встал в дверях, держал бы Палыча вечно.
			++PawnBlockedCount;
			if (PawnBlockedCount >= StuckBehindPawnWaits)
			{
				UE_LOG(LogObshaga, Verbose, TEXT("Komendant gave up the path to %s after %d tries, state %s"), *Point.ToCompactString(),
					PawnBlockedCount, *UEnum::GetValueAsString(State));
				PawnBlockedCount = 0;
				PathIndex = Path.Num();
				return true;
			}
		}
	}
	return false;
}

void AKomendantAIController::SkipPassedPathStart()
{
	// Путь всегда начинается с ближайшей точки маршрута, даже когда Палыч её уже прошёл (середина лестницы) —
	// он сначала шёл бы назад. Если до второй точки ближе, чем от первой, и к ней можно пройти напрямую, первую пропускаем.
	if (Path.Num() < 2)
	{
		return;
	}
	const FVector From = Komendant->GetActorLocation();
	if (FVector::Dist(From, Path[1]) >= FVector::Dist(Path[0], Path[1]))
	{
		return;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(KomendantSkipStart), false, Komendant);
	for (ADoorActor* Door : Doors)
	{
		Params.AddIgnoredActor(Door);
	}
	for (TActorIterator<APawn> It(GetWorld()); It; ++It)
	{
		Params.AddIgnoredActor(*It);
	}
	FHitResult Hit;
	const float Radius = Komendant->GetCapsuleComponent()->GetScaledCapsuleRadius();
	if (!GetWorld()->SweepSingleByChannel(Hit, From, Path[1], FQuat::Identity, ECC_Pawn, FCollisionShape::MakeSphere(Radius), Params))
	{
		Path.RemoveAt(0);
	}
}

bool AKomendantAIController::HasClearLine(const FVector& From, const FVector& To) const
{
	// Мебель, двери, предметы и люди не в счёт — мешают только стены и перекрытия.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(KomendantClearLine), false);
	for (ADoorActor* Door : Doors)
	{
		Params.AddIgnoredActor(Door);
	}
	for (AHidingSpot* Spot : HidingSpots)
	{
		Params.AddIgnoredActor(Spot);
	}
	for (TActorIterator<AItemActor> It(GetWorld()); It; ++It)
	{
		Params.AddIgnoredActor(*It);
	}
	for (TActorIterator<APawn> It(GetWorld()); It; ++It)
	{
		Params.AddIgnoredActor(*It);
	}
	FHitResult Hit;
	return !GetWorld()->LineTraceSingleByChannel(Hit, From, To, ECC_Visibility, Params);
}

AKomendantWaypoint* AKomendantAIController::FindNearestWaypoint(const FVector& Location, bool bNeedSight) const
{
	AKomendantWaypoint* Best = nullptr;
	float BestScore = TNumericLimits<float>::Max();
	for (AKomendantWaypoint* Waypoint : Waypoints)
	{
		const FVector Delta = Waypoint->GetActorLocation() - Location;
		// Точка на другом этаже «далеко», даже если она прямо над головой. Точка на пролёте лестницы ниже или выше
		// тоже хуже точки на своей высоте: иначе с площадки он шагал к ней напрямик и упирался в перила.
		float Score = Delta.Size() + FMath::Abs(Delta.Z) * 3.f + (FMath::Abs(Delta.Z) > ArriveHeight ? 100000.f : 0.f);
		// Точка за стеной от цели хуже любой точки, откуда цель видно: иначе к тайнику он подошёл бы из коридора.
		if (bNeedSight && !HasClearLine(Waypoint->GetActorLocation(), Location))
		{
			Score += 10000.f;
		}
		if (Score < BestScore)
		{
			BestScore = Score;
			Best = Waypoint;
		}
	}
	return Best;
}

bool AKomendantAIController::PlanPathTo(const FVector& Goal, bool bGoalNeedsSight, bool bSkipPassedStart)
{
	Path.Reset();
	PathIndex = 0;
	BestDistanceToPoint = TNumericLimits<float>::Max();
	NoProgressTime = 0.f;
	PawnBlockedCount = 0;

	// Стартовая точка — ближайшая, до которой можно пройти напрямую телом Палыча (мебель мешает, люди и двери — нет).
	// Иначе после обыска шкафа он выбирал точку за этим же шкафом и упирался в него.
	AKomendantWaypoint* Start = nullptr;
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(KomendantStart), false, Komendant);
		for (ADoorActor* Door : Doors)
		{
			Params.AddIgnoredActor(Door);
		}
		for (TActorIterator<APawn> It(GetWorld()); It; ++It)
		{
			Params.AddIgnoredActor(*It);
		}
		const FVector From = Komendant->GetActorLocation();
		const float Radius = Komendant->GetCapsuleComponent()->GetScaledCapsuleRadius();
		float BestScore = TNumericLimits<float>::Max();
		for (AKomendantWaypoint* Waypoint : Waypoints)
		{
			const FVector Delta = Waypoint->GetActorLocation() - From;
			const float Score = Delta.Size() + FMath::Abs(Delta.Z) * 3.f;
			FHitResult Hit;
			if (Score < BestScore && FMath::Abs(Delta.Z) < ArriveHeight
				&& !GetWorld()->SweepSingleByChannel(Hit, From, Waypoint->GetActorLocation(), FQuat::Identity, ECC_Pawn, FCollisionShape::MakeSphere(Radius), Params))
			{
				BestScore = Score;
				Start = Waypoint;
			}
		}
		if (!Start)
		{
			Start = FindNearestWaypoint(From);
		}
	}
	AKomendantWaypoint* End = FindNearestWaypoint(Goal, bGoalNeedsSight);
	if (!Start || !End)
	{
		return false;
	}

	// Кратчайший путь по графу точек (алгоритм Дейкстры; точек мало, хватает простого перебора).
	TMap<AKomendantWaypoint*, float> Distance;
	TMap<AKomendantWaypoint*, AKomendantWaypoint*> Previous;
	TArray<AKomendantWaypoint*> Open;
	Distance.Add(Start, 0.f);
	Open.Add(Start);

	while (!Open.IsEmpty())
	{
		int32 BestIndex = 0;
		for (int32 Index = 1; Index < Open.Num(); ++Index)
		{
			if (Distance[Open[Index]] < Distance[Open[BestIndex]])
			{
				BestIndex = Index;
			}
		}
		AKomendantWaypoint* Current = Open[BestIndex];
		Open.RemoveAtSwap(BestIndex);
		if (Current == End)
		{
			break;
		}

		const float CurrentDistance = Distance[Current];
		for (const TWeakObjectPtr<AKomendantWaypoint>& NeighborPtr : Current->Neighbors)
		{
			AKomendantWaypoint* Neighbor = NeighborPtr.Get();
			if (!Neighbor)
			{
				continue;
			}

			const float NewDistance = CurrentDistance + FVector::Dist(Current->GetActorLocation(), Neighbor->GetActorLocation());
			const float* Known = Distance.Find(Neighbor);
			if (!Known || NewDistance < *Known)
			{
				Distance.Add(Neighbor, NewDistance);
				Previous.Add(Neighbor, Current);
				Open.AddUnique(Neighbor);
			}
		}
	}

	if (!Distance.Contains(End))
	{
		UE_LOG(LogObshaga, Warning, TEXT("Komendant: no path from %s to %s"), *Start->GetName(), *End->GetName());
		return false;
	}

	for (AKomendantWaypoint* Step = End; Step; Step = Previous.FindRef(Step))
	{
		Path.Insert(Step->GetActorLocation(), 0);
	}
	if (bSkipPassedStart)
	{
		SkipPassedPathStart();
	}
	// Через дверные проёмы — ровно посередине, чтобы не цеплять косяк.
	ADoorActor::ThreadDoorways(GetWorld(), Path, Komendant->GetActorLocation());
	return true;
}

AKomendantWaypoint* AKomendantAIController::PickPatrolTarget() const
{
	const AKomendantWaypoint* Here = FindNearestWaypoint(Komendant->GetActorLocation());

	// Взвешенный выбор: чем «горячее» комната, тем чаще туда.
	TArray<AKomendantWaypoint*> Candidates;
	TArray<float> Weights;
	float TotalWeight = 0.f;
	for (AKomendantWaypoint* Waypoint : Waypoints)
	{
		if (Waypoint->bPatrolTarget && Waypoint != Here)
		{
			const float Weight = 1.f + RoomHeat.FindRef(Waypoint->RoomId);
			Candidates.Add(Waypoint);
			Weights.Add(Weight);
			TotalWeight += Weight;
		}
	}
	if (Candidates.IsEmpty())
	{
		return nullptr;
	}

	float Roll = FMath::FRandRange(0.f, TotalWeight);
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		Roll -= Weights[Index];
		if (Roll <= 0.f)
		{
			return Candidates[Index];
		}
	}
	return Candidates.Last();
}

AHidingSpot* AKomendantAIController::PickSpotNear(const FVector& Location) const
{
	// Тайники рядом и на том же этаже; обыскивает случайный из них.
	TArray<AHidingSpot*> Near;
	for (AHidingSpot* Spot : HidingSpots)
	{
		const FVector Delta = GetSpotStandLocation(Spot) - Location;
		// Тайник за стеной не выбирает: дойти до него по прямой он не сможет.
		if (Delta.Size2D() < 600.f && FMath::Abs(Delta.Z) < SameFloorHeight && HasClearLine(Location, Location + Delta))
		{
			Near.Add(Spot);
		}
	}
	return Near.IsEmpty() ? nullptr : Near[FMath::RandRange(0, Near.Num() - 1)];
}

FVector AKomendantAIController::GetSpotStandLocation(const AHidingSpot* Spot) const
{
	// Тайник стоит на полу, а комендант и точки маршрута — на высоте пояса. Без этой поправки тайник
	// второго этажа оказывался «ближе» к точке первого этажа под ним, и комендант шёл не на тот этаж.
	return Spot->GetActorLocation() + FVector(0.f, 0.f, Komendant->GetSimpleCollisionHalfHeight());
}

void AKomendantAIController::OpenDoorsNearby()
{
	const FVector Location = Komendant->GetActorLocation();
	for (ADoorActor* Door : Doors)
	{
		if (!Door->IsOpen() && FVector::Dist(Door->GetDoorCenter(), Location) < DoorOpenDistance)
		{
			Door->OpenFor(Location);
		}
	}
}

void AKomendantAIController::LookAround()
{
	const float Yaw = LookBaseYaw + 75.f * FMath::Sin(StateTime * 1.8f);
	Komendant->SetActorRotation(FRotator(0.f, Yaw, 0.f));
}

void AKomendantAIController::FaceLocation(const FVector& Location)
{
	const FVector Direction = Location - Komendant->GetActorLocation();
	if (!Direction.IsNearlyZero())
	{
		Komendant->SetActorRotation(FRotator(0.f, Direction.Rotation().Yaw, 0.f));
	}
}

FVector AKomendantAIController::GetEvidenceLocation() const
{
	// Изъятое комендант складывает у себя на вахте.
	const UObshagaRoundConfig* Config = GetRoundConfig();
	const ARoomVolume* Room = ARoomVolume::FindRoomById(this, Config->EvidenceRoomId);
	return Room ? Room->GetActorLocation() : Komendant->GetActorLocation();
}

#undef LOCTEXT_NAMESPACE
