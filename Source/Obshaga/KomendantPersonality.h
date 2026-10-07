#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "KomendantPersonality.generated.h"

/** Личность коменданта: один ассет на характер (строгий, рассеянный, мстительный). Выбирается случайно на раунд. */
UCLASS(BlueprintType)
class OBSHAGA_API UKomendantPersonality : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Personality")
	FText DisplayName;

	/** Как далеко он видит. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Senses", meta = (ClampMin = "100", Units = "cm"))
	float SightRadius = 1400.f;

	/** Половина угла конуса зрения. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Senses", meta = (ClampMin = "5", ClampMax = "180", Units = "deg"))
	float SightHalfAngle = 40.f;

	/** С какого расстояния он слышит самый громкий шум (громкость 1). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Senses", meta = (ClampMin = "0", Units = "cm"))
	float HearingRange = 3000.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0", Units = "cm/s"))
	float PatrolSpeed = 220.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0", Units = "cm/s"))
	float ChaseSpeed = 480.f;

	/** Сколько секунд он ещё ищет цель, потеряв её из виду. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behavior", meta = (ClampMin = "0", Units = "s"))
	float PatienceSeconds = 6.f;

	/** Шанс «ложной тревоги» на каждой точке обхода. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behavior", meta = (ClampMin = "0", ClampMax = "1"))
	float BluffChance = 0.15f;

	/** Шанс обыскать тайник рядом с точкой обхода. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behavior", meta = (ClampMin = "0", ClampMax = "1"))
	float InspectChance = 0.25f;

	/** Во сколько раз быстрее или медленнее у игроков растёт подозрение. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behavior", meta = (ClampMin = "0"))
	float SuspicionMultiplier = 1.f;
};
