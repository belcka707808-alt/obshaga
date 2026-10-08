#include "RoomVolume.h"

#include "ObshagaVisuals.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace
{
	// «Коврик» зоны чуть приподнят над полом (чтобы не мерцал) и чуть меньше зоны (чтобы не лез под стены).
	constexpr float FloorTintLift = 3.f;
	constexpr float FloorTintMargin = 24.f;
}

ARoomVolume::ARoomVolume()
{
	PrimaryActorTick.bCanEverTick = false;

	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	Box->SetBoxExtent(FVector(200.f, 200.f, 150.f));
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Box->SetCollisionObjectType(ECC_WorldStatic);
	Box->SetCollisionResponseToAllChannels(ECR_Ignore);
	Box->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Box->SetGenerateOverlapEvents(true);
	Box->SetCanEverAffectNavigation(false);
	RootComponent = Box;
}

void ARoomVolume::BeginPlay()
{
	Super::BeginPlay();

	// Цветной «коврик» на весь пол зоны: по цвету пола игрок сразу понимает, где он. Только картинка, без столкновений.
	UStaticMesh* PlaneMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (ZoneColor.A <= 0.f || !PlaneMesh)
	{
		return;
	}

	const FBox Bounds = Box->Bounds.GetBox();
	const FVector Size = Bounds.GetSize();
	UStaticMeshComponent* FloorTint = NewObject<UStaticMeshComponent>(this, TEXT("FloorTint"));
	FloorTint->SetStaticMesh(PlaneMesh);
	FloorTint->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FloorTint->SetCastShadow(false);
	FloorTint->SetUsingAbsoluteLocation(true);
	FloorTint->SetUsingAbsoluteRotation(true);
	FloorTint->SetUsingAbsoluteScale(true);
	FloorTint->SetupAttachment(Box);
	FloorTint->RegisterComponent();
	FloorTint->SetWorldLocation(FVector(Bounds.GetCenter().X, Bounds.GetCenter().Y, Bounds.Min.Z + FloorTintLift));
	FloorTint->SetWorldScale3D(FVector((Size.X - FloorTintMargin) / 100.f, (Size.Y - FloorTintMargin) / 100.f, 1.f));
	ObshagaVisuals::Tint(FloorTint, ZoneColor);
}

ARoomVolume* ARoomVolume::FindRoomAt(const UObject* WorldContextObject, const FVector& Location)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World)
	{
		return nullptr;
	}

	for (TActorIterator<ARoomVolume> It(World); It; ++It)
	{
		if (It->Box->Bounds.GetBox().IsInsideOrOn(Location))
		{
			return *It;
		}
	}
	return nullptr;
}

ARoomVolume* ARoomVolume::FindRoomById(const UObject* WorldContextObject, FName InRoomId)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World || InRoomId.IsNone())
	{
		return nullptr;
	}

	for (TActorIterator<ARoomVolume> It(World); It; ++It)
	{
		if (It->RoomId == InRoomId)
		{
			return *It;
		}
	}
	return nullptr;
}
