#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "ObshagaCharacter.generated.h"

class AHidingSpot;
class ARoomVolume;
enum class EGameEventType : uint8;
class UCameraComponent;
class UCarryComponent;
class UInteractionComponent;
class UObshagaCharacterConfig;
class USpringArmComponent;

/** Жилец общаги: ходьба, бег, присед, камера от третьего лица, переноска предметов, прятки. */
UCLASS()
class OBSHAGA_API AObshagaCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AObshagaCharacter();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PostInitializeComponents() override;
	virtual void NotifyActorBeginOverlap(AActor* OtherActor) override;
	virtual void NotifyActorEndOverlap(AActor* OtherActor) override;
	virtual void OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;

	/** Числа баланса; если ассет не назначен — значения по умолчанию из класса. */
	const UObshagaCharacterConfig* GetConfig() const;

	UInteractionComponent* GetInteractionComponent() const { return InteractionComponent; }
	UCarryComponent* GetCarryComponent() const { return CarryComponent; }
	/** Точка перед персонажем, к которой цепляется предмет в руках. */
	USceneComponent* GetCarryPoint() const { return CarryPoint; }

	void SetSprinting(bool bNewSprinting);
	bool IsSprinting() const { return bIsSprinting; }

	/** Вызывает UCarryComponent, когда в руках что-то появилось или исчезло. */
	void OnCarriedItemChanged();

	/** Комната, в которой персонаж сейчас находится (считается на каждой машине по позиции). */
	ARoomVolume* GetCurrentRoom() const;

	AHidingSpot* GetHidingSpot() const { return HidingSpot; }
	bool IsHiding() const { return HidingSpot != nullptr; }

	// Только сервер. Вызывает AHidingSpot.
	void EnterHidingSpot(AHidingSpot* Spot);
	void ExitHidingSpot(const FVector& ExitLocation);

protected:
	virtual void BeginPlay() override;

	UFUNCTION(Server, Reliable)
	void ServerSetSprinting(bool bNewSprinting);

	UFUNCTION()
	void OnRep_IsSprinting();

	UFUNCTION()
	void OnRep_HidingSpot();

	void UpdateMovementSpeed();
	void ApplyHiding();
	void PublishRoomEvent(EGameEventType Type, const ARoomVolume* Room);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<UCameraComponent> FollowCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interaction")
	TObjectPtr<UInteractionComponent> InteractionComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Carry")
	TObjectPtr<UCarryComponent> CarryComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Carry")
	TObjectPtr<USceneComponent> CarryPoint;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config")
	TObjectPtr<UObshagaCharacterConfig> Config;

	/** Владельцу не шлём: он выставляет бег сам, сразу по нажатию. */
	UPROPERTY(ReplicatedUsing = OnRep_IsSprinting)
	bool bIsSprinting = false;

	/** Тайник, в котором персонаж сейчас прячется. */
	UPROPERTY(ReplicatedUsing = OnRep_HidingSpot)
	TObjectPtr<AHidingSpot> HidingSpot;

private:
	TArray<TWeakObjectPtr<ARoomVolume>> OverlappingRooms;
};
