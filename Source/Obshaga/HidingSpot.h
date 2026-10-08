#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interactable.h"
#include "HidingSpot.generated.h"

class AItemActor;
class AObshagaCharacter;
class UStaticMeshComponent;

/**
 * Тайник: шкаф, тумбочка, кабинка. E с предметом в руках — спрятать, E с пустыми руками — обыскать,
 * F — спрятаться самому (если тайник это позволяет).
 * Что лежит внутри, знает только сервер: клиентам это не реплицируется, чтобы тайник нельзя было «просветить» читом.
 */
UCLASS()
class OBSHAGA_API AHidingSpot : public AActor, public IInteractable
{
	GENERATED_BODY()

public:
	AHidingSpot();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void OnConstruction(const FTransform& Transform) override;

	//~ IInteractable
	virtual FText GetInteractionPrompt(const AObshagaCharacter* By) const override;
	virtual void Interact(AObshagaCharacter* By) override;
	virtual FText GetSecondaryPrompt(const AObshagaCharacter* By) const override;
	virtual void SecondaryInteract(AObshagaCharacter* By) override;

	/** Сервер: предмет, спрятанный внутри (для проверок коменданта и заданий). */
	AItemActor* GetHiddenItem() const { return HiddenItem; }
	/** Сервер: игрок, который сидит внутри. */
	AObshagaCharacter* GetHiddenPlayer() const { return HiddenPlayer; }

	/**
	 * Сервер: тайник обыскивает комендант. Спрятавшегося игрока выгоняет наружу (OutFoundPlayer),
	 * запрещёнку вынимает и возвращает; остальные предметы не трогает.
	 */
	AItemActor* KomendantSearch(AObshagaCharacter*& OutFoundPlayer);

	/** Сервер: игрок, сидевший внутри, исчез из игры (вышел) — тайник снова свободен. */
	void ForgetHiddenPlayer(const AObshagaCharacter* Player);

	/** Где стоит спрятавшийся игрок. HalfHeight — половина роста его капсулы. */
	virtual FVector GetHiddenPlayerLocation(float HalfHeight) const;

	/** Сервер: опустошить тайник (реванш). Предметы и игроков возвращает на место тот, кто вызывает. */
	void ResetSpot();

protected:
	virtual void BeginPlay() override;

	void ApplySize();
	void StartTimedAction(AObshagaCharacter* By, bool bHideItem);
	void FinishTimedAction();
	void FinishHidingItem(AObshagaCharacter* By);
	void FinishSearch(AObshagaCharacter* By);
	virtual void EjectHiddenPlayer();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HidingSpot")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** Куда встаёт игрок, выходя из укрытия. Сторона +X — «лицо» тайника. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HidingSpot")
	TObjectPtr<USceneComponent> ExitPoint;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HidingSpot")
	FText DisplayName;

	/** Размер серого куба-заглушки. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HidingSpot", meta = (Units = "cm"))
	FVector BoxSize = FVector(60.f, 100.f, 200.f);

	/** Цвет куба-заглушки. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HidingSpot")
	FLinearColor Color = FLinearColor(0.36f, 0.22f, 0.12f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HidingSpot")
	bool bCanHidePlayer = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HidingSpot", meta = (ClampMin = "0", Units = "s"))
	float HideDuration = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HidingSpot", meta = (ClampMin = "0", Units = "s"))
	float SearchDuration = 1.f;

	/** Кто-то прямо сейчас прячет предмет или обыскивает тайник. */
	UPROPERTY(ReplicatedUsing = OnRep_Busy)
	bool bBusy = false;

	/** Кто-то начал рыться в тайнике: слышен шорох. */
	UFUNCTION()
	void OnRep_Busy();

	// Только сервер, не реплицируется.
	UPROPERTY()
	TObjectPtr<AItemActor> HiddenItem;

	UPROPERTY()
	TObjectPtr<AObshagaCharacter> HiddenPlayer;

private:
	FTimerHandle ActionTimer;
	TWeakObjectPtr<AObshagaCharacter> PendingBy;
	bool bPendingHideItem = false;
};
