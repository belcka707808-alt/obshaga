#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "ObshagaPlayerController.generated.h"

class AObshagaCharacter;
class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

/** «Руки игрока»: читает клавиши и передаёт команды персонажу. */
UCLASS()
class OBSHAGA_API AObshagaPlayerController : public APlayerController
{
	GENERATED_BODY()

protected:
	virtual void SetupInputComponent() override;

private:
	/** Раскладка MVP задаётся в коде: WASD, мышь, Shift, Ctrl, пробел, E. */
	void CreateDefaultInput();

	void OnMoveForward(const FInputActionValue& Value);
	void OnMoveRight(const FInputActionValue& Value);
	void OnLook(const FInputActionValue& Value);
	void OnJumpStarted();
	void OnJumpCompleted();
	void OnSprintStarted();
	void OnSprintCompleted();
	void OnCrouchStarted();
	void OnCrouchCompleted();
	void OnInteract();

	AObshagaCharacter* GetObshagaCharacter() const;

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> MoveForwardAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> MoveRightAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> JumpAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> SprintAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> CrouchAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> InteractAction;
};
