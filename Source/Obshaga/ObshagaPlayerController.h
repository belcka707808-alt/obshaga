#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "ObshagaAudioConfig.h"
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
	void ClientHeardNoise(FVector_NetQuantize Location, float Loudness, ENoiseKind Kind);

	/** Короткое сообщение лично этому игроку: «Пусто», «Спрятано: телевизор». */
	UFUNCTION(Client, Reliable)
	void ClientShowNotice(const FText& Text);

	// Для HUD локального игрока.
	const TArray<FHeardNoise>& GetRecentNoises() const { return RecentNoises; }
	const FText& GetNotice() const { return Notice; }
	float GetNoticeTime() const { return NoticeTime; }
	bool IsPhoneOpen() const { return bPhoneOpen; }
	bool IsEmoteWheelOpen() const { return bEmoteWheelOpen; }
	bool IsSettingsOpen() const { return bSettingsOpen; }
	/** Куда сохранён последний снимок экрана (F12) и когда — экран итогов показывает путь несколько секунд. */
	const FString& GetScreenshotPath() const { return ScreenshotPath; }
	float GetScreenshotTime() const { return ScreenshotTime; }
	/** Показывается ли этому игроку заставка прямо сейчас (он её не пропустил). */
	bool IsWatchingIntro() const { return bIntroCamera; }

	/** Насколько близка опасность: 0 — коменданта рядом нет, 1 — он вплотную. Считается у локального игрока. */
	float GetDangerLevel() const { return DangerLevel; }
	/** Удар сердца сейчас: 0 — между ударами, 1 — пик удара. */
	float GetHeartPulse() const;

	/** Текст текущего шага обучения; пусто, если обучение не идёт (пройдено, лобби, заставка, итоги). */
	FText GetTutorialText() const;
	int32 GetTutorialStep() const { return TutorialStep; }
	static constexpr int32 NumTutorialSteps = 5;

	/** Сколько секунд сообщение висит на экране и сколько сообщений может ждать в очереди. */
	static constexpr float NoticeSeconds = 3.f;
	static constexpr int32 MaxQueuedNotices = 4;
	/** Игрок, на которого локальный игрок сейчас может показать коменданту (смотрит на него и стоит рядом). */
	AObshagaCharacter* FindAccuseTarget() const;
	/** Может ли локальный игрок прямо сейчас настучать на комнату, в которой стоит. */
	bool CanTipRoomNow() const;

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;
	virtual bool InputKey(const FInputKeyEventArgs& Params) override;
	/** Хост закрыл комнату (или сервер выгнал): уходим в меню и показываем там причину. */
	virtual void ClientReturnToMainMenuWithTextReason_Implementation(const FText& ReturnReason) override;

	/** Игрок сообщает серверу своё имя (ник Steam). Сервер чистит его и следит, чтобы имена не совпадали. */
	UFUNCTION(Server, Reliable)
	void ServerSetDisplayName(const FString& Wanted);

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

	/** Только для проверок (не в Shipping и только с ключом запуска -ObshagaDebugKeys): 0 — «поймай меня», 1 — «дай телевизор», 2 — записка, 3 — Палыч к боту, 4/5 — встать на улице (у двери / вдали). */
	UFUNCTION(Server, Reliable)
	void ServerDebug(uint8 What);

	/** Игрок стучит на жилую комнату, в которой стоит. */
	UFUNCTION(Server, Reliable)
	void ServerTipOffRoom();

private:
	/** Когда игрок в прошлый раз нажал «выйти в меню»: выход срабатывает со второго нажатия подряд. */
	float LeavePressTime = -100.f;
	bool bDisplayNameSet = false;
	bool bSettingsOpen = false;
	FString ScreenshotPath;
	float ScreenshotTime = -100.f;

	/** Заставка: пролёт камеры по коридорам. Камера у каждого игрока своя, по сети не ходит. */
	void UpdateIntro();
	UPROPERTY(Transient)
	TObjectPtr<class ACameraActor> IntroCamera;
	bool bIntroCamera = false;

	/** Сцена допроса: лампа над пойманным (у всех) и камера сбоку (у самого пойманного). */
	void UpdateInterrogationScene();
	UPROPERTY(Transient)
	TObjectPtr<class APointLight> InterrogationLamp;
	UPROPERTY(Transient)
	TObjectPtr<class ACameraActor> InterrogationCamera;
	bool bInterrogationCamera = false;

	/** Вещь в своих руках, которую сейчас не рисуем: камера упёрлась в неё вплотную (см. PlayerTick). Только своя машина. */
	TWeakObjectPtr<class AItemActor> CameraHiddenItem;

	/**
	 * Обход для проверки вида (ключ запуска -ObshagaTour, не в Shipping): камера сама встаёт с обеих сторон каждой
	 * открытой двери и в двух углах каждой зоны и сохраняет снимки в Saved/Tour.
	 */
	void StartTour();
	void TourStep();
	TArray<FTransform> TourViews;
	TArray<FString> TourNames;
	TArray<TWeakObjectPtr<class ADoorActor>> TourDoors;
	TWeakObjectPtr<class ACameraActor> TourCamera;
	FTimerHandle TourTimer;
	int32 TourIndex = 0;
	bool bTourShot = false;
	bool bIntroSkipped = false;
	bool bFlashlightsOn = false;

	/** Раскладка MVP задаётся в коде: WASD, мышь, Shift, Ctrl, пробел, E, F, G, Tab, 1–8, Y, T, R, B, Q, H, Enter, левая кнопка мыши. */
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
	void OnToggleEmoteWheel();
	void OnDigit(int32 Digit);
	void OnSkipTutorial();

	void UpdateDanger(float DeltaTime);
	void UpdateCameraShake(float DeltaTime);
	void UpdateTutorial(float DeltaTime);
	/** Обучение идёт прямо сейчас: не пройдено, раунд идёт и заставка не играет. В остальное время его часы стоят. */
	bool IsTutorialLive() const;
	void FinishTutorial();
	/** Свой допрос начался или кончился: прочитанное сообщение уступает место следующему. */
	void UpdateNoticesOnInterrogation();
	void UpdateFootsteps();
	void UpdateMusic(float DeltaTime);

	// Звук — только на этой машине.
	UPROPERTY(Transient)
	TObjectPtr<class UAudioComponent> MusicCalm;

	UPROPERTY(Transient)
	TObjectPtr<class UAudioComponent> MusicTense;

	bool bMusicStarted = false;
	float FrameSampleSeconds = 0.f;
	int32 FrameSampleCount = 0;
	float MusicCalmVolume = 0.f;
	float MusicTenseVolume = 0.f;
	bool bResultsJinglePlayed = false;
	bool bKomendantWasChasing = false;
	TMap<TWeakObjectPtr<const class ACharacter>, FVector> FootstepLastLocation;
	TMap<TWeakObjectPtr<const class ACharacter>, float> FootstepTravelled;
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

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> EmoteWheelAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> SkipTutorialAction;

	/** Цифры 1–8: выбор фразы в колесе эмоций. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInputAction>> DigitActions;

	bool bEmoteWheelOpen = false;

	// Чувство опасности и тряска — только у локального игрока.
	float DangerLevel = 0.f;
	float HeartPhase = 0.f;
	float ShakeTimeLeft = 0.f;
	bool bWasInterrogated = false;

	// Обучение первого запуска: номер шага (NumTutorialSteps — пройдено) и сколько секунд шли всё обучение и текущий шаг.
	// Считается только время раунда: лобби и заставка в эти секунды не входят.
	int32 TutorialStep = NumTutorialSteps;
	float TutorialSeconds = 0.f;
	float TutorialStepSeconds = 0.f;

	/** Телефон открыт только у локального игрока; мир при этом не останавливается. */
	bool bPhoneOpen = false;

	TArray<FHeardNoise> RecentNoises;
	FText Notice;
	float NoticeTime = -100.f;
	TArray<FText> NoticeQueue;
	/** Идёт свой допрос: сообщения видны не посреди экрана, а строкой в панели допроса. */
	bool bNoticesInPanel = false;
};
