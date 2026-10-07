#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "ObshagaPlayerController.generated.h"

class AObshagaCharacter;
class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

/** Шум, который услышал локальный игрок. Нужен только для индикатора на экране. */
struct FHeardNoise
{
	FVector Location = FVector::ZeroVector;
	float Loudness = 0.f;
	float Time = 0.f;
};

/** «Руки игрока»: читает клавиши и передаёт команды персонажу. */
UCLASS()
class OBSHAGA_API AObshagaPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	/** Сервер сообщает игроку, что тот услышал шум. Шлётся только тем, кто достаточно близко. */
	UFUNCTION(Client, Unreliable)
	void ClientHeardNoise(FVector_NetQuantize Location, float Loudness);

	/** Короткое сообщение лично этому игроку: «Пусто», «Спрятано: телевизор». */
	UFUNCTION(Client, Reliable)
	void ClientShowNotice(const FText& Text);

	// Для HUD локального игрока.
	const TArray<FHeardNoise>& GetRecentNoises() const { return RecentNoises; }
	const FText& GetNotice() const { return Notice; }
	float GetNoticeTime() const { return NoticeTime; }

protected:
	virtual void SetupInputComponent() override;

private:
	/** Раскладка MVP задаётся в коде: WASD, мышь, Shift, Ctrl, пробел, E, F, G, левая кнопка мыши. */
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
	void OnSecondaryInteract();
	void OnDrop();
	void OnThrow();

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

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> SecondaryInteractAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> DropAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> ThrowAction;

	TArray<FHeardNoise> RecentNoises;
	FText Notice;
	float NoticeTime = -100.f;
};
