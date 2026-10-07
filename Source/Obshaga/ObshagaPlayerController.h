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
	bool IsPhoneOpen() const { return bPhoneOpen; }

	/** Сколько секунд сообщение висит на экране и сколько сообщений может ждать в очереди. */
	static constexpr float NoticeSeconds = 3.f;
	static constexpr int32 MaxQueuedNotices = 4;
	/** Игрок, на которого локальный игрок сейчас может показать коменданту (смотрит на него и стоит рядом). */
	AObshagaCharacter* FindAccuseTarget() const;
	/** Может ли локальный игрок прямо сейчас настучать на комнату, в которой стоит. */
	bool CanTipRoomNow() const;

protected:
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;

	/** Пойманный выбирает ответ на допросе: 1 — сознаться, 2 — соврать, 3 — молчать. */
	UFUNCTION(Server, Reliable)
	void ServerInterrogationChoice(uint8 Choice);

	/** Игрок рядом подтверждает алиби пойманного. */
	UFUNCTION(Server, Reliable)
	void ServerConfirmAlibi();

	/** Хост начинает раунд или реванш. От остальных сервер просьбу игнорирует. */
	UFUNCTION(Server, Reliable)
	void ServerRequestStart();

	/** Крыса стучит коменданту на свою жертву. */
	UFUNCTION(Server, Reliable)
	void ServerTipOff();

	/** Игрок называет коменданту вора. Сервер сам проверит, что тот рядом и на виду. */
	UFUNCTION(Server, Reliable)
	void ServerAccuse(AObshagaCharacter* Suspect);

	/** Игрок стучит на жилую комнату, в которой стоит. */
	UFUNCTION(Server, Reliable)
	void ServerTipOffRoom();

private:
	/** Раскладка MVP задаётся в коде: WASD, мышь, Shift, Ctrl, пробел, E, F, G, Tab, 1/2/3, Y, T, R, B, Enter, левая кнопка мыши. */
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
	void OnTogglePhone();
	void OnChoiceConfess();
	void OnChoiceLie();
	void OnChoiceSilent();
	void OnAlibi();
	void OnStart();
	void OnTipOff();
	void OnAccuse();
	void OnTipOffRoom();
	bool IsLocalPlayerInterrogated() const;

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

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> PhoneAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> ConfessAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LieAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> SilentAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> AlibiAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> StartAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> TipAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> AccuseAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> RoomTipAction;

	/** Телефон открыт только у локального игрока; мир при этом не останавливается. */
	bool bPhoneOpen = false;

	TArray<FHeardNoise> RecentNoises;
	FText Notice;
	float NoticeTime = -100.f;
	TArray<FText> NoticeQueue;
};
