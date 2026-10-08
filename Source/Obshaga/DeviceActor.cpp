#include "DeviceActor.h"

#include "GameEventSubsystem.h"
#include "InteractionComponent.h"
#include "NoiseStatics.h"
#include "Obshaga.h"
#include "ObshagaCharacter.h"
#include "ObshagaGameState.h"
#include "ObshagaPlayerController.h"
#include "ObshagaVisuals.h"
#include "RoomVolume.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "DeviceActor"

namespace
{
	void NotifyDeviceUser(AObshagaCharacter* Character, const FText& Text)
	{
		if (AObshagaPlayerController* Controller = Character ? Cast<AObshagaPlayerController>(Character->GetController()) : nullptr)
		{
			Controller->ClientShowNotice(Text);
		}
	}
}

ADeviceActor::ADeviceActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(RootComponent);
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));

	// Серый куб движка — заглушка до настоящей модели.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		Mesh->SetStaticMesh(CubeMesh.Object);
	}

	DisplayName = LOCTEXT("DefaultName", "Плита");
	ApplySize();
}

void ADeviceActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ADeviceActor, bBroken);
	DOREPLIFETIME(ADeviceActor, bBusy);
}

void ADeviceActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	ApplySize();
}

void ADeviceActor::BeginPlay()
{
	Super::BeginPlay();

	ApplySize();
	ObshagaVisuals::Tint(Mesh, Color);
}

void ADeviceActor::ApplySize()
{
	// Начало координат актора — центр основания.
	Mesh->SetRelativeScale3D(BoxSize / 100.f);
	Mesh->SetRelativeLocation(FVector(0.f, 0.f, BoxSize.Z * 0.5f));
}

FVector ADeviceActor::GetLabelLocation() const
{
	return GetActorLocation() + FVector(0.f, 0.f, BoxSize.Z + 25.f);
}

FName ADeviceActor::GetRoomId() const
{
	const ARoomVolume* Room = ARoomVolume::FindRoomAt(this, GetActorLocation() + FVector(0.f, 0.f, 50.f));
	return Room ? Room->RoomId : NAME_None;
}

bool ADeviceActor::IsRoundInProgress() const
{
	// В лобби и на экране итогов прибор не трогают: иначе задания засчитывались бы заранее.
	const AObshagaGameState* GameState = GetWorld()->GetGameState<AObshagaGameState>();
	return GameState && GameState->GetRoundState() == ERoundState::InProgress;
}

FText ADeviceActor::GetInteractionPrompt(const AObshagaCharacter* By) const
{
	if (!By || !bBroken || !IsRoundInProgress())
	{
		return FText::GetEmpty();
	}
	if (bBusy)
	{
		return LOCTEXT("Busy", "Занято... не отходи");
	}
	return FText::Format(LOCTEXT("Repair", "Починить: {0} ({1} с)"), DisplayName, FText::AsNumber(FMath::RoundToInt32(RepairDuration)));
}

FText ADeviceActor::GetSecondaryPrompt(const AObshagaCharacter* By) const
{
	if (!By || bBroken || !IsRoundInProgress())
	{
		return FText::GetEmpty();
	}
	if (bBusy)
	{
		return LOCTEXT("Busy", "Занято... не отходи");
	}
	return FText::Format(LOCTEXT("Break", "Сломать: {0}"), DisplayName);
}

void ADeviceActor::Interact(AObshagaCharacter* By)
{
	if (HasAuthority() && By && bBroken && !bBusy && IsRoundInProgress())
	{
		StartTimedAction(By, false);
	}
}

void ADeviceActor::SecondaryInteract(AObshagaCharacter* By)
{
	if (HasAuthority() && By && !bBroken && !bBusy && IsRoundInProgress())
	{
		StartTimedAction(By, true);
	}
}

void ADeviceActor::StartTimedAction(AObshagaCharacter* By, bool bBreak)
{
	bBusy = true;
	PendingBy = By;
	bPendingBreak = bBreak;

	const float Duration = bBreak ? BreakDuration : RepairDuration;
	if (Duration <= 0.f)
	{
		FinishTimedAction();
		return;
	}
	GetWorldTimerManager().SetTimer(ActionTimer, this, &ADeviceActor::FinishTimedAction, Duration, false);
}

void ADeviceActor::FinishTimedAction()
{
	bBusy = false;

	AObshagaCharacter* By = PendingBy.Get();
	PendingBy.Reset();

	// Отошёл, спрятался или попался раньше времени — действие сорвалось.
	if (!By || By->IsHiding() || By->IsFrozen() || By->IsGhost()
		|| !By->GetInteractionComponent()->IsInRange(this, UInteractionComponent::ServerRangeSlack))
	{
		NotifyDeviceUser(By, LOCTEXT("Interrupted", "Не успел: отошёл слишком рано"));
		return;
	}

	SetBroken(bPendingBreak, By);
	NotifyDeviceUser(By, FText::Format(bPendingBreak ? LOCTEXT("Broke", "Сломано: {0}") : LOCTEXT("Repaired", "Починено: {0}"), DisplayName));
}

void ADeviceActor::SetBroken(bool bNewBroken, AObshagaCharacter* By)
{
	bBroken = bNewBroken;
	APlayerState* ByState = By ? By->GetPlayerState() : nullptr;
	if (bNewBroken)
	{
		BrokenBy = ByState;
		RepairedBy.Reset();
		BrokenTime = GetWorld()->GetTimeSeconds();
	}
	else
	{
		RepairedBy = ByState;
	}
	ForceNetUpdate();

	if (UGameEventSubsystem* Bus = UGameEventSubsystem::Get(this))
	{
		FGameEvent Event;
		Event.Type = bNewBroken ? EGameEventType::DeviceBroken : EGameEventType::DeviceRepaired;
		Event.Instigator = ByState;
		Event.RoomId = GetRoomId();
		Bus->Publish(Event);
	}
	UE_LOG(LogObshaga, Verbose, TEXT("%s %s by %s"), *GetName(), bNewBroken ? TEXT("broken") : TEXT("repaired"), *GetNameSafe(By));

	// Поломка трещит: комендант может прийти посмотреть.
	if (bNewBroken)
	{
		UNoiseStatics::MakeGameNoise(this, GetLabelLocation(), BreakLoudness, By);
	}
}

void ADeviceActor::BreakByItself()
{
	if (HasAuthority() && !bBroken && !bBusy)
	{
		SetBroken(true, nullptr);
	}
}

void ADeviceActor::ResetDevice()
{
	if (HasAuthority())
	{
		GetWorldTimerManager().ClearTimer(ActionTimer);
		PendingBy.Reset();
		bBusy = false;
		bBroken = false;
		BrokenBy.Reset();
		RepairedBy.Reset();
		ForceNetUpdate();
	}
}

#undef LOCTEXT_NAMESPACE
