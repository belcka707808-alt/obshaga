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

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Room")
	TObjectPtr<UBoxComponent> Box;
};
