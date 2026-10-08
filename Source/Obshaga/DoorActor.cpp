#include "DoorActor.h"

#include "GameEventSubsystem.h"
#include "NoiseStatics.h"
#include "Obshaga.h"
#include "ObshagaCharacter.h"
#include "ObshagaGameMode.h"
#include "ObshagaGameState.h"
#include "ObshagaRoundConfig.h"
#include "ObshagaVisuals.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "DoorActor"

ADoorActor::ADoorActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	bReplicates = true;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	// Петля стоит в начале координат актора, створка уходит от неё вдоль оси X.
	Hinge = CreateDefaultSubobject<USceneComponent>(TEXT("Hinge"));
	Hinge->SetupAttachment(RootComponent);

	DoorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DoorMesh"));
	DoorMesh->SetupAttachment(Hinge);
	DoorMesh->SetRelativeLocation(FVector(50.f, 0.f, 105.f));
	DoorMesh->SetRelativeScale3D(FVector(1.f, 0.08f, 2.1f));
	DoorMesh->SetCollisionProfileName(TEXT("BlockAll"));

	// Серый куб движка — заглушка до настоящей модели двери.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		DoorMesh->SetStaticMesh(CubeMesh.Object);
	}
}

void ADoorActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ADoorActor, DoorState);
}

void ADoorActor::BeginPlay()
{
	Super::BeginPlay();

	// Кто подключился позже, сразу видит дверь в нужном положении, без анимации.
	CurrentYaw = GetTargetYaw();
	Hinge->SetRelativeRotation(FRotator(0.f, CurrentYaw, 0.f));
	ObshagaVisuals::Tint(DoorMesh, Color);
	ObshagaVisuals::Dress(DoorMesh, Model, ModelYaw, Color);
}

float ADoorActor::GetTargetYaw() const
{
	switch (DoorState)
	{
	case EDoorState::OpenForward:
		return OpenAngle;
	case EDoorState::OpenBackward:
		return -OpenAngle;
	default:
		return 0.f;
	}
}

FText ADoorActor::GetInteractionPrompt(const AObshagaCharacter* By) const
{
	return IsOpen() ? LOCTEXT("Close", "Закрыть дверь") : LOCTEXT("Open", "Открыть дверь");
}

void ADoorActor::Interact(AObshagaCharacter* By)
{
	if (!HasAuthority())
	{
		return;
	}

	if (IsOpen())
	{
		DoorState = EDoorState::Closed;
	}
	else
	{
		// Дверь открывается от игрока, чтобы не бить его створкой.
		const FVector ToPlayer = By ? By->GetActorLocation() - GetActorLocation() : FVector::ZeroVector;
		const bool bPlayerOnPositiveSide = FVector::DotProduct(ToPlayer, GetActorRightVector()) > 0.f;
		DoorState = bPlayerOnPositiveSide ? EDoorState::OpenBackward : EDoorState::OpenForward;
	}

	UE_LOG(LogObshaga, Verbose, TEXT("%s %s by %s"), *GetName(), IsOpen() ? TEXT("opened") : TEXT("closed"), *GetNameSafe(By));
	OnRep_DoorState();
	UGameEventSubsystem::PublishFrom(By, IsOpen() ? EGameEventType::DoorOpened : EGameEventType::DoorClosed);

	// Скрип двери слышно рядом.
	// Ночью двери слышнее.
	float Loudness = NoiseLoudness;
	const AObshagaGameState* GameState = GetWorld()->GetGameState<AObshagaGameState>();
	const AObshagaGameMode* GameMode = GetWorld()->GetAuthGameMode<AObshagaGameMode>();
	if (GameState && GameMode && GameState->GetRoundState() == ERoundState::InProgress && GameState->GetPhase() == ERoundPhase::Night)
	{
		Loudness *= GameMode->GetRoundConfig()->NightDoorNoiseMultiplier;
	}
	UNoiseStatics::MakeGameNoise(this, DoorMesh->GetComponentLocation(), FMath::Min(Loudness, 1.f), By, ENoiseKind::Door);
}

void ADoorActor::ResetDoor()
{
	if (HasAuthority() && IsOpen())
	{
		DoorState = EDoorState::Closed;
		OnRep_DoorState();
	}
}

FVector ADoorActor::GetDoorCenter() const
{
	return DoorMesh->GetComponentLocation();
}

void ADoorActor::OpenFor(const FVector& FromLocation)
{
	if (!HasAuthority() || IsOpen())
	{
		return;
	}

	const bool bOnPositiveSide = FVector::DotProduct(FromLocation - GetActorLocation(), GetActorRightVector()) > 0.f;
	DoorState = bOnPositiveSide ? EDoorState::OpenBackward : EDoorState::OpenForward;
	OnRep_DoorState();
}

void ADoorActor::OnRep_DoorState()
{
	SetActorTickEnabled(true);
}

void ADoorActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const float TargetYaw = GetTargetYaw();
	CurrentYaw = FMath::FInterpConstantTo(CurrentYaw, TargetYaw, DeltaSeconds, OpenSpeed);
	Hinge->SetRelativeRotation(FRotator(0.f, CurrentYaw, 0.f));

	if (FMath::IsNearlyEqual(CurrentYaw, TargetYaw))
	{
		SetActorTickEnabled(false);
	}
}

#undef LOCTEXT_NAMESPACE
