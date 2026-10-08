#include "ObshagaVisuals.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

void ObshagaVisuals::Dress(UStaticMeshComponent* Cube, UStaticMesh* Model, float YawDegrees, const FLinearColor& Color)
{
	if (!Cube || !Model)
	{
		return;
	}

	static const FName VisualName(TEXT("Model"));
	UStaticMeshComponent* Visual = FindObject<UStaticMeshComponent>(Cube->GetOwner(), *VisualName.ToString());
	if (!Visual)
	{
		Visual = NewObject<UStaticMeshComponent>(Cube->GetOwner(), VisualName);
		Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Visual->SetupAttachment(Cube);
		Visual->RegisterComponent();
	}
	Visual->SetStaticMesh(Model);

	// Куб движка — 100 см по каждой стороне в своих координатах; модель вписываем в него, тогда она
	// унаследует размеры заглушки. Поворот кратен 90°, а куб одинаков по всем сторонам, так что модель из него не вылезет.
	const FBoxSphereBounds Bounds = Model->GetBounds();
	const FVector Size = (Bounds.BoxExtent * 2.f).ComponentMax(FVector(1.f));
	const FRotator Rotation(0.f, YawDegrees, 0.f);
	const FVector Scale(100.f / Size.X, 100.f / Size.Y, 100.f / Size.Z);
	Visual->SetRelativeRotation(Rotation);
	Visual->SetRelativeScale3D(Scale);
	Visual->SetRelativeLocation(-Rotation.RotateVector(Bounds.Origin * Scale));

	// У модели со своими материалами (родные цвета набора) ничего не трогаем; красим только «голые» слоты.
	for (int32 Slot = 0; Slot < Visual->GetNumMaterials(); ++Slot)
	{
		if (Model->GetMaterial(Slot))
		{
			continue;
		}
		UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		if (UMaterialInstanceDynamic* Material = Base ? Visual->CreateDynamicMaterialInstance(Slot, Base) : nullptr)
		{
			Material->SetVectorParameterValue(TEXT("Color"), Color);
		}
	}

	// Сам куб больше не рисуем, но его дочерняя модель остаётся видимой.
	Cube->SetVisibility(false, false);
}

void ObshagaVisuals::Tint(UStaticMeshComponent* Mesh, const FLinearColor& Color)
{
	if (!Mesh)
	{
		return;
	}

	// Материал базовых фигур движка: у него есть параметр «Color», своих ассетов для покраски не нужно.
	static const TCHAR* BaseMaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");
	UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0));
	if (!Material)
	{
		UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, BaseMaterialPath);
		Material = Base ? Mesh->CreateDynamicMaterialInstance(0, Base) : nullptr;
	}
	if (Material)
	{
		Material->SetVectorParameterValue(TEXT("Color"), Color);
	}
}
