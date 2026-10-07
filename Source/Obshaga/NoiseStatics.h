#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "NoiseStatics.generated.h"

/** Единая точка входа для любого шума в игре. */
UCLASS()
class OBSHAGA_API UNoiseStatics : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Сообщает, что в точке Location было громко. Работает только на сервере.
	 * Loudness: 0 — тишина, 1 — грохот. Шум слышит AI (Perception) и игроки, которые достаточно близко.
	 */
	UFUNCTION(BlueprintCallable, Category = "Obshaga|Noise", meta = (WorldContext = "WorldContextObject"))
	static void MakeGameNoise(const UObject* WorldContextObject, FVector Location, float Loudness, AActor* NoiseInstigator);
};
