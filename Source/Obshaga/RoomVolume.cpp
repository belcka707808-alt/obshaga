#include "RoomVolume.h"

#include "ObshagaDecorConfig.h"
#include "ObshagaGameState.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TimerManager.h"
#include "ObshagaVisuals.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/PlayerStart.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace
{
	// «Коврик» зоны приподнят над полом на волосок (чтобы не мерцал, но и не закрывал записку и конверт, лежащие на полу)
	// и чуть меньше зоны (чтобы не лез под стены).
	constexpr float FloorTintLift = 0.4f;
	constexpr float FloorTintMargin = 24.f;

	// Поиск стен: луч идёт изнутри комнаты наружу через границу зоны.
	constexpr float WallTraceInset = 90.f;
	constexpr float WallTraceReach = 110.f;
	// Краска — тонкий лист, прижатый к стене.
	constexpr float PaintThickness = 1.2f;
	constexpr float MaxWallHeight = 400.f;
	constexpr float RugLift = 1.2f;
	constexpr float GlowUpdateSeconds = 0.3f;
	constexpr float FloorProbeUp = 120.f;
	constexpr float FloorProbeDown = 60.f;

	/** Одно место у стены шириной в шаг краски. */
	struct FWallSlot
	{
		FVector LowPoint = FVector::ZeroVector;
		FVector HighPoint = FVector::ZeroVector;
		bool bLow = false;
		bool bHigh = false;
	};

	/** Одна из четырёх сторон зоны. */
	struct FWallSide
	{
		FVector Origin;
		FVector Tangent;
		FVector Normal;
		float Length = 0.f;
		TArray<FWallSlot> Slots;
	};

	/** Ищет стену серого макета; мебель, двери и игроков луч проходит насквозь. */
	bool TraceWall(const UWorld* World, const FVector& Start, const FVector& End, const FVector& InwardNormal, FVector& OutPoint)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(RoomDecor), false);
		for (int32 Try = 0; Try < 6; ++Try)
		{
			FHitResult Hit;
			if (!World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params) || !Hit.GetActor())
			{
				return false;
			}
			if (Hit.GetActor()->IsA<AStaticMeshActor>())
			{
				OutPoint = Hit.Location;
				return FVector::DotProduct(Hit.ImpactNormal, InwardNormal) > 0.9f;
			}
			Params.AddIgnoredActor(Hit.GetActor());
		}
		return false;
	}
}

ARoomVolume::ARoomVolume()
{
	PrimaryActorTick.bCanEverTick = false;

	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	Box->SetBoxExtent(FVector(200.f, 200.f, 150.f));
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Box->SetCollisionObjectType(ECC_WorldStatic);
	Box->SetCollisionResponseToAllChannels(ECR_Ignore);
	Box->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Box->SetGenerateOverlapEvents(true);
	Box->SetCanEverAffectNavigation(false);
	RootComponent = Box;
}

void ARoomVolume::BeginPlay()
{
	Super::BeginPlay();

	// Оформление зоны: комнаты одного типа берут цвета по очереди (по порядку имён), чтобы не быть одинаковыми.
	const FRoomStyle* Style = UObshagaDecorConfig::Get()->FindStyle(RoomType);
	int32 Ordinal = 0;
	for (TActorIterator<ARoomVolume> It(GetWorld()); It; ++It)
	{
		Ordinal += It->RoomType == RoomType && It->RoomId.LexicalLess(RoomId) ? 1 : 0;
	}
	if (Style && GetNetMode() != NM_DedicatedServer)
	{
		if (!Style->FloorColors.IsEmpty())
		{
			ZoneColor = Style->FloorColors[Ordinal % Style->FloorColors.Num()];
		}
		Decorate(*Style, Ordinal);
	}

	// Цветной «коврик» на весь пол зоны: по цвету пола игрок сразу понимает, где он. Только картинка, без столкновений.
	UStaticMesh* PlaneMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (ZoneColor.A <= 0.f || !PlaneMesh)
	{
		return;
	}

	const FBox Bounds = Box->Bounds.GetBox();
	const FVector Size = Bounds.GetSize();
	const float FloorZ = FindFloorZ();
	UStaticMeshComponent* FloorTint = NewObject<UStaticMeshComponent>(this, TEXT("FloorTint"));
	FloorTint->SetStaticMesh(PlaneMesh);
	FloorTint->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FloorTint->SetCastShadow(false);
	FloorTint->SetUsingAbsoluteLocation(true);
	FloorTint->SetUsingAbsoluteRotation(true);
	FloorTint->SetUsingAbsoluteScale(true);
	FloorTint->SetupAttachment(Box);
	FloorTint->RegisterComponent();
	FloorTint->SetWorldLocation(FVector(Bounds.GetCenter().X, Bounds.GetCenter().Y, FloorZ + FloorTintLift));
	FloorTint->SetWorldScale3D(FVector((Size.X - FloorTintMargin) / 100.f, (Size.Y - FloorTintMargin) / 100.f, 1.f));
	if (UMaterialInstanceDynamic* Material = ObshagaVisuals::Paint(FloorTint, ZoneColor, UObshagaDecorConfig::Get()->PaintGlow))
	{
		GlowMaterials.Add(Material);
	}
	if (!GlowMaterials.IsEmpty())
	{
		GetWorldTimerManager().SetTimer(GlowTimer, this, &ARoomVolume::UpdateGlow, GlowUpdateSeconds, true);
	}
}

ARoomVolume* ARoomVolume::FindRoomAt(const UObject* WorldContextObject, const FVector& Location)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World)
	{
		return nullptr;
	}

	for (TActorIterator<ARoomVolume> It(World); It; ++It)
	{
		if (It->Box->Bounds.GetBox().IsInsideOrOn(Location))
		{
			return *It;
		}
	}
	return nullptr;
}

ARoomVolume* ARoomVolume::FindRoomById(const UObject* WorldContextObject, FName InRoomId)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World || InRoomId.IsNone())
	{
		return nullptr;
	}

	for (TActorIterator<ARoomVolume> It(World); It; ++It)
	{
		if (It->RoomId == InRoomId)
		{
			return *It;
		}
	}
	return nullptr;
}

float ARoomVolume::FindFloorZ() const
{
	// Низ зоны не всегда лежит точно на полу, поэтому пол ищем лучом вниз из середины зоны.
	const FBox Bounds = Box->Bounds.GetBox();
	const FVector Center = Bounds.GetCenter();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(RoomFloor), false);
	for (int32 Try = 0; Try < 6; ++Try)
	{
		FHitResult Hit;
		if (!GetWorld()->LineTraceSingleByChannel(Hit, FVector(Center.X, Center.Y, Bounds.Min.Z + FloorProbeUp), FVector(Center.X, Center.Y, Bounds.Min.Z - FloorProbeDown), ECC_Visibility, Params) || !Hit.GetActor())
		{
			break;
		}
		if (Hit.GetActor()->IsA<AStaticMeshActor>())
		{
			return Hit.Location.Z;
		}
		Params.AddIgnoredActor(Hit.GetActor());
	}
	return Bounds.Min.Z;
}

void ARoomVolume::UpdateGlow()
{
	const AObshagaGameState* GameState = GetWorld()->GetGameState<AObshagaGameState>();
	const float Darkness = GameState ? GameState->GetDarkness() : 0.f;
	if (FMath::IsNearlyEqual(Darkness, AppliedDarkness, 0.01f))
	{
		return;
	}
	AppliedDarkness = Darkness;
	const UObshagaDecorConfig* Config = UObshagaDecorConfig::Get();
	const float Glow = FMath::Lerp(Config->PaintGlow, Config->PaintGlowAtNight, Darkness);
	for (UMaterialInstanceDynamic* Material : GlowMaterials)
	{
		Material->SetScalarParameterValue(TEXT("Glow"), Glow);
	}
}

void ARoomVolume::Decorate(const FRoomStyle& Style, int32 Ordinal)
{
	const UObshagaDecorConfig* Config = UObshagaDecorConfig::Get();
	const UWorld* World = GetWorld();
	const FBox Bounds = Box->Bounds.GetBox();
	const FVector Size = Bounds.GetSize();
	const float Floor = FindFloorZ();
	const float Step = FMath::Max(Config->PaintStep, 10.f);

	// 1. Где у зоны настоящие стены. Проёмы дверей и открытые стороны остаются пустыми.
	TArray<FWallSide> Sides;
	Sides.Add({ FVector(Bounds.Min.X, Bounds.Min.Y, Floor), FVector(0.f, 1.f, 0.f), FVector(1.f, 0.f, 0.f), static_cast<float>(Size.Y) });
	Sides.Add({ FVector(Bounds.Max.X, Bounds.Min.Y, Floor), FVector(0.f, 1.f, 0.f), FVector(-1.f, 0.f, 0.f), static_cast<float>(Size.Y) });
	Sides.Add({ FVector(Bounds.Min.X, Bounds.Min.Y, Floor), FVector(1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f), static_cast<float>(Size.X) });
	Sides.Add({ FVector(Bounds.Min.X, Bounds.Max.Y, Floor), FVector(1.f, 0.f, 0.f), FVector(0.f, -1.f, 0.f), static_cast<float>(Size.X) });

	const float LowZ = Config->PaintHeight * 0.5f;
	const float HighZ = Config->PaintHeight + 60.f;
	float WallTop = HighZ + 20.f;
	bool bTopMeasured = false;
	for (FWallSide& Side : Sides)
	{
		const int32 NumSlots = FMath::FloorToInt32(Side.Length / Step);
		const float Offset = (Side.Length - NumSlots * Step) * 0.5f;
		Side.Slots.SetNum(NumSlots);
		for (int32 Index = 0; Index < NumSlots; ++Index)
		{
			FWallSlot& Slot = Side.Slots[Index];
			const FVector OnBorder = Side.Origin + Side.Tangent * (Offset + (Index + 0.5f) * Step);
			const FVector Start = OnBorder + Side.Normal * WallTraceInset;
			const FVector End = OnBorder - Side.Normal * WallTraceReach;
			Slot.bLow = TraceWall(World, Start + FVector(0.f, 0.f, LowZ), End + FVector(0.f, 0.f, LowZ), Side.Normal, Slot.LowPoint);
			Slot.bHigh = TraceWall(World, Start + FVector(0.f, 0.f, HighZ), End + FVector(0.f, 0.f, HighZ), Side.Normal, Slot.HighPoint);

			// Высоту стен меряем один раз: поднимаем луч, пока он упирается в стену.
			if (Slot.bHigh && !bTopMeasured)
			{
				bTopMeasured = true;
				FVector Unused;
				for (float Z = HighZ + 20.f; Z <= MaxWallHeight && TraceWall(World, Start + FVector(0.f, 0.f, Z), End + FVector(0.f, 0.f, Z), Side.Normal, Unused); Z += 20.f)
				{
					WallTop = Z + 10.f;
				}
			}
		}
	}

	// 2. Краска: низ цветной, верх «побелка». Все куски одного цвета — один компонент, рисуется за один проход.
	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh && !Style.WallColors.IsEmpty())
	{
		auto MakeSheet = [this, CubeMesh](const TCHAR* Name, const FLinearColor& SheetColor)
		{
			UInstancedStaticMeshComponent* Sheet = NewObject<UInstancedStaticMeshComponent>(this, Name);
			Sheet->SetStaticMesh(CubeMesh);
			Sheet->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Sheet->SetCastShadow(false);
			Sheet->SetupAttachment(Box);
			Sheet->RegisterComponent();
			if (UMaterialInstanceDynamic* Material = ObshagaVisuals::Paint(Sheet, SheetColor, UObshagaDecorConfig::Get()->PaintGlow))
			{
				GlowMaterials.Add(Material);
			}
			return Sheet;
		};
		UInstancedStaticMeshComponent* LowSheet = MakeSheet(TEXT("WallPaint"), Style.WallColors[Ordinal % Style.WallColors.Num()]);
		UInstancedStaticMeshComponent* HighSheet = MakeSheet(TEXT("WallWhitewash"), Style.WallTopColor);
		const float HighHeight = FMath::Max(WallTop - Config->PaintHeight, 10.f);
		for (const FWallSide& Side : Sides)
		{
			const FRotator Rotation = Side.Normal.Rotation();
			for (const FWallSlot& Slot : Side.Slots)
			{
				if (Slot.bLow)
				{
					const FVector Location(Slot.LowPoint.X + Side.Normal.X * PaintThickness, Slot.LowPoint.Y + Side.Normal.Y * PaintThickness, Floor + Config->PaintHeight * 0.5f);
					LowSheet->AddInstance(FTransform(Rotation, Location, FVector(PaintThickness / 100.f, Step / 100.f, Config->PaintHeight / 100.f)), true);
				}
				if (Slot.bHigh)
				{
					const FVector Location(Slot.HighPoint.X + Side.Normal.X * PaintThickness, Slot.HighPoint.Y + Side.Normal.Y * PaintThickness, Floor + Config->PaintHeight + HighHeight * 0.5f);
					HighSheet->AddInstance(FTransform(Rotation, Location, FVector(PaintThickness / 100.f, Step / 100.f, HighHeight / 100.f)), true);
				}
			}
		}
	}

	// 3. Лампа: тёплый свет без теней (тени от десятка ламп слабая видеокарта не потянет).
	if (Style.LampIntensity > 0.f)
	{
		UPointLightComponent* Lamp = NewObject<UPointLightComponent>(this, TEXT("Lamp"));
		Lamp->bUseInverseSquaredFalloff = false;
		Lamp->SetupAttachment(Box);
		Lamp->RegisterComponent();
		Lamp->SetWorldLocation(FVector(Bounds.GetCenter().X, Bounds.GetCenter().Y, Floor + Config->LampHeight));
		Lamp->SetLightFalloffExponent(2.f);
		Lamp->SetIntensity(Style.LampIntensity);
		Lamp->SetLightColor(Style.LampColor);
		Lamp->SetCastShadows(false);
		Lamp->SetAttenuationRadius(FMath::Max(Size.X, Size.Y) * 0.75f);
	}

	// 4. Украшения. Случайность зависит только от имени комнаты, поэтому у всех игроков расстановка одна и та же.
	FRandomStream Random(GetTypeHash(RoomId));
	TArray<FBox> Used;
	TArray<FVector> Starts;
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		Starts.Add(It->GetActorLocation());
	}

	auto IsFree = [&](const FBox& Wanted)
	{
		const FBox Roomy = Wanted.ExpandBy(FVector(Config->KeepClear, Config->KeepClear, 0.f));
		for (const FBox& Other : Used)
		{
			if (Other.Intersect(Wanted))
			{
				return false;
			}
		}
		for (const FVector& Start : Starts)
		{
			if (Roomy.IsInsideXY(Start))
			{
				return false;
			}
		}
		// Мебель, тайники, приборы, двери, предметы: перед ними оставляем место. Стены макета и сами зоны не в счёт.
		TArray<FOverlapResult> Overlaps;
		World->OverlapMultiByObjectType(Overlaps, Roomy.GetCenter(), FQuat::Identity, FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllObjects), FCollisionShape::MakeBox(Roomy.GetExtent()));
		for (const FOverlapResult& Overlap : Overlaps)
		{
			const AActor* Other = Overlap.GetActor();
			if (Other && !Other->IsA<ARoomVolume>() && !Other->IsA<AStaticMeshActor>() && !Other->IsA<APawn>())
			{
				return false;
			}
		}
		return true;
	};

	auto Spawn = [this](const FDecorEntry& Entry, float Scale, const FVector& BottomCenter, float Yaw)
	{
		const FBoxSphereBounds ModelBounds = Entry.Model->GetBounds();
		const FRotator Rotation(0.f, Yaw, 0.f);
		UStaticMeshComponent* Item = NewObject<UStaticMeshComponent>(this);
		Item->SetStaticMesh(Entry.Model);
		Item->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Item->SetCastShadow(false);
		Item->SetUsingAbsoluteLocation(true);
		Item->SetUsingAbsoluteRotation(true);
		Item->SetUsingAbsoluteScale(true);
		Item->SetupAttachment(Box);
		Item->RegisterComponent();
		// У моделей набора точка отсчёта в углу, поэтому ставим по центру низа.
		const FVector Pivot(ModelBounds.Origin.X, ModelBounds.Origin.Y, ModelBounds.Origin.Z - ModelBounds.BoxExtent.Z);
		Item->SetWorldScale3D(FVector(Scale));
		Item->SetWorldRotation(Rotation);
		Item->SetWorldLocation(BottomCenter - Rotation.RotateVector(Pivot * Scale));
	};

	for (const FDecorEntry& Entry : Style.Decor)
	{
		if (!Entry.Model)
		{
			continue;
		}
		const FVector ModelSize = Entry.Model->GetBounds().BoxExtent * 2.f * Entry.Scale;

		if (Entry.Place == EDecorPlace::Center)
		{
			// Ковёр: длинной стороной вдоль длинной стороны комнаты, не больше половины пола.
			const bool bTurn = (Size.Y > Size.X) != (ModelSize.Y > ModelSize.X);
			const float AlongX = bTurn ? ModelSize.Y : ModelSize.X;
			const float AlongY = bTurn ? ModelSize.X : ModelSize.Y;
			const float Fit = FMath::Min3(1.f, static_cast<float>(Size.X) * 0.55f / FMath::Max(AlongX, 1.f), static_cast<float>(Size.Y) * 0.55f / FMath::Max(AlongY, 1.f));
			Spawn(Entry, Entry.Scale * Fit, FVector(Bounds.GetCenter().X, Bounds.GetCenter().Y, Floor + RugLift), bTurn ? 90.f : 0.f);
			continue;
		}

		const bool bHigh = Entry.Place == EDecorPlace::WallHigh;
		const float Width = ModelSize.X;
		const float Depth = ModelSize.Y;
		const int32 Span = FMath::CeilToInt32(FMath::Max(Width * 0.5f / Step - 0.5f, 0.f));
		for (int32 Copy = 0; Copy < Entry.Count; ++Copy)
		{
			// Перебираем места у стен в случайном порядке и берём первое свободное.
			TArray<TPair<int32, int32>> Candidates;
			for (int32 SideIndex = 0; SideIndex < Sides.Num(); ++SideIndex)
			{
				for (int32 SlotIndex = Span; SlotIndex < Sides[SideIndex].Slots.Num() - Span; ++SlotIndex)
				{
					Candidates.Emplace(SideIndex, SlotIndex);
				}
			}
			for (int32 Index = Candidates.Num() - 1; Index > 0; --Index)
			{
				Candidates.Swap(Index, Random.RandRange(0, Index));
			}

			for (const TPair<int32, int32>& Candidate : Candidates)
			{
				const FWallSide& Side = Sides[Candidate.Key];
				bool bWallBehind = true;
				for (int32 SlotIndex = Candidate.Value - Span; SlotIndex <= Candidate.Value + Span && bWallBehind; ++SlotIndex)
				{
					bWallBehind = bHigh ? Side.Slots[SlotIndex].bHigh : Side.Slots[SlotIndex].bLow;
				}
				if (!bWallBehind)
				{
					continue;
				}

				const FWallSlot& Slot = Side.Slots[Candidate.Value];
				const FVector OnWall = bHigh ? Slot.HighPoint : Slot.LowPoint;
				const FVector BottomCenter(OnWall.X + Side.Normal.X * (Depth * 0.5f + 3.f), OnWall.Y + Side.Normal.Y * (Depth * 0.5f + 3.f), Floor + (bHigh ? Entry.MountHeight : 0.f));
				const FVector HalfSize = (Side.Tangent * Width * 0.5f + Side.Normal * Depth * 0.5f).GetAbs() + FVector(0.f, 0.f, ModelSize.Z * 0.5f);
				const FVector BoxCenter = BottomCenter + FVector(0.f, 0.f, ModelSize.Z * 0.5f);
				const FBox Wanted(BoxCenter - HalfSize, BoxCenter + HalfSize);
				if (!IsFree(Wanted))
				{
					continue;
				}

				// Модели набора смотрят «лицом» в сторону -Y; поворачиваем лицом в комнату.
				Spawn(Entry, Entry.Scale, BottomCenter, Side.Normal.Rotation().Yaw + 90.f);
				Used.Add(Wanted.ExpandBy(6.f));
				break;
			}
		}
	}
}
