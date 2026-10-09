#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RoomVolume.generated.h"

class UBoxComponent;

UENUM(BlueprintType)
enum class ERoomType : uint8
{
	Corridor,
	Vahta,
	Kitchen,
	LivingRoom,
	Bathroom,
	NightExit,
	Bedroom,
	Balcony,
	Stairs
};

/** Невидимая коробка, размечающая одну зону общаги. */
UCLASS()
class OBSHAGA_API ARoomVolume : public AActor
{
	GENERATED_BODY()

public:
	ARoomVolume();

	UBoxComponent* GetBox() const { return Box; }

	/** Комната, внутри которой лежит точка; nullptr, если точка вне всех комнат. */
	static ARoomVolume* FindRoomAt(const UObject* WorldContextObject, const FVector& Location);
	static ARoomVolume* FindRoomById(const UObject* WorldContextObject, FName InRoomId);

	/** Короткий код зоны для логики и заданий: Kitchen, Room201... */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Room")
	FName RoomId;

	/** Название для игрока: «Кухня», «Комната 201». */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Room")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Room")
	ERoomType RoomType = ERoomType::Corridor;

	/** Находиться здесь после отбоя — нарушение. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Room")
	bool bForbiddenAfterCurfew = false;

	/** Цвет пола зоны. Прозрачный (альфа 0) — пол не красится. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Room")
	FLinearColor ZoneColor = FLinearColor(0.f, 0.f, 0.f, 0.f);

protected:
	virtual void BeginPlay() override;

	/** Красит стены, вешает лампу и расставляет украшения по настройкам DA_Decor. Только картинка, у каждого игрока своя. */
	void Decorate(const struct FRoomStyle& Style, int32 Ordinal);
	/** Ночью краска светится слабее: комнаты темнеют вместе с улицей. */
	void UpdateGlow();
	float FindFloorZ() const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<class UMaterialInstanceDynamic>> GlowMaterials;

	FTimerHandle GlowTimer;
	float AppliedDarkness = -1.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Room")
	TObjectPtr<UBoxComponent> Box;
};
