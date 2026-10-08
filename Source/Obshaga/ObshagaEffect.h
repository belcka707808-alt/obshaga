#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ObshagaEffect.generated.h"

class UStaticMeshComponent;

UENUM()
enum class EObshagaEffect : uint8
{
	/** Искры: прибор сломался. */
	Sparks,
	/** Облачко пыли: упало что-то тяжёлое. */
	Dust
};

/**
 * Простой разовый эффект без ассетов: горсть маленьких фигурок разлетается и исчезает меньше чем за секунду.
 * Живёт только на той машине, где появился (по сети не ходит): каждая машина запускает его сама
 * по тому же событию, по которому играет звук.
 */
UCLASS(NotPlaceable)
class OBSHAGA_API AObshagaEffect : public AActor
{
	GENERATED_BODY()

public:
	AObshagaEffect();

	virtual void Tick(float DeltaSeconds) override;

	/** Показать эффект в точке мира на этой машине. */
	static void Play(const UObject* WorldContextObject, EObshagaEffect Kind, const FVector& Location);

private:
	void Build(EObshagaEffect Kind);

	struct FPiece
	{
		TWeakObjectPtr<UStaticMeshComponent> Mesh;
		FVector Velocity = FVector::ZeroVector;
	};

	TArray<FPiece> Pieces;
	float Age = 0.f;
	float Lifetime = 1.f;
	float Gravity = 0.f;
	float Drag = 0.f;
	float StartScale = 0.1f;
	float PeakScale = 0.1f;
};
