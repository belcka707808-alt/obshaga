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
	 * Красит «светящейся» краской (материал M_Paint): цвет виден и в тени, без единой лампы.
	 * Glow — доля собственного свечения. Возвращает материал, чтобы свечение можно было менять (ночью оно слабее).
	 * Если M_Paint нет, красит обычной краской и возвращает nullptr.
	 */
	OBSHAGA_API class UMaterialInstanceDynamic* Paint(UStaticMeshComponent* Mesh, const FLinearColor& Color, float Glow);

	/**
	 * Надевает на куб-заглушку настоящую модель: модель растягивается ровно по размерам куба (если у неё нет своих
	 * материалов — красится в один цвет),
	 * а сам куб перестаёт рисоваться (столкновения и взаимодействие остаются за ним — игра не меняется).
	 * YawDegrees — на сколько повернуть модель (кратно 90), если её «лицо» смотрит не туда.
	 */
	OBSHAGA_API void Dress(UStaticMeshComponent* Cube, UStaticMesh* Model, float YawDegrees, const FLinearColor& Color);
}
