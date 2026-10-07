#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "ObshagaCharacter.generated.h"

class ARoomVolume;
class UCameraComponent;
class UInteractionComponent;
class UObshagaCharacterConfig;
class USpringArmComponent;

/** Жилец общаги: ходьба, бег, присед, камера от третьего лица. */
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

	void SetSprinting(bool bNewSprinting);
	bool IsSprinting() const { return bIsSprinting; }

	/** Комната, в которой персонаж сейчас находится (считается на каждой машине по позиции). */
	ARoomVolume* GetCurrentRoom() const;

protected:
	virtual void BeginPlay() override;

	UFUNCTION(Server, Reliable)
	void ServerSetSprinting(bool bNewSprinting);

	UFUNCTION()
	void OnRep_IsSprinting();

	void UpdateMovementSpeed();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<UCameraComponent> FollowCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interaction")
	TObjectPtr<UInteractionComponent> InteractionComponent;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config")
	TObjectPtr<UObshagaCharacterConfig> Config;

	/** Владельцу не шлём: он выставляет бег сам, сразу по нажатию. */
	UPROPERTY(ReplicatedUsing = OnRep_IsSprinting)
	bool bIsSprinting = false;

private:
	TArray<TWeakObjectPtr<ARoomVolume>> OverlappingRooms;
};
