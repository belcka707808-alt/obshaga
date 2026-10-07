#include "KomendantAIController.h"

#include "CarryComponent.h"
#include "DoorActor.h"
#include "GameEventSubsystem.h"
#include "HidingSpot.h"
#include "ItemActor.h"
#include "KomendantCharacter.h"
#include "KomendantPersonality.h"
#include "KomendantWaypoint.h"
#include "Obshaga.h"
#include "ObshagaCharacter.h"
#include "ObshagaGameMode.h"
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

#define LOCTEXT_NAMESPACE "Komendant"

namespace
{
	// Геометрия маршрута, а не баланс: насколько далеко могут быть соседние точки и когда точка считается достигнутой.
	constexpr float MaxLinkDistance = 750.f;
	constexpr float ArriveDistance = 70.f;
	constexpr float ArriveHeight = 220.f;
	constexpr float DoorOpenDistance = 190.f;
	constexpr float StuckSeconds = 2.5f;
	constexpr float DirectChaseDistance = 900.f;
	constexpr float SameFloorHeight = 150.f;

	void NotifyPlayer(const APlayerState* TargetState, const FText& Text)
	{
		if (AObshagaPlayerController* Controller = TargetState ? Cast<AObshagaPlayerController>(TargetState->GetOwner()) : nullptr)
		{
			Controller->ClientShowNotice(Text);
		}
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

	Personality = Komendant->PickPersonality();
	ApplyPersonality();
	Perception->OnTargetPerceptionUpdated.AddUniqueDynamic(this, &AKomendantAIController::OnPerceptionUpdated);

	// Точки маршрута и двери должны успеть появиться в мире.
	FTimerHandle Handle;
	GetWorldTimerManager().SetTimer(Handle, this, &AKomendantAIController::BuildGraph, 0.5f, false);
}

void AKomendantAIController::ApplyPersonality()
{
	SightConfig->SightRadius = Personality->SightRadius;
	SightConfig->LoseSightRadius = Personality->SightRadius * 1.15f;
	SightConfig->PeripheralVisionAngleDegrees = Personality->SightHalfAngle;
	HearingConfig->HearingRange = Personality->HearingRange;
	Perception->ConfigureSense(*SightConfig);
	Perception->ConfigureSense(*HearingConfig);

	UE_LOG(LogObshaga, Log, TEXT("Komendant personality: %s"), *Personality->GetName());
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
				Waypoints[A]->Neighbors.Add(Waypoints[B]);
				Waypoints[B]->Neighbors.Add(Waypoints[A]);
				++NumLinks;
			}
		}
	}

	bGraphReady = Waypoints.Num() > 0;
	UE_LOG(LogObshaga, Log, TEXT("Komendant graph: %d waypoints, %d links, %d doors, %d hiding spots"), Waypoints.Num(), NumLinks, Doors.Num(), HidingSpots.Num());
}

void AKomendantAIController::SetState(EKomendantState NewState)
{
	State = NewState;
	StateTime = 0.f;
	Path.Reset();
	PathIndex = 0;
	WaitTime = 0.f;
	SearchTime = 0.f;
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
		if (Player && !Player->IsHiding())
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
	if (State == EKomendantState::Interrogate)
	{
		return;
	}

	const UObshagaRoundConfig* Config = GetRoundConfig();
	const float Now = GetWorld()->GetTimeSeconds();

	for (AObshagaCharacter* Player : SeenPlayers)
	{
		AObshagaPlayerState* TargetState = Player->GetPlayerState<AObshagaPlayerState>();
		if (!TargetState || Player->IsFrozen() || ImmuneUntil.FindRef(Player) > Now)
		{
			continue;
		}

		// Событие «заметил» — не чаще раза в несколько секунд на игрока.
		float& LastEvent = LastSpottedEventTime.FindOrAdd(Player, -100.f);
		if (Now - LastEvent > 5.f)
		{
			LastEvent = Now;
			UGameEventSubsystem::PublishFrom(Player, EGameEventType::PlayerSpotted);
		}

		// Подозрительно всё, что несут в руках: запрещёнка — сразу, тяжёлое — быстро, мелочь — понемногу.
		float Rate = 0.f;
		if (const AItemActor* Item = Player->GetCarryComponent()->GetCarriedItem())
		{
			const UObshagaItemData* Data = Item->GetItemData();
			Rate = Data->bContraband ? Config->ContrabandSuspicionPerSecond
				: (Data->bHeavy ? Config->HeavyCarrySuspicionPerSecond : Config->CarrySuspicionPerSecond);
		}

		USuspicionComponent* Suspicion = TargetState->GetSuspicionComponent();
		if (Rate > 0.f)
		{
			Suspicion->AddSuspicion(Rate * Personality->SuspicionMultiplier * DeltaSeconds);
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

	if (const ARoomVolume* Room = ARoomVolume::FindRoomAt(this, Stimulus.StimulusLocation))
	{
		RoomHeat.FindOrAdd(Room->RoomId) += Stimulus.Strength * 10.f;
	}

	// Тихий шум (скрип двери) только «греет» комнату; на громкий он идёт смотреть.
	const bool bBusy = State == EKomendantState::Chase || State == EKomendantState::Interrogate;
	if (!bBusy && Stimulus.Strength >= GetRoundConfig()->KomendantMinNoise)
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
}

void AKomendantAIController::StartChase(AObshagaCharacter* Target)
{
	SetState(EKomendantState::Chase);
	ChaseTarget = Target;
	LastKnownLocation = Target->GetActorLocation();
	LastSeenTime = GetWorld()->GetTimeSeconds();
	ReplanTime = 0.f;
	NotifyPlayer(Target->GetPlayerState(), LOCTEXT("ChaseNotice", "Комендант идёт за тобой!"));
}

void AKomendantAIController::TickPatrol(float DeltaSeconds)
{
	if (WaitTime > 0.f)
	{
		WaitTime -= DeltaSeconds;
		return;
	}

	if (Path.IsEmpty())
	{
		const AKomendantWaypoint* Target = PickPatrolTarget();
		if (!Target || !PlanPathTo(Target->GetActorLocation()))
		{
			WaitTime = 1.f;
		}
		return;
	}

	if (!FollowPath(Personality->PatrolSpeed, DeltaSeconds))
	{
		return;
	}

	// Дошёл до точки обхода: ложная тревога, обыск тайника или короткая пауза.
	Path.Reset();
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
	if (InvestigateLookTime >= GetRoundConfig()->InvestigateSeconds)
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
		SetState(EKomendantState::Patrol);
		return;
	}

	if (Target->IsHiding())
	{
		// Видел, как нарушитель залез в укрытие, — идёт открывать. Не видел — ищет на месте.
		if (Now - LastSeenTime < 1.f && Target->GetHidingSpot())
		{
			AHidingSpot* Spot = Target->GetHidingSpot();
			SetState(EKomendantState::Inspect);
			InspectSpot = Spot;
		}
		else
		{
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
		StartInvestigate(LastKnownLocation);
		return;
	}

	const FVector ToTarget = LastKnownLocation - Komendant->GetActorLocation();
	const bool bSameFloor = FMath::Abs(ToTarget.Z) < SameFloorHeight;
	if (bSeen && bSameFloor && ToTarget.Size2D() <= GetRoundConfig()->KomendantCatchDistance)
	{
		AObshagaGameMode* GameMode = GetWorld()->GetAuthGameMode<AObshagaGameMode>();
		if (GameMode && GameMode->StartInterrogation(Target, GetEvidenceLocation()))
		{
			SetState(EKomendantState::Interrogate);
			ChaseTarget = Target;
		}
		else
		{
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
	const FVector SpotLocation = Spot->GetActorLocation();
	const float Distance = FVector::Dist2D(SpotLocation, Komendant->GetActorLocation());
	if (SearchTime <= 0.f && Distance > 170.f && StateTime < 4.f)
	{
		MoveToward(SpotLocation, Personality->PatrolSpeed * 1.4f);
		return;
	}

	FaceLocation(SpotLocation);
	SearchTime += DeltaSeconds;
	if (SearchTime >= GetRoundConfig()->InspectSeconds)
	{
		SearchSpot(Spot);
	}
}

void AKomendantAIController::SearchSpot(AHidingSpot* Spot)
{
	const UObshagaRoundConfig* Config = GetRoundConfig();

	AObshagaCharacter* FoundPlayer = nullptr;
	AItemActor* Contraband = Spot->KomendantSearch(FoundPlayer);
	UE_LOG(LogObshaga, Verbose, TEXT("Komendant searched %s: contraband=%s player=%s"), *Spot->GetName(), *GetNameSafe(Contraband), *GetNameSafe(FoundPlayer));

	if (Contraband)
	{
		// Запрещёнку уносит на вахту; тот, кто прятал, под подозрением.
		APlayerState* Hider = Contraband->GetLastHiddenBy();
		Contraband->ReleaseToWorld(GetEvidenceLocation(), FVector::ZeroVector);

		if (UGameEventSubsystem* Bus = UGameEventSubsystem::Get(this))
		{
			FGameEvent Event;
			Event.Type = EGameEventType::ContrabandConfiscated;
			Event.Instigator = Hider;
			Event.Item = Contraband;
			const ARoomVolume* Room = ARoomVolume::FindRoomAt(this, Spot->GetActorLocation() + FVector(0.f, 0.f, 50.f));
			Event.RoomId = Room ? Room->RoomId : NAME_None;
			Bus->Publish(Event);
		}

		if (const AObshagaPlayerState* HiderState = Cast<AObshagaPlayerState>(Hider))
		{
			HiderState->GetSuspicionComponent()->AddSuspicion(Config->HiddenContrabandSuspicion);
			NotifyPlayer(HiderState, LOCTEXT("ConfiscatedNotice", "Комендант нашёл твою запрещёнку!"));
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

bool AKomendantAIController::MoveToward(const FVector& Goal, float Speed)
{
	Komendant->GetCharacterMovement()->MaxWalkSpeed = Speed;

	const FVector ToGoal = Goal - Komendant->GetActorLocation();
	if (ToGoal.Size2D() < ArriveDistance && FMath::Abs(ToGoal.Z) < ArriveHeight)
	{
		return true;
	}

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
		return !Path.IsValidIndex(PathIndex);
	}

	// Страховка: если упёрся и не приближается к точке, переносим его на неё.
	const float Distance = FVector::Dist2D(Point, Komendant->GetActorLocation());
	if (Distance < BestDistanceToPoint - 15.f)
	{
		BestDistanceToPoint = Distance;
		NoProgressTime = 0.f;
	}
	else
	{
		NoProgressTime += DeltaSeconds;
		if (NoProgressTime > StuckSeconds)
		{
			UE_LOG(LogObshaga, Warning, TEXT("Komendant stuck near %s, snapping to waypoint"), *Komendant->GetActorLocation().ToCompactString());
			Komendant->SetActorLocation(Point, false, nullptr, ETeleportType::TeleportPhysics);
			NoProgressTime = 0.f;
		}
	}
	return false;
}

AKomendantWaypoint* AKomendantAIController::FindNearestWaypoint(const FVector& Location) const
{
	AKomendantWaypoint* Best = nullptr;
	float BestScore = TNumericLimits<float>::Max();
	for (AKomendantWaypoint* Waypoint : Waypoints)
	{
		const FVector Delta = Waypoint->GetActorLocation() - Location;
		// Точка на другом этаже «далеко», даже если она прямо над головой.
		const float Score = Delta.Size() + (FMath::Abs(Delta.Z) > ArriveHeight ? 100000.f : 0.f);
		if (Score < BestScore)
		{
			BestScore = Score;
			Best = Waypoint;
		}
	}
	return Best;
}

bool AKomendantAIController::PlanPathTo(const FVector& Goal)
{
	Path.Reset();
	PathIndex = 0;
	BestDistanceToPoint = TNumericLimits<float>::Max();
	NoProgressTime = 0.f;

	AKomendantWaypoint* Start = FindNearestWaypoint(Komendant->GetActorLocation());
	AKomendantWaypoint* End = FindNearestWaypoint(Goal);
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
		const FVector Delta = Spot->GetActorLocation() - Location;
		if (Delta.Size2D() < 600.f && FMath::Abs(Delta.Z) < ArriveHeight)
		{
			Near.Add(Spot);
		}
	}
	return Near.IsEmpty() ? nullptr : Near[FMath::RandRange(0, Near.Num() - 1)];
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
