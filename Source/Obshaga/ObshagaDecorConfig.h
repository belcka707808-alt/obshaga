#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "RoomVolume.h"
#include "ObshagaDecorConfig.generated.h"

class UStaticMesh;

/** Куда в комнате ставится украшение. */
UENUM(BlueprintType)
enum class EDecorPlace : uint8
{
	/** На полу у стены, спиной к ней: шкаф, растение, стул, торшер. */
	Wall,
	/** На стене на высоте MountHeight: зеркало, бра, навесной шкафчик. */
	WallHigh,
	/** На полу посередине комнаты: ковёр. */
	Center,
};

/** Одно украшение комнаты. Только картинка: без столкновений, на игру не влияет. */
USTRUCT(BlueprintType)
struct FDecorEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Decor")
	TObjectPtr<UStaticMesh> Model;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Decor")
	EDecorPlace Place = EDecorPlace::Wall;

	/** Во сколько раз уменьшить модель. Модели набора Kenney в пять раз крупнее настоящих вещей. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Decor")
	float Scale = 0.2f;

	/** Сколько таких поставить в одной комнате. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Decor")
	int32 Count = 1;

	/** Что стоит перед этим предметом лицом к нему: стул перед столом. Пусто — ничего. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Decor")
	TObjectPtr<UStaticMesh> FrontModel;

	/** Что стоит на этом предмете сверху: лампа или книги на столе. Пусто — ничего. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Decor")
	TObjectPtr<UStaticMesh> TopModel;

	/** Для WallHigh: высота низа предмета над полом. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Decor", meta = (Units = "cm"))
	float MountHeight = 130.f;
};

/** Как выглядят комнаты одного типа: краска, свет и набор украшений. */
USTRUCT(BlueprintType)
struct FRoomStyle
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	ERoomType RoomType = ERoomType::Corridor;

	/** Цвета пола; комнаты одного типа берут их по очереди, чтобы не быть одинаковыми. Пусто — остаётся цвет зоны. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	TArray<FLinearColor> FloorColors;

	/** Цвета нижней, крашеной части стен (как в настоящей общаге); тоже по очереди. Пусто — стены не красятся. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	TArray<FLinearColor> WallColors;

	/** Верх стен — «побелка». */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	FLinearColor WallTopColor = FLinearColor(0.85f, 0.8f, 0.68f);

	/** Лампа под потолком. Яркость 0 — лампы нет. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	FLinearColor LampColor = FLinearColor(1.f, 0.82f, 0.55f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	float LampIntensity = 0.f;

	/** Окна с батареями на наружных стенах (по одному на стену). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	bool bWindows = true;

	/** Сколько плакатов повесить на стены. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	int32 NumPosters = 0;

	/** Доска объявлений с бумажками. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	bool bNoticeBoard = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	TArray<FDecorEntry> Decor;
};

/** Оформление общаги: всё, что делает комнаты цветными и обжитыми. Правится в редакторе, код не трогаем. */
UCLASS()
class OBSHAGA_API UObshagaDecorConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Оформление проекта (DA_Decor). Если ассета нет — пустое: комнаты останутся как были. */
	static const UObshagaDecorConfig* Get();
	const FRoomStyle* FindStyle(ERoomType RoomType) const;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Decor")
	TArray<FRoomStyle> Styles;

	/** До какой высоты стена покрашена. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paint", meta = (Units = "cm"))
	float PaintHeight = 125.f;

	/** Насколько краска стен и пола светится сама (0 — только отражает свет). Заменяет лампы: десяток ламп стоил 10 мс кадра. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paint")
	float PaintGlow = 0.18f;

	/** То же ночью, после отбоя. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paint")
	float PaintGlowAtNight = 0.05f;

	/** Ширина одного куска краски вдоль стены. Меньше — точнее обходит проёмы, но больше кусков. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paint", meta = (Units = "cm"))
	float PaintStep = 50.f;

	/** Свободное место, которое украшение оставляет перед мебелью, тайниками и местами появления игроков. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Decor", meta = (Units = "cm"))
	float KeepClear = 45.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Light", meta = (Units = "cm"))
	float LampHeight = 235.f;

	/** Украшения мельче этого размера на низком качестве графики не ставятся. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Decor", meta = (Units = "cm"))
	float SmallDecorSize = 90.f;

	/** Потолок там, где его нет на карте (второй этаж). Тени он не даёт, чтобы комнаты не потемнели. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paint")
	bool bCeilings = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paint")
	FLinearColor CeilingColor = FLinearColor(0.8f, 0.78f, 0.7f);

	/** Потолок красится без расчёта света: яркость днём и доля от неё ночью и при «пробках». */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paint")
	float CeilingBrightness = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paint")
	float CeilingNightScale = 0.18f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paint")
	float CeilingBlackoutScale = 0.3f;

	/** Цвет стекла окон днём и цвета плакатов (берутся по очереди). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paint")
	FLinearColor WindowColor = FLinearColor(0.55f, 0.8f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paint")
	TArray<FLinearColor> PosterColors = { FLinearColor(0.9f, 0.2f, 0.2f), FLinearColor(0.95f, 0.75f, 0.1f), FLinearColor(0.2f, 0.6f, 0.9f), FLinearColor(0.3f, 0.75f, 0.35f), FLinearColor(0.7f, 0.3f, 0.8f) };
};
