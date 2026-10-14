#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interactable.h"
#include "DoorActor.generated.h"

class UStaticMeshComponent;

UENUM()
enum class EDoorState : uint8
{
	Closed,
	OpenForward,
	OpenBackward
};

/** Дверь. Состояние решает сервер, поворот створки каждая машина доигрывает сама. */
UCLASS()
class OBSHAGA_API ADoorActor : public AActor, public IInteractable
{
	GENERATED_BODY()

public:
	ADoorActor();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;

	//~ IInteractable
	virtual FText GetInteractionPrompt(const AObshagaCharacter* By) const override;
	virtual void Interact(AObshagaCharacter* By) override;

	bool IsOpen() const { return DoorState != EDoorState::Closed; }

	/**
	 * Проём закрытой двери в мире: от петли на ширину створки, от пола до верха створки.
	 * AlongClearance — запас вдоль стены с каждой стороны, HalfDepth — на сколько коробка выходит по обе стороны стены.
	 */
	FBox GetDoorwayBox(float AlongClearance, float HalfDepth) const;
	/**
	 * Путь по точкам маршрута, который проходит через дверной проём наискосок, получает точку в середине проёма:
	 * так идущий проходит дверь ровно посередине и не цепляет косяк плечом. Только сервер (для ботов и Палыча).
	 */
	static void ThreadDoorways(const UWorld* World, TArray<FVector>& Path, const FVector& Start);

	/** Всё место, через которое проходит створка, открываясь в любую сторону. */
	FBox GetSwingBox() const;

	/** Середина створки в мире. */
	FVector GetDoorCenter() const;

	/** Сервер: закрыть дверь (реванш). */
	void ResetDoor();

	/** Сервер: открыть дверь от того, кто стоит в точке FromLocation (без шума — так открывает комендант). */
	void OpenFor(const FVector& FromLocation);

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnRep_DoorState();

	float GetTargetYaw() const;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Door")
	TObjectPtr<USceneComponent> Hinge;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Door")
	TObjectPtr<UStaticMeshComponent> DoorMesh;

	UPROPERTY(EditAnywhere, Category = "Door", meta = (ClampMin = "0", ClampMax = "170", Units = "deg"))
	float OpenAngle = 95.f;

	/** Скорость поворота створки, градусов в секунду. */
	UPROPERTY(EditAnywhere, Category = "Door", meta = (ClampMin = "1"))
	float OpenSpeed = 260.f;

	/** Модель двери вместо куба; пусто — остаётся куб. */
	UPROPERTY(EditAnywhere, Category = "Door")
	TObjectPtr<class UStaticMesh> Model;

	UPROPERTY(EditAnywhere, Category = "Door", meta = (Units = "deg"))
	float ModelYaw = 0.f;

	/** Цвет куба-заглушки. */
	UPROPERTY(EditAnywhere, Category = "Door")
	FLinearColor Color = FLinearColor(0.55f, 0.36f, 0.18f);

	/** Громкость скрипа: 0 — тихо, 1 — грохот. */
	UPROPERTY(EditAnywhere, Category = "Door", meta = (ClampMin = "0", ClampMax = "1"))
	float NoiseLoudness = 0.2f;

	UPROPERTY(ReplicatedUsing = OnRep_DoorState)
	EDoorState DoorState = EDoorState::Closed;

private:
	float CurrentYaw = 0.f;
};
