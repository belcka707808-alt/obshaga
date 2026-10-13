#include "RoomVolume.h"

#include "DoorActor.h"
#include "Obshaga.h"

#include "ObshagaDecorConfig.h"
#include "ObshagaGameState.h"
#include "ObshagaSettings.h"
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
	// Стены красим не выше одного этажа: зона лестницы тянется на оба, и её побелка ложилась поверх краски коридора второго этажа.
	constexpr float MaxWallHeight = 300.f;
	constexpr float RugLift = 1.2f;
	// Насколько стул задвинут под стол.
	constexpr float FrontTuck = 12.f;
	constexpr float GlowUpdateSeconds = 0.3f;
	// Как часто проверяем, не въехала ли камера в украшение, и на каком расстоянии от него оно прячется.
	constexpr float CameraCheckSeconds = 0.1f;
	constexpr float CameraHideDistance = 35.f;
	// Краска не подходит к дверному проёму ближе этого расстояния.
	constexpr float DoorwayClearance = 6.f;
	// На сколько коробка дверного проёма выходит по обе стороны стены: всё, что ближе, считается «в проёме».
	constexpr float DoorwayHalfDepth = 60.f;
	// Перемычку над дверью красим, только если она выше этого.
	constexpr float MinLintelHeight = 15.f;
	constexpr float FloorProbeUp = 120.f;
	constexpr float FloorProbeDown = 60.f;

	/** Одно место у стены шириной в шаг краски. */
	struct FWallSlot
	{
		FVector LowPoint = FVector::ZeroVector;
		FVector HighPoint = FVector::ZeroVector;
		bool bLow = false;
		bool bHigh = false;
		/** Над этим местом перемычка двери: побелка только выше DoorTop (высота над полом). */
		bool bLintel = false;
		float DoorTop = 0.f;
		/** Кусок попал в проём двери. */
		bool bDoor = false;
		/** На сколько кусок краски удлинён к соседней стене (угол, колонна), чтобы у неё не оставалось голой щели: [низ/верх][назад/вперёд вдоль стены]. */
		float Extend[2][2] = { { 0.f, 0.f }, { 0.f, 0.f } };
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
	if (!DecorItems.IsEmpty())
	{
		GetWorldTimerManager().SetTimer(CameraTimer, this, &ARoomVolume::HideDecorNearCamera, CameraCheckSeconds, true);
	}
	if (!GlowMaterials.IsEmpty() || !FlatMaterials.IsEmpty())
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

void ARoomVolume::HideDecorNearCamera()
{
	// Камера от третьего лица упирается только в стены и мебель; украшения она проходит насквозь, и вешалка
	// или торшер за спиной закрывали бы пол-экрана. Что оказалось вплотную к камере — на это время не рисуем.
	const APlayerController* Controller = GetWorld()->GetFirstPlayerController();
	if (!Controller || !Controller->PlayerCameraManager)
	{
		return;
	}
	const FVector Camera = Controller->PlayerCameraManager->GetCameraLocation();
	// Камера далеко от зоны — проверять каждое украшение незачем.
	const bool bNearZone = Box->Bounds.GetBox().ExpandBy(CameraHideDistance * 2.f).IsInsideOrOn(Camera);
	for (UStaticMeshComponent* Item : DecorItems)
	{
		const bool bHide = bNearZone && Item->Bounds.GetBox().ExpandBy(CameraHideDistance).IsInsideOrOn(Camera);
		if (Item->IsVisible() == bHide)
		{
			Item->SetVisibility(!bHide);
		}
	}
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
	// «Пробки» считаем добавкой к темноте, чтобы их начало и конец тоже перекрашивали потолок.
	const float Darkness = GameState ? GameState->GetDarkness() : 0.f;
	const float Key = Darkness + (GameState && GameState->IsBlackout() ? 10.f : 0.f);
	if (FMath::IsNearlyEqual(Key, AppliedDarkness, 0.01f))
	{
		return;
	}
	AppliedDarkness = Key;
	const UObshagaDecorConfig* Config = UObshagaDecorConfig::Get();
	const float Glow = FMath::Lerp(Config->PaintGlow, Config->PaintGlowAtNight, Darkness);
	for (UMaterialInstanceDynamic* Material : GlowMaterials)
	{
		Material->SetScalarParameterValue(TEXT("Glow"), Glow);
	}
	// Потолок светом не освещается, поэтому ночью темнеет вручную (при «пробках» — ещё сильнее).
	const float EventScale = GameState && GameState->IsBlackout() ? Config->CeilingBlackoutScale : 1.f;
	const FLinearColor Ceiling = FlatColor * Config->CeilingBrightness * FMath::Lerp(1.f, Config->CeilingNightScale, Darkness) * EventScale;
	for (UMaterialInstanceDynamic* Material : FlatMaterials)
	{
		Material->SetVectorParameterValue(TEXT("Color"), Ceiling);
	}
}

void ARoomVolume::Decorate(const FRoomStyle& Style, int32 Ordinal)
{
	const UObshagaDecorConfig* Config = UObshagaDecorConfig::Get();
	const UWorld* World = GetWorld();
	const FBox Bounds = Box->Bounds.GetBox();
	const FVector Size = Bounds.GetSize();
	const float Floor = FindFloorZ();
	const bool bLowQuality = ObshagaSettings::IsLowQuality();
	const float Step = FMath::Max(Config->PaintStep, 10.f);

	// 1. Где у зоны настоящие стены. Проёмы дверей и открытые стороны остаются пустыми.
	TArray<FWallSide> Sides;
	Sides.Add({ FVector(Bounds.Min.X, Bounds.Min.Y, Floor), FVector(0.f, 1.f, 0.f), FVector(1.f, 0.f, 0.f), static_cast<float>(Size.Y) });
	Sides.Add({ FVector(Bounds.Max.X, Bounds.Min.Y, Floor), FVector(0.f, 1.f, 0.f), FVector(-1.f, 0.f, 0.f), static_cast<float>(Size.Y) });
	Sides.Add({ FVector(Bounds.Min.X, Bounds.Min.Y, Floor), FVector(1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f), static_cast<float>(Size.X) });
	Sides.Add({ FVector(Bounds.Min.X, Bounds.Max.Y, Floor), FVector(1.f, 0.f, 0.f), FVector(0.f, -1.f, 0.f), static_cast<float>(Size.X) });

	// Двери этой зоны. На лучи тут полагаться нельзя (луч проходит створку насквозь и может упереться в косяк или
	// перемычку), поэтому проём берётся прямо у двери: в нём не бывает ни краски, ни украшений.
	TArray<FBox> Doorways;
	TArray<FBox> DoorSwings;
	for (TActorIterator<ADoorActor> It(World); It; ++It)
	{
		const FBox Doorway = It->GetDoorwayBox(DoorwayClearance, DoorwayHalfDepth);
		if (Doorway.Intersect(Bounds.ExpandBy(WallTraceReach)) && FMath::Abs(Doorway.Min.Z - Floor) < 100.f)
		{
			Doorways.Add(Doorway);
			DoorSwings.Add(It->GetSwingBox());
		}
	}
	int32 NumDoorCuts = 0;

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
			// Стена должна быть под всей шириной куска, а не только под его серединой:
			// иначе кусок у дверного проёма залезал бы в проём на полширины и «сужал» дверь.
			auto TraceBand = [&](float Z, FVector& OutPoint)
			{
				FVector Edge;
				const FVector Up(0.f, 0.f, Z);
				const FVector Half = Side.Tangent * (Step * 0.5f + DoorwayClearance);
				return TraceWall(World, Start + Up, End + Up, Side.Normal, OutPoint)
					&& TraceWall(World, Start + Up - Half, End + Up - Half, Side.Normal, Edge)
					&& TraceWall(World, Start + Up + Half, End + Up + Half, Side.Normal, Edge);
			};
			Slot.bLow = TraceBand(LowZ, Slot.LowPoint);
			Slot.bHigh = TraceBand(HighZ, Slot.HighPoint);

			// Кусок, который хоть краем заходит в проём двери, не красим вовсе; над дверью останется только перемычка.
			// Где стена найдена, меряем от неё (иначе дверь в соседней стене у самого угла срезала бы и эту);
			// где не найдена — от границы зоны с запасом на дальность луча.
			const bool bFound = Slot.bLow || Slot.bHigh;
			const FVector SlotCenter = bFound ? FVector(Slot.bLow ? Slot.LowPoint.X : Slot.HighPoint.X, Slot.bLow ? Slot.LowPoint.Y : Slot.HighPoint.Y, Floor) : OnBorder;
			const FVector SlotHalf = (Side.Tangent * Step * 0.5f + Side.Normal * (bFound ? 5.f : WallTraceReach)).GetAbs();
			const FBox SlotBox(SlotCenter - SlotHalf, SlotCenter + SlotHalf + FVector(0.f, 0.f, MaxWallHeight));
			for (const FBox& Doorway : Doorways)
			{
				if (Doorway.Intersect(SlotBox))
				{
					NumDoorCuts += (Slot.bLow ? 1 : 0) + (Slot.bHigh ? 1 : 0);
					Slot.bLow = false;
					Slot.bHigh = false;
					Slot.bLintel = true;
					Slot.bDoor = true;
					Slot.DoorTop = Doorway.Max.Z - Floor;
				}
			}

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

	// Перемычка над дверью: если над створкой есть стена, её белят от верха двери до потолка.
	int32 NumLintels = 0;
	for (FWallSide& Side : Sides)
	{
		const float Offset = (Side.Length - Side.Slots.Num() * Step) * 0.5f;
		for (int32 Index = 0; Index < Side.Slots.Num(); ++Index)
		{
			FWallSlot& Slot = Side.Slots[Index];
			if (!Slot.bLintel)
			{
				continue;
			}
			const FVector OnBorder = Side.Origin + Side.Tangent * (Offset + (Index + 0.5f) * Step);
			const FVector Up(0.f, 0.f, (Slot.DoorTop + WallTop) * 0.5f);
			Slot.bLintel = bTopMeasured && WallTop - Slot.DoorTop >= MinLintelHeight
				&& TraceWall(World, OnBorder + Side.Normal * WallTraceInset + Up, OnBorder - Side.Normal * WallTraceReach + Up, Side.Normal, Slot.HighPoint);
			NumLintels += Slot.bLintel ? 1 : 0;
		}
	}
	// Щели в углах: куски краски идут с шагом и до угла не дотягиваются. Крайний кусок (у которого сосед не покрашен
	// и это не дверь) дотягиваем до ближайшей поперечной стены или колонны, если она рядом.
	int32 NumExtended = 0;
	for (FWallSide& Side : Sides)
	{
		for (int32 Index = 0; Index < Side.Slots.Num(); ++Index)
		{
			FWallSlot& Slot = Side.Slots[Index];
			for (int32 Band = 0; Band < 2; ++Band)
			{
				if (!(Band == 0 ? Slot.bLow : Slot.bHigh))
				{
					continue;
				}
				for (int32 Way = 0; Way < 2; ++Way)
				{
					const int32 Next = Index + (Way == 0 ? -1 : 1);
					const FWallSlot* Neighbour = Side.Slots.IsValidIndex(Next) ? &Side.Slots[Next] : nullptr;
					if (Neighbour && ((Band == 0 ? Neighbour->bLow : Neighbour->bHigh) || Neighbour->bDoor))
					{
						continue;
					}
					const FVector Direction = Side.Tangent * (Way == 0 ? -1.f : 1.f);
					const FVector From = (Band == 0 ? Slot.LowPoint : Slot.HighPoint) + Side.Normal * 3.f;
					FVector Hit;
					if (TraceWall(World, From, From + Direction * Step * 2.f, -Direction, Hit))
					{
						Slot.Extend[Band][Way] = FMath::Max(FVector::Dist2D(From, Hit) - Step * 0.5f, 0.f);
						NumExtended += Slot.Extend[Band][Way] > 0.5f ? 1 : 0;
					}
				}
			}
		}
	}
	UE_LOG(LogObshaga, Log, TEXT("Decor %s: %d doors, %d paint pieces cut from doorways, %d lintel pieces, %d pieces stretched to a corner"), *RoomId.ToString(), Doorways.Num(), NumDoorCuts, NumLintels, NumExtended);

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
					const FVector Location = FVector(Slot.LowPoint.X + Side.Normal.X * PaintThickness, Slot.LowPoint.Y + Side.Normal.Y * PaintThickness, Floor + Config->PaintHeight * 0.5f)
						+ Side.Tangent * (Slot.Extend[0][1] - Slot.Extend[0][0]) * 0.5f;
					LowSheet->AddInstance(FTransform(Rotation, Location, FVector(PaintThickness / 100.f, (Step + Slot.Extend[0][0] + Slot.Extend[0][1]) / 100.f, Config->PaintHeight / 100.f)), true);
				}
				if (Slot.bHigh)
				{
					const FVector Location = FVector(Slot.HighPoint.X + Side.Normal.X * PaintThickness, Slot.HighPoint.Y + Side.Normal.Y * PaintThickness, Floor + Config->PaintHeight + HighHeight * 0.5f)
						+ Side.Tangent * (Slot.Extend[1][1] - Slot.Extend[1][0]) * 0.5f;
					HighSheet->AddInstance(FTransform(Rotation, Location, FVector(PaintThickness / 100.f, (Step + Slot.Extend[1][0] + Slot.Extend[1][1]) / 100.f, HighHeight / 100.f)), true);
				}
				if (Slot.bLintel)
				{
					const float LintelHeight = WallTop - Slot.DoorTop;
					const FVector Location(Slot.HighPoint.X + Side.Normal.X * PaintThickness, Slot.HighPoint.Y + Side.Normal.Y * PaintThickness, Floor + Slot.DoorTop + LintelHeight * 0.5f);
					HighSheet->AddInstance(FTransform(Rotation, Location, FVector(PaintThickness / 100.f, Step / 100.f, LintelHeight / 100.f)), true);
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
		// Там, где ходит створка двери, ничего не ставим: иначе дверь открывалась бы сквозь мебель.
		for (const FBox& Swing : DoorSwings)
		{
			if (Swing.Intersect(Roomy))
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
		DecorItems.Add(Item);
	};

	for (const FDecorEntry& Entry : Style.Decor)
	{
		if (!Entry.Model)
		{
			continue;
		}
		const FVector ModelSize = Entry.Model->GetBounds().BoxExtent * 2.f * Entry.Scale;
		// На низком качестве мелочь (горшки, книги, бра) не ставим — остаётся крупная мебель и ковёр.
		if (bLowQuality && Entry.Place != EDecorPlace::Center && ModelSize.GetMax() < Config->SmallDecorSize)
		{
			continue;
		}

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
		// Место нужно и тому, что стоит перед предметом (стул перед столом).
		const float FrontDepth = Entry.FrontModel ? Entry.FrontModel->GetBounds().BoxExtent.Y * 2.f * Entry.Scale : 0.f;
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
				const FVector HalfSize = (Side.Tangent * Width * 0.5f + Side.Normal * (Depth + FrontDepth) * 0.5f).GetAbs() + FVector(0.f, 0.f, ModelSize.Z * 0.5f);
				const FVector BoxCenter = BottomCenter + Side.Normal * (FrontDepth * 0.5f) + FVector(0.f, 0.f, ModelSize.Z * 0.5f);
				const FBox Wanted(BoxCenter - HalfSize, BoxCenter + HalfSize);
				if (!IsFree(Wanted))
				{
					continue;
				}

				// Модели набора смотрят «лицом» в сторону -Y; поворачиваем лицом в комнату.
				const float Yaw = Side.Normal.Rotation().Yaw + 90.f;
				Spawn(Entry, Entry.Scale, BottomCenter, Yaw);
				if (Entry.FrontModel)
				{
					// Стул: вплотную к столу, развёрнут к нему (и к стене) лицом, чуть задвинут под столешницу.
					FDecorEntry Front;
					Front.Model = Entry.FrontModel;
					Spawn(Front, Entry.Scale, BottomCenter + Side.Normal * (Depth * 0.5f + FrontDepth * 0.5f - FrontTuck), Yaw + 180.f);
				}
				if (Entry.TopModel && !bLowQuality)
				{
					// Лампа или книги: на столешнице, ближе к одному краю.
					FDecorEntry Top;
					Top.Model = Entry.TopModel;
					Spawn(Top, Entry.Scale, BottomCenter + Side.Tangent * (Width * 0.25f) + FVector(0.f, 0.f, ModelSize.Z), Yaw);
				}
				Used.Add(Wanted.ExpandBy(6.f));
				break;
			}
		}
	}

	// 5. Потолок, окна с батареями, плакаты и доска объявлений — плоские цветные детали из простых фигур.
	if (!CubeMesh)
	{
		return;
	}
	// Glow < 0 — деталь красится самой дешёвой краской без освещения (потолок).
	auto AddBlock = [this, CubeMesh](const FVector& Center, const FRotator& Rotation, const FVector& SizeCm, const FLinearColor& BlockColor, float Glow)
	{
		UStaticMeshComponent* Block = NewObject<UStaticMeshComponent>(this);
		Block->SetStaticMesh(CubeMesh);
		Block->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Block->SetCastShadow(false);
		Block->SetUsingAbsoluteLocation(true);
		Block->SetUsingAbsoluteRotation(true);
		Block->SetUsingAbsoluteScale(true);
		Block->SetupAttachment(Box);
		Block->RegisterComponent();
		Block->SetWorldLocation(Center);
		Block->SetWorldRotation(Rotation);
		Block->SetWorldScale3D(SizeCm / 100.f);
		if (Glow < 0.f)
		{
			if (UMaterialInstanceDynamic* Flat = ObshagaVisuals::PaintFlat(Block, BlockColor))
			{
				FlatMaterials.Add(Flat);
				FlatColor = BlockColor;
				AppliedDarkness = -1.f;
			}
		}
		else if (UMaterialInstanceDynamic* Material = ObshagaVisuals::Paint(Block, BlockColor, Glow))
		{
			GlowMaterials.Add(Material);
		}
	};

	// Потолок — только там, где над комнатой нет перекрытия.
	if (Config->bCeilings && bTopMeasured)
	{
		FVector Unused;
		const FVector Center(Bounds.GetCenter().X, Bounds.GetCenter().Y, Floor);
		const bool bHasCeiling = TraceWall(World, Center + FVector(0.f, 0.f, 120.f), Center + FVector(0.f, 0.f, MaxWallHeight + 80.f), FVector(0.f, 0.f, -1.f), Unused);
		if (!bHasCeiling)
		{
			AddBlock(Center + FVector(0.f, 0.f, WallTop + 2.f), FRotator::ZeroRotator, FVector(Size.X, Size.Y, 4.f), Config->CeilingColor, -1.f);
		}
	}

	// Плоская деталь на стене: Along — ширина вдоль стены, Height — высота, Bottom — низ над полом.
	auto WallItem = [&](const FWallSide& Side, const FWallSlot& Slot, float Along, float Height, float Bottom, float Thickness, const FLinearColor& ItemColor, float Glow, float Shift = 0.f)
	{
		const FVector OnWall = Slot.bHigh ? Slot.HighPoint : Slot.LowPoint;
		const FVector Center = FVector(OnWall.X, OnWall.Y, Floor + Bottom + Height * 0.5f) + Side.Normal * (PaintThickness * 2.f + Thickness * 0.5f) + Side.Tangent * Shift;
		AddBlock(Center, Side.Normal.Rotation(), FVector(Thickness, Along, Height), ItemColor, Glow);
	};
	// Свободен ли кусок стены под деталь (стена сплошная на всю ширину, и на ней ничего не висит).
	auto WallFree = [&](const FWallSide& Side, int32 SlotIndex, int32 HalfSpan, float Bottom, float Height, float Along, FBox& OutBox)
	{
		for (int32 Index = SlotIndex - HalfSpan; Index <= SlotIndex + HalfSpan; ++Index)
		{
			if (!Side.Slots.IsValidIndex(Index) || !Side.Slots[Index].bHigh || !Side.Slots[Index].bLow)
			{
				return false;
			}
		}
		const FVector OnWall = Side.Slots[SlotIndex].HighPoint;
		const FVector Center(OnWall.X + Side.Normal.X * 12.f, OnWall.Y + Side.Normal.Y * 12.f, Floor + Bottom + Height * 0.5f);
		const FVector HalfSize = (Side.Tangent * Along * 0.5f + Side.Normal * 12.f).GetAbs() + FVector(0.f, 0.f, Height * 0.5f);
		OutBox = FBox(Center - HalfSize, Center + HalfSize);
		for (const FBox& Swing : DoorSwings)
		{
			if (Swing.Intersect(OutBox))
			{
				return false;
			}
		}
		for (const FBox& Other : Used)
		{
			if (Other.Intersect(OutBox))
			{
				return false;
			}
		}
		return true;
	};

	// На низком качестве графики стены остаются голыми: окна, плакаты и доски — первое, чем жертвуем на слабых ноутбуках.
	if (bLowQuality)
	{
		return;
	}

	const int32 WideSpan = FMath::CeilToInt32(60.f / Step);
	for (const FWallSide& Side : Sides)
	{
		// Окно — на стене, за которой улица (за стеной нет другой зоны).
		if (!Style.bWindows || Side.Slots.Num() < WideSpan * 2 + 1)
		{
			continue;
		}
		const int32 Middle = Side.Slots.Num() / 2;
		for (int32 Offset = 0; Offset <= Middle; ++Offset)
		{
			const int32 SlotIndex = Middle + ((Offset % 2) ? Offset / 2 + 1 : -(Offset / 2));
			FBox WindowBox;
			if (!Side.Slots.IsValidIndex(SlotIndex) || !WallFree(Side, SlotIndex, WideSpan, 60.f, 170.f, 120.f, WindowBox))
			{
				continue;
			}
			const FWallSlot& Slot = Side.Slots[SlotIndex];
			if (ARoomVolume::FindRoomAt(this, Slot.HighPoint - Side.Normal * 200.f + FVector(0.f, 0.f, 50.f)))
			{
				break;
			}
			const FLinearColor Frame(0.92f, 0.9f, 0.85f);
			WallItem(Side, Slot, 124.f, 124.f, 108.f, 2.f, Frame, Config->PaintGlow);
			WallItem(Side, Slot, 54.f, 112.f, 114.f, 3.f, Config->WindowColor, 0.9f, -29.f);
			WallItem(Side, Slot, 54.f, 112.f, 114.f, 3.f, Config->WindowColor, 0.9f, 29.f);
			WallItem(Side, Slot, 136.f, 5.f, 102.f, 14.f, Frame, Config->PaintGlow);
			// Батарея под окном.
			WallItem(Side, Slot, 96.f, 46.f, 22.f, 9.f, FLinearColor(0.85f, 0.85f, 0.82f), Config->PaintGlow);
			for (int32 Rib = -3; Rib <= 3; ++Rib)
			{
				WallItem(Side, Slot, 4.f, 50.f, 20.f, 11.f, FLinearColor(0.7f, 0.7f, 0.68f), Config->PaintGlow, Rib * 14.f);
			}
			Used.Add(WindowBox);
			break;
		}
	}

	// Доска объявлений: коричневая доска и бумажки на ней.
	bool bBoardPlaced = !Style.bNoticeBoard;
	int32 PostersLeft = Style.NumPosters;
	for (int32 Try = 0; Try < 40 && (!bBoardPlaced || PostersLeft > 0); ++Try)
	{
		const FWallSide& Side = Sides[Random.RandRange(0, Sides.Num() - 1)];
		if (Side.Slots.IsEmpty())
		{
			continue;
		}
		const int32 SlotIndex = Random.RandRange(0, Side.Slots.Num() - 1);
		const FWallSlot& Slot = Side.Slots[SlotIndex];
		FBox ItemBox;
		if (!bBoardPlaced)
		{
			if (WallFree(Side, SlotIndex, FMath::CeilToInt32(80.f / Step), 110.f, 100.f, 160.f, ItemBox))
			{
				WallItem(Side, Slot, 160.f, 100.f, 110.f, 3.f, FLinearColor(0.45f, 0.28f, 0.14f), Config->PaintGlow);
				for (int32 Paper = 0; Paper < 6; ++Paper)
				{
					const FLinearColor PaperColor = Paper % 3 == 0 ? FLinearColor(1.f, 0.95f, 0.5f) : FLinearColor(0.95f, 0.95f, 0.92f);
					WallItem(Side, Slot, 26.f + 6.f * (Paper % 2), 30.f + 5.f * (Paper % 3), 120.f + 44.f * (Paper % 2) + Random.FRandRange(-4.f, 4.f), 4.5f, PaperColor, Config->PaintGlow,
						-55.f + 44.f * (Paper / 2) + Random.FRandRange(-5.f, 5.f));
				}
				Used.Add(ItemBox);
				bBoardPlaced = true;
			}
			continue;
		}
		if (WallFree(Side, SlotIndex, FMath::CeilToInt32(30.f / Step), 130.f, 80.f, 60.f, ItemBox) && !Config->PosterColors.IsEmpty())
		{
			// Плакат: цветной лист с белой полосой «подписи».
			const FLinearColor PosterColor = Config->PosterColors[Random.RandRange(0, Config->PosterColors.Num() - 1)];
			WallItem(Side, Slot, 56.f, 76.f, 132.f, 2.f, PosterColor, Config->PaintGlow);
			WallItem(Side, Slot, 40.f, 8.f, 142.f, 3.f, FLinearColor(0.95f, 0.95f, 0.9f), Config->PaintGlow);
			WallItem(Side, Slot, 30.f, 30.f, 164.f, 3.f, PosterColor * 0.5f, Config->PaintGlow);
			Used.Add(ItemBox);
			--PostersLeft;
		}
	}
}