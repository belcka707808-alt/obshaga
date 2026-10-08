#include "ObshagaEffect.h"

#include "ObshagaVisuals.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"

namespace
{
	// Это картинка, а не баланс: числа подобраны на глаз и на игру не влияют.
	constexpr int32 NumSparks = 10;
	constexpr int32 NumDustPuffs = 6;
}

AObshagaEffect::AObshagaEffect()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AObshagaEffect::Play(const UObject* WorldContextObject, EObshagaEffect Kind, const FVector& Location)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (AObshagaEffect* Effect = World->SpawnActor<AObshagaEffect>(Location, FRotator::ZeroRotator, Params))
	{
		Effect->Build(Kind);
	}
}

void AObshagaEffect::Build(EObshagaEffect Kind)
{
	const bool bSparks = Kind == EObshagaEffect::Sparks;
	UStaticMesh* Shape = LoadObject<UStaticMesh>(nullptr, bSparks ? TEXT("/Engine/BasicShapes/Cube.Cube") : TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (!Shape)
	{
		Destroy();
		return;
	}

	// Искры — мелкие жёлтые кубики, летят вверх и падают. Пыль — серые шарики, расползаются по полу и раздуваются.
	Lifetime = bSparks ? 0.6f : 0.8f;
	Gravity = bSparks ? -980.f : 0.f;
	Drag = bSparks ? 0.f : 2.5f;
	StartScale = bSparks ? 0.05f : 0.12f;
	PeakScale = bSparks ? 0.05f : 0.5f;

	const int32 Count = bSparks ? NumSparks : NumDustPuffs;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(this);
		Mesh->SetStaticMesh(Shape);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetCastShadow(false);
		Mesh->SetupAttachment(RootComponent);
		Mesh->RegisterComponent();
		Mesh->SetRelativeScale3D(FVector(StartScale));
		ObshagaVisuals::Tint(Mesh, bSparks ? FLinearColor(1.f, FMath::FRandRange(0.5f, 0.9f), 0.05f) : FLinearColor(0.55f, 0.52f, 0.47f));

		FPiece& Piece = Pieces.AddDefaulted_GetRef();
		Piece.Mesh = Mesh;
		const float Angle = FMath::FRandRange(0.f, 2.f * PI);
		const float Out = bSparks ? FMath::FRandRange(60.f, 220.f) : FMath::FRandRange(80.f, 180.f);
		const float Up = bSparks ? FMath::FRandRange(180.f, 380.f) : FMath::FRandRange(10.f, 40.f);
		Piece.Velocity = FVector(FMath::Cos(Angle) * Out, FMath::Sin(Angle) * Out, Up);
	}
}

void AObshagaEffect::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	Age += DeltaSeconds;
	if (Age >= Lifetime)
	{
		Destroy();
		return;
	}

	// Сначала фигурка растёт до пика, в последней трети жизни сжимается в ноль — так она «тает» без прозрачности.
	const float Alpha = Age / Lifetime;
	const float Scale = FMath::Lerp(StartScale, PeakScale, FMath::Min(Alpha / 0.67f, 1.f)) * FMath::Clamp((1.f - Alpha) / 0.33f, 0.f, 1.f);
	for (FPiece& Piece : Pieces)
	{
		if (UStaticMeshComponent* Mesh = Piece.Mesh.Get())
		{
			Piece.Velocity.Z += Gravity * DeltaSeconds;
			Piece.Velocity *= FMath::Max(0.f, 1.f - Drag * DeltaSeconds);
			Mesh->AddRelativeLocation(Piece.Velocity * DeltaSeconds);
			Mesh->SetRelativeScale3D(FVector(Scale));
		}
	}
}
