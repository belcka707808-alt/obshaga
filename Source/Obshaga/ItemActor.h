#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interactable.h"
#include "ItemActor.generated.h"

class AHidingSpot;
class AObshagaCharacter;
class APlayerState;
class UObshagaItemData;
class UStaticMeshComponent;

UENUM()
enum class EItemState : uint8
{
	/** Лежит в мире, работает физика. */
	World,
	/** В руках у игрока. */
	Carried,
	/** Спрятан в тайнике, невидим. */
	Hidden
};

/** Где предмет находится. Реплицируется одной структурой, чтобы состояние и владелец приходили вместе. */
USTRUCT()
struct FItemPlacement
{
	GENERATED_BODY()

	UPROPERTY()
	EItemState State = EItemState::World;

	/** Игрок, который несёт предмет. Для спрятанного предмета пусто: тайник знает только сервер. */
	UPROPERTY()
	TObjectPtr<AActor> Holder = nullptr;
};

/** Предмет, который можно взять, нести, бросить и спрятать. Всё решает сервер. */
UCLASS()
class OBSHAGA_API AItemActor : public AActor, public IInteractable
{
	GENERATED_BODY()

public:
	AItemActor();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void OnConstruction(const FTransform& Transform) override;

	//~ IInteractable
	virtual FText GetInteractionPrompt(const AObshagaCharacter* By) const override;
	virtual void Interact(AObshagaCharacter* By) override;
	virtual FText GetSecondaryPrompt(const AObshagaCharacter* By) const override;
	virtual void SecondaryInteract(AObshagaCharacter* By) override;

	/** Сервер: игрок читает записку. Текст слуха уходит только ему. */
	void ReadBy(AObshagaCharacter* By);

	/** Паспорт предмета; если не назначен — значения по умолчанию. */
	const UObshagaItemData* GetItemData() const;
	FText GetDisplayName() const;
	bool IsHeavy() const;
	EItemState GetItemState() const { return Placement.State; }

	// Только сервер. Вызывают UCarryComponent и AHidingSpot.
	void SetCarriedBy(AObshagaCharacter* Carrier);
	void ReleaseToWorld(const FVector& Location, const FVector& Velocity);
	void SetHiddenIn(AHidingSpot* Spot, AObshagaCharacter* By);

	/** Сервер: вернуть предмет туда, где он лежал в начале игры (реванш). */
	void ResetToInitial();

	/** Сервер: id комнаты, где предмет сейчас (лежит, спрятан или его несут). */
	FName GetCurrentRoomId() const;
	/** Сервер: кто последним спрятал предмет в тайник. */
	APlayerState* GetLastHiddenBy() const { return LastHiddenBy.Get(); }

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnRep_Placement();

	UFUNCTION()
	void OnMeshHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	void ApplyPlacement();
	void ApplyItemData();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Item")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
	TObjectPtr<UObshagaItemData> ItemData;

	UPROPERTY(ReplicatedUsing = OnRep_Placement)
	FItemPlacement Placement;

	/** Только сервер: тайник, в котором лежит предмет. Не реплицируется. */
	UPROPERTY()
	TObjectPtr<AHidingSpot> HidingSpot;

private:
	/** Кто последним держал предмет: он считается виновником шума. */
	TWeakObjectPtr<AObshagaCharacter> LastCarrier;
	TWeakObjectPtr<APlayerState> LastHiddenBy;
	/** Слух в записке; сочиняется при первом чтении и живёт до конца раунда. Только сервер. */
	FText NoteText;
	float LastNoiseTime = -100.f;
	FTransform InitialTransform;
};
