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
	/** Упал ниже карты: движок уничтожил бы предмет, а сервер возвращает его на место. */
	virtual void FellOutOfWorld(const class UDamageType& DamageType) override;

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
	/** Поворот предмета относительно того, кто его несёт или кладёт: длинной стороной поперёк взгляда, плюс CarryYaw. */
	float GetCarryYaw() const;
	EItemState GetItemState() const { return Placement.State; }
	/** Кто несёт предмет; nullptr, если он не в руках. */
	AActor* GetHolder() const { return Placement.Holder; }

	// Только сервер. Вызывают UCarryComponent и AHidingSpot.
	void SetCarriedBy(AObshagaCharacter* Carrier);
	/** Предмет ложится с тем поворотом, с каким его держали (изъятое Палычем — на стол вахты). */
	void ReleaseToWorld(const FVector& Location, const FVector& Velocity);
	/** То же, но предмет ставится с этим поворотом (положили, бросили). */
	void ReleaseToWorld(const FVector& Location, const FVector& Velocity, const FRotator& Rotation);
	void SetHiddenIn(AHidingSpot* Spot, AObshagaCharacter* By);

	/** Сервер: предмет забрал комендант. Его стук о стол на вахте — не шум игрока, который нёс его последним. */
	void ForgetCarrier();
	/**
	 * Сервер: предмет только что бросили (вызывает UCarryComponent::Throw после ReleaseToWorld). Удар в течение
	 * LureThrowMaxAgeSeconds после броска — шум вида Thrown; взяли, положили, спрятали или вернули — метка стирается.
	 */
	void MarkThrown();
	/**
	 * Сервер: вещь только что оставил By (положил, бросил, спрятал, выронил на допросе). Если это записка, а у By задание
	 * пустить слух, на бумаге появляется подсказка: из каких комнат автор (его комната и ещё одна; Параноику — две чужие).
	 * Тот же автор подсказку не меняет, другой автор — сочиняет заново; кто просто переложил записку — ничего не меняет.
	 */
	void MarkPlantedBy(AObshagaCharacter* By);

	/** Сервер: вернуть предмет туда, где он лежал в начале игры (реванш). */
	void ResetToInitial();
	/** Комната, где предмет лежал в начале раунда (его «место»). */
	FName GetHomeRoomId() const;
	/** Сервер: предмет лежит там же, где в начале игры (не дальше Tolerance). */
	bool IsAtInitialPlace(float Tolerance) const;

	/** Сервер: id комнаты, где предмет сейчас (лежит, спрятан или его несут). */
	FName GetCurrentRoomId() const;
	/** Сервер: кто последним спрятал предмет в тайник. */
	APlayerState* GetLastHiddenBy() const { return LastHiddenBy.Get(); }

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnRep_Placement(const FItemPlacement& OldPlacement);

	UFUNCTION()
	void OnMeshHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	void ApplyPlacement();
	void ApplyItemData();
	/** Сервер, кадр спустя после падения ниже карты: Палыч приносит предмет на место. */
	void ReturnFromBelowWorld();

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
	/**
	 * Подсказка на бумаге записки-слуха (только сервер, сбрасывается в ResetToInitial): кто её оставил, правдивая пара
	 * комнат (комната автора и ещё одна, в случайном порядке) и выдумка для Параноика (две комнаты, где автор не живёт).
	 * Пусто — подсказки нет.
	 */
	TWeakObjectPtr<APlayerState> HintAuthor;
	TArray<FName> HintRooms;
	TArray<FName> FakeHintRooms;
	float LastNoiseTime = -100.f;
	/** Когда предмет бросили; -100 — с тех пор его брали, клали или прятали. Только сервер. */
	float LastThrowTime = -100.f;
	FTransform InitialTransform;
};
