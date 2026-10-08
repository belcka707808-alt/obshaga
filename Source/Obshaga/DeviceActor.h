#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interactable.h"
#include "DeviceActor.generated.h"

class AObshagaCharacter;
class APlayerState;
class UStaticMeshComponent;

/**
 * Прибор, который можно сломать и починить (плита на кухне). F — сломать, E — починить;
 * оба действия занимают время, и отходить нельзя. Состояние решает сервер.
 */
UCLASS()
class OBSHAGA_API ADeviceActor : public AActor, public IInteractable
{
	GENERATED_BODY()

public:
	ADeviceActor();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void OnConstruction(const FTransform& Transform) override;

	//~ IInteractable
	virtual FText GetInteractionPrompt(const AObshagaCharacter* By) const override;
	virtual void Interact(AObshagaCharacter* By) override;
	virtual FText GetSecondaryPrompt(const AObshagaCharacter* By) const override;
	virtual void SecondaryInteract(AObshagaCharacter* By) override;

	bool IsBroken() const { return bBroken; }
	const FText& GetDisplayName() const { return DisplayName; }
	/** Точка над прибором для подписи «сломано». */
	FVector GetLabelLocation() const;

	// Только сервер.
	FName GetRoomId() const;
	/** Кто сломал последним; пусто, если прибор сломался сам. */
	APlayerState* GetBrokenBy() const { return BrokenBy.Get(); }
	APlayerState* GetRepairedBy() const { return RepairedBy.Get(); }
	float GetBrokenTime() const { return BrokenTime; }
	/** Прибор ломается сам, без виновника. */
	void BreakByItself();
	/** Новый раунд: снова исправен. */
	void ResetDevice();

protected:
	virtual void BeginPlay() override;

	void ApplySize();
	bool IsRoundInProgress() const;
	void StartTimedAction(AObshagaCharacter* By, bool bBreak);
	void FinishTimedAction();
	void SetBroken(bool bNewBroken, AObshagaCharacter* By);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Device")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Device")
	FText DisplayName;

	/** Размер серого куба-заглушки. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Device", meta = (Units = "cm"))
	FVector BoxSize = FVector(60.f, 60.f, 90.f);

	/** Цвет куба-заглушки. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Device")
	FLinearColor Color = FLinearColor(0.75f, 0.75f, 0.78f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Device", meta = (ClampMin = "0", Units = "s"))
	float BreakDuration = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Device", meta = (ClampMin = "0", Units = "s"))
	float RepairDuration = 5.f;

	/** Громкость треска при поломке: 0 — тихо, 1 — грохот. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Device", meta = (ClampMin = "0", ClampMax = "1"))
	float BreakLoudness = 0.4f;

	UPROPERTY(Replicated)
	bool bBroken = false;

	/** Кто-то прямо сейчас ломает или чинит. */
	UPROPERTY(Replicated)
	bool bBusy = false;

private:
	FTimerHandle ActionTimer;
	TWeakObjectPtr<AObshagaCharacter> PendingBy;
	bool bPendingBreak = false;

	TWeakObjectPtr<APlayerState> BrokenBy;
	TWeakObjectPtr<APlayerState> RepairedBy;
	float BrokenTime = 0.f;
};
