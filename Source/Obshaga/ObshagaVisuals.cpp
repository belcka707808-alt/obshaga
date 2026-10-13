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
	// унаследует размеры заглушки.
	const FBoxSphereBounds Bounds = Model->GetBounds();
	const FVector Size = (Bounds.BoxExtent * 2.f).ComponentMax(FVector(1.f));
	const FRotator Rotation(0.f, YawDegrees, 0.f);
	// Куб растянут неравномерно (шкаф, дверь), а движок сначала перемножает масштабы по осям и только потом
	// поворачивает. Поэтому при повороте на 90° или 270° растяжение куба по X и Y надо поменять местами,
	// иначе длина модели ляжет на ширину куба.
	const FVector Parent = Cube->GetRelativeScale3D().ComponentMax(FVector(KINDA_SMALL_NUMBER));
	const bool bSwap = (FMath::RoundToInt32(YawDegrees / 90.f) & 1) != 0;
	const FVector Scale(100.f * (bSwap ? Parent.Y : Parent.X) / (Parent.X * Size.X), 100.f * (bSwap ? Parent.X : Parent.Y) / (Parent.Y * Size.Y), 100.f / Size.Z);
	Visual->SetRelativeRotation(Rotation);
	Visual->SetRelativeScale3D(Scale);
	Visual->SetRelativeLocation(-Rotation.RotateVector(Bounds.Origin * Scale * Parent) / Parent);

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

UMaterialInstanceDynamic* ObshagaVisuals::PaintFlat(UStaticMeshComponent* Mesh, const FLinearColor& Color)
{
	UMaterialInterface* Base = Mesh ? LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Obshaga/Map/Furniture/M_Flat.M_Flat"), nullptr, LOAD_NoWarn) : nullptr;
	if (UMaterialInstanceDynamic* Material = Base ? Mesh->CreateDynamicMaterialInstance(0, Base) : nullptr)
	{
		Material->SetVectorParameterValue(TEXT("Color"), Color);
		return Material;
	}
	Tint(Mesh, Color);
	return nullptr;
}

void ObshagaVisuals::Emblem(UStaticMeshComponent* Cube, FName ItemId)
{
	if (!Cube)
	{
		return;
	}

	// Детали — дочерние фигуры движка. Их положение и размер заданы в долях куба (куб — 100 единиц по каждой стороне),
	// поэтому они растягиваются вместе с ним и подходят к любому размеру предмета.
	auto AddPart = [Cube](const TCHAR* MeshPath, const FVector& Location, const FVector& Scale, const FLinearColor& PartColor)
	{
		UStaticMesh* PartMesh = LoadObject<UStaticMesh>(nullptr, MeshPath);
		if (!PartMesh)
		{
			return;
		}
		UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(Cube->GetOwner());
		Part->SetStaticMesh(PartMesh);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCastShadow(false);
		Part->SetupAttachment(Cube);
		Part->RegisterComponent();
		Part->SetRelativeLocation(Location);
		Part->SetRelativeScale3D(Scale);
		Tint(Part, PartColor);
	};
	static const TCHAR* CubePath = TEXT("/Engine/BasicShapes/Cube.Cube");
	static const TCHAR* CylinderPath = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");

	const FString Id = ItemId.ToString();
	if (Id.Contains(TEXT("Note")))
	{
		// Лист бумаги с тремя строчками.
		Tint(Cube, FLinearColor(0.92f, 0.9f, 0.8f));
		const FLinearColor Ink(0.15f, 0.15f, 0.3f);
		AddPart(CubePath, FVector(0.f, -26.f, 46.f), FVector(0.72f, 0.07f, 0.2f), Ink);
		AddPart(CubePath, FVector(0.f, 0.f, 46.f), FVector(0.72f, 0.07f, 0.2f), Ink);
		AddPart(CubePath, FVector(-12.f, 26.f, 46.f), FVector(0.48f, 0.07f, 0.2f), Ink);
	}
	else if (Id.Contains(TEXT("Envelope")))
	{
		// Конверт: светлый, с тёмным клапаном и красной печатью.
		Tint(Cube, FLinearColor(0.85f, 0.74f, 0.5f));
		AddPart(CubePath, FVector(0.f, -27.f, 44.f), FVector(0.98f, 0.44f, 0.25f), FLinearColor(0.6f, 0.48f, 0.28f));
		AddPart(CylinderPath, FVector(0.f, -5.f, 50.f), FVector(0.18f, 0.28f, 0.3f), FLinearColor(0.75f, 0.1f, 0.1f));
	}
	else if (Id.Contains(TEXT("Key")))
	{
		// Ключ: кольцо, стержень и два зубца. Сам куб остаётся только для столкновений.
		const FLinearColor Brass(0.9f, 0.68f, 0.12f);
		AddPart(CylinderPath, FVector(-30.f, 0.f, 0.f), FVector(0.4f, 1.f, 0.6f), Brass);
		AddPart(CubePath, FVector(18.f, 0.f, 0.f), FVector(0.62f, 0.3f, 0.5f), Brass);
		AddPart(CubePath, FVector(40.f, -30.f, 0.f), FVector(0.12f, 0.4f, 0.5f), Brass);
		AddPart(CubePath, FVector(22.f, -30.f, 0.f), FVector(0.12f, 0.4f, 0.5f), Brass);
		Cube->SetVisibility(false, false);
	}
}

UMaterialInstanceDynamic* ObshagaVisuals::Paint(UStaticMeshComponent* Mesh, const FLinearColor& Color, float Glow)
{
	UMaterialInterface* Base = Mesh ? LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Obshaga/Map/Furniture/M_Paint.M_Paint"), nullptr, LOAD_NoWarn) : nullptr;
	UMaterialInstanceDynamic* Material = Base ? Mesh->CreateDynamicMaterialInstance(0, Base) : nullptr;
	if (!Material)
	{
		Tint(Mesh, Color);
		return nullptr;
	}
	Material->SetVectorParameterValue(TEXT("Color"), Color);
	Material->SetScalarParameterValue(TEXT("Glow"), Glow);
	return Material;
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
