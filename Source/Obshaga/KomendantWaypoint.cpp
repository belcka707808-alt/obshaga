#include "KomendantWaypoint.h"

#include "Components/SceneComponent.h"

AKomendantWaypoint::AKomendantWaypoint()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}
