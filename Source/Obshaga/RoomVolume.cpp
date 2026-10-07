#include "RoomVolume.h"

#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"

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
