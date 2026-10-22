#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ObshagaAudioConfig.h"
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
	 * Kind уходит слуху Палыча как Tag стимула (см. GetNoiseKind).
	 */
	UFUNCTION(BlueprintCallable, Category = "Obshaga|Noise", meta = (WorldContext = "WorldContextObject"))
	static void MakeGameNoise(const UObject* WorldContextObject, FVector Location, float Loudness, AActor* NoiseInstigator, ENoiseKind Kind = ENoiseKind::Impact);

	/** Вид шума для журнала и для Tag стимула слуха: Impact, Door, Device, Thrown, Other. */
	static const TCHAR* GetNoiseKindName(ENoiseKind Kind);
	/** Вид шума по Tag стимула слуха; незнакомый Tag — Other. */
	static ENoiseKind GetNoiseKind(FName Tag);
};
