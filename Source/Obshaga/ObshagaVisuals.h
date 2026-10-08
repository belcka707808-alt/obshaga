#pragma once

#include "CoreMinimal.h"

class UStaticMeshComponent;

/** Простая стилизация серого макета: однотонная покраска кубов-заглушек, пока нет настоящих моделей. */
namespace ObshagaVisuals
{
	/** Красит меш в один цвет (матовый материал движка с параметром цвета). Работает на каждой машине отдельно. */
	OBSHAGA_API void Tint(UStaticMeshComponent* Mesh, const FLinearColor& Color);
}
