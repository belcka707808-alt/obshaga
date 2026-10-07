#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "KomendantWaypoint.generated.h"

/**
 * Точка маршрута коменданта. Ставится примерно в метре над полом.
 * Соседей комендант находит сам: это точки, до которых отсюда есть прямая видимость.
 */
UCLASS()
class OBSHAGA_API AKomendantWaypoint : public AActor
{
	GENERATED_BODY()

public:
	AKomendantWaypoint();

	/** Может ли комендант выбрать эту точку целью обхода (служебные точки на лестницах — нет). */
	UPROPERTY(EditAnywhere, Category = "Waypoint")
	bool bPatrolTarget = true;

	// Заполняет AKomendantAIController при старте, только на сервере.
	FName RoomId;
	TArray<TWeakObjectPtr<AKomendantWaypoint>> Neighbors;
};
