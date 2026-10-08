#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DecorActor.generated.h"

class UStaticMesh;
class UStaticMeshComponent;

/**
 * Мебель, с которой нельзя взаимодействовать: кухонная стойка, стол. Устроена как тайник без тайника —
 * куб нужного размера держит столкновения, а поверх него надета модель.
 */
UCLASS()
class OBSHAGA_API ADecorActor : public AActor
{
	GENERATED_BODY()

public:
	ADecorActor();

	virtual void OnConstruction(const FTransform& Transform) override;

protected:
	virtual void BeginPlay() override;

	void ApplySize();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Decor")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** Размер куба; начало координат актора — центр основания. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Decor", meta = (Units = "cm"))
	FVector BoxSize = FVector(100.f, 100.f, 80.f);

	/** Модель вместо куба; пусто — остаётся куб. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Decor")
	TObjectPtr<UStaticMesh> Model;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Decor", meta = (Units = "deg"))
	float ModelYaw = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Decor")
	FLinearColor Color = FLinearColor(0.7f, 0.68f, 0.6f);
};
