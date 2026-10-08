#include "DecorActor.h"

#include "ObshagaVisuals.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

ADecorActor::ADecorActor()
{
	PrimaryActorTick.bCanEverTick = false;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(RootComponent);
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		Mesh->SetStaticMesh(CubeMesh.Object);
	}
	ApplySize();
}

void ADecorActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	ApplySize();
}

void ADecorActor::BeginPlay()
{
	Super::BeginPlay();

	ApplySize();
	ObshagaVisuals::Tint(Mesh, Color);
	ObshagaVisuals::Dress(Mesh, Model, ModelYaw, Color);
}

void ADecorActor::ApplySize()
{
	Mesh->SetRelativeScale3D(BoxSize / 100.f);
	Mesh->SetRelativeLocation(FVector(0.f, 0.f, BoxSize.Z * 0.5f));
}
