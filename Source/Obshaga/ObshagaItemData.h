#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ObshagaItemData.generated.h"

/** «Паспорт предмета»: один ассет на вид предмета (телевизор, чайник...). */
UCLASS(BlueprintType)
class OBSHAGA_API UObshagaItemData : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Короткий код вида предмета для заданий: TV, Contraband... */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
	FName ItemId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
	FText DisplayName;

	/** Размер серого куба-заглушки, пока нет настоящей модели. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item", meta = (Units = "cm"))
	FVector BoxSize = FVector(30.f, 30.f, 30.f);

	/** Модель предмета вместо куба; пусто — остаётся куб. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
	TObjectPtr<class UStaticMesh> Model;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item", meta = (Units = "deg"))
	float ModelYaw = 0.f;

	/** Цвет куба-заглушки или модели. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
	FLinearColor Color = FLinearColor(0.35f, 0.35f, 0.4f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item", meta = (ClampMin = "0.01", Units = "kg"))
	float Weight = 1.f;

	/** Тяжёлый: несут медленно и без бега. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
	bool bHeavy = false;

	/** Запрещёнка: за неё комендант ловит. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
	bool bContraband = false;

	/** Записка: её можно прочитать (F), и в ней написан слух. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
	bool bReadable = false;

	/** С какой скоростью предмет вылетает из рук при броске. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Throw", meta = (ClampMin = "0", Units = "cm/s"))
	float ThrowSpeed = 900.f;

	/** Насколько предмет шумный при ударе: 0 — бесшумный, 1 — грохот. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Noise", meta = (ClampMin = "0", ClampMax = "1"))
	float NoiseFactor = 0.5f;

	/** Удары слабее этого шума не дают. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Noise", meta = (ClampMin = "0", Units = "cm/s"))
	float MinImpactSpeed = 150.f;

	/** Удар с такой скоростью и сильнее даёт максимальную громкость предмета. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Noise", meta = (ClampMin = "1", Units = "cm/s"))
	float LoudImpactSpeed = 800.f;
};
