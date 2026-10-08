#pragma once

#include "CoreMinimal.h"

class UStaticMesh;
class UStaticMeshComponent;

/** Простая стилизация серого макета: однотонная покраска кубов-заглушек, пока нет настоящих моделей. */
namespace ObshagaVisuals
{
	/** Красит меш в один цвет (матовый материал движка с параметром цвета). Работает на каждой машине отдельно. */
	OBSHAGA_API void Tint(UStaticMeshComponent* Mesh, const FLinearColor& Color);

	/**
	 * Надевает на куб-заглушку настоящую модель: модель растягивается ровно по размерам куба, красится в один цвет,
	 * а сам куб перестаёт рисоваться (столкновения и взаимодействие остаются за ним — игра не меняется).
	 * YawDegrees — на сколько повернуть модель (кратно 90), если её «лицо» смотрит не туда.
	 */
	OBSHAGA_API void Dress(UStaticMeshComponent* Cube, UStaticMesh* Model, float YawDegrees, const FLinearColor& Color);
}
