#include "ObshagaPlayerController.h"

#include "CarryComponent.h"
#include "InteractionComponent.h"
#include "KomendantCharacter.h"
#include "ObshagaCharacter.h"
#include "ObshagaCharacterConfig.h"
#include "Obshaga.h"
#include "Components/AudioComponent.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ConfigCacheIni.h"
#include "ObshagaGameMode.h"
#include "ObshagaGameState.h"
#include "ObshagaPlayerState.h"
#include "RoomVolume.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"

namespace
{
	// Насколько точно надо смотреть на игрока, чтобы показать на него: косинус угла (около 20°).
	constexpr float AccuseAimDot = 0.94f;

	// Чувство опасности: коменданта этажом выше или ниже не чувствуем; в погоне он страшнее; экран темнеет плавно.
	constexpr float DangerSameFloorHeight = 250.f;
	constexpr float DangerChaseBoost = 1.4f;
	constexpr float DangerInterpSpeed = 3.f;
	// Тише этого уровня опасности сердце не слышно.
	constexpr float HeartbeatMinDanger = 0.08f;
	// Шаги: смещение за кадр больше этого — телепорт, а не шаг. Музыка: скорость смены громкости.
	constexpr float FootstepMaxFrameStep = 120.f;
	constexpr float MusicFadeSpeed = 1.5f;

	// Обучение: общий предел и сколько висит последняя подсказка.
	constexpr float TutorialMaxSeconds = 90.f;
	constexpr float TutorialLastStepSeconds = 8.f;
	const TCHAR* TutorialConfigSection = TEXT("Obshaga");
	const TCHAR* TutorialConfigKey = TEXT("bTutorialDone");

	UInputAction* MakeAction(UObject* Outer, FName Name, EInputActionValueType ValueType)
	{
		UInputAction* Action = NewObject<UInputAction>(Outer, Name);
		Action->ValueType = ValueType;
		return Action;
	}

	void MapNegated(UInputMappingContext* Context, const UInputAction* Action, FKey Key, bool bX, bool bY)
	{
		UInputModifierNegate* Negate = NewObject<UInputModifierNegate>(Context);
		Negate->bX = bX;
		Negate->bY = bY;
		Negate->bZ = false;
		Context->MapKey(Action, Key).Modifiers.Add(Negate);
	}
}

void AObshagaPlayerController::CreateDefaultInput()
{
	MoveForwardAction = MakeAction(this, TEXT("IA_MoveForward"), EInputActionValueType::Axis1D);
	MoveRightAction = MakeAction(this, TEXT("IA_MoveRight"), EInputActionValueType::Axis1D);
	LookAction = MakeAction(this, TEXT("IA_Look"), EInputActionValueType::Axis2D);
	JumpAction = MakeAction(this, TEXT("IA_Jump"), EInputActionValueType::Boolean);
	SprintAction = MakeAction(this, TEXT("IA_Sprint"), EInputActionValueType::Boolean);
	CrouchAction = MakeAction(this, TEXT("IA_Crouch"), EInputActionValueType::Boolean);
	InteractAction = MakeAction(this, TEXT("IA_Interact"), EInputActionValueType::Boolean);
	SecondaryInteractAction = MakeAction(this, TEXT("IA_SecondaryInteract"), EInputActionValueType::Boolean);
	DropAction = MakeAction(this, TEXT("IA_Drop"), EInputActionValueType::Boolean);
	ThrowAction = MakeAction(this, TEXT("IA_Throw"), EInputActionValueType::Boolean);
	PhoneAction = MakeAction(this, TEXT("IA_Phone"), EInputActionValueType::Boolean);
	ConfessAction = MakeAction(this, TEXT("IA_Confess"), EInputActionValueType::Boolean);
	LieAction = MakeAction(this, TEXT("IA_Lie"), EInputActionValueType::Boolean);
	SilentAction = MakeAction(this, TEXT("IA_Silent"), EInputActionValueType::Boolean);
	AlibiAction = MakeAction(this, TEXT("IA_Alibi"), EInputActionValueType::Boolean);
	StartAction = MakeAction(this, TEXT("IA_Start"), EInputActionValueType::Boolean);
	TipAction = MakeAction(this, TEXT("IA_Tip"), EInputActionValueType::Boolean);
	AccuseAction = MakeAction(this, TEXT("IA_Accuse"), EInputActionValueType::Boolean);
	RoomTipAction = MakeAction(this, TEXT("IA_RoomTip"), EInputActionValueType::Boolean);
	EmoteWheelAction = MakeAction(this, TEXT("IA_EmoteWheel"), EInputActionValueType::Boolean);
	SkipTutorialAction = MakeAction(this, TEXT("IA_SkipTutorial"), EInputActionValueType::Boolean);

	DefaultMappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_Obshaga"));
	DefaultMappingContext->MapKey(MoveForwardAction, EKeys::W);
	MapNegated(DefaultMappingContext, MoveForwardAction, EKeys::S, true, false);
	DefaultMappingContext->MapKey(MoveRightAction, EKeys::D);
	MapNegated(DefaultMappingContext, MoveRightAction, EKeys::A, true, false);
	// Мышь вверх = взгляд вверх.
	MapNegated(DefaultMappingContext, LookAction, EKeys::Mouse2D, false, true);
	DefaultMappingContext->MapKey(JumpAction, EKeys::SpaceBar);
	DefaultMappingContext->MapKey(SprintAction, EKeys::LeftShift);
	DefaultMappingContext->MapKey(CrouchAction, EKeys::LeftControl);
	DefaultMappingContext->MapKey(InteractAction, EKeys::E);
	DefaultMappingContext->MapKey(SecondaryInteractAction, EKeys::F);
	DefaultMappingContext->MapKey(DropAction, EKeys::G);
	DefaultMappingContext->MapKey(ThrowAction, EKeys::LeftMouseButton);
	DefaultMappingContext->MapKey(PhoneAction, EKeys::Tab);
	DefaultMappingContext->MapKey(ConfessAction, EKeys::One);
	DefaultMappingContext->MapKey(LieAction, EKeys::Two);
	DefaultMappingContext->MapKey(SilentAction, EKeys::Three);
	DefaultMappingContext->MapKey(AlibiAction, EKeys::Y);
	DefaultMappingContext->MapKey(StartAction, EKeys::Enter);
	DefaultMappingContext->MapKey(TipAction, EKeys::T);
	DefaultMappingContext->MapKey(AccuseAction, EKeys::R);
	DefaultMappingContext->MapKey(RoomTipAction, EKeys::B);
	DefaultMappingContext->MapKey(EmoteWheelAction, EKeys::Q);
	DefaultMappingContext->MapKey(SkipTutorialAction, EKeys::H);

	// Цифры 1–8 выбирают фразу в колесе эмоций. Клавиши 1–3 заняты ещё и ответами на допросе — они не мешают друг другу.
	const FKey DigitKeys[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight };
	DigitActions.Reset();
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(DigitKeys); ++Index)
	{
		UInputAction* Action = MakeAction(this, *FString::Printf(TEXT("IA_Digit%d"), Index + 1), EInputActionValueType::Boolean);
		DigitActions.Add(Action);
		DefaultMappingContext->MapKey(Action, DigitKeys[Index]);
	}
}

void AObshagaPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(InputComponent);
	if (!ensureMsgf(Input, TEXT("Obshaga needs Enhanced Input as the default input component class")))
	{
		return;
	}

	CreateDefaultInput();

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		Subsystem->AddMappingContext(DefaultMappingContext, 0);
	}

	Input->BindAction(MoveForwardAction, ETriggerEvent::Triggered, this, &AObshagaPlayerController::OnMoveForward);
	Input->BindAction(MoveRightAction, ETriggerEvent::Triggered, this, &AObshagaPlayerController::OnMoveRight);
	Input->BindAction(LookAction, ETriggerEvent::Triggered, this, &AObshagaPlayerController::OnLook);
	Input->BindAction(JumpAction, ETriggerEvent::Started, this, &AObshagaPlayerController::OnJumpStarted);
	Input->BindAction(JumpAction, ETriggerEvent::Completed, this, &AObshagaPlayerController::OnJumpCompleted);
	Input->BindAction(SprintAction, ETriggerEvent::Started, this, &AObshagaPlayerController::OnSprintStarted);
	Input->BindAction(SprintAction, ETriggerEvent::Completed, this, &AObshagaPlayerController::OnSprintCompleted);
	Input->BindAction(CrouchAction, ETriggerEvent::Started, this, &AObshagaPlayerController::OnCrouchStarted);
	Input->BindAction(CrouchAction, ETriggerEvent::Completed, this, &AObshagaPlayerController::OnCrouchCompleted);
	Input->BindAction(InteractAction, ETriggerEvent::Started, this, &AObshagaPlayerController::OnInteract);
	Input->BindAction(SecondaryInteractAction, ETriggerEvent::Started, this, &AObshagaPlayerController::OnSecondaryInteract);
	Input->BindAction(DropAction, ETriggerEvent::Started, this, &AObshagaPlayerController::OnDrop);
	Input->BindAction(ThrowAction, ETriggerEvent::Started, this, &AObshagaPlayerController::OnThrow);
	Input->BindAction(PhoneAction, ETriggerEvent::Started, this, &AObshagaPlayerController::OnTogglePhone);
	Input->BindAction(ConfessAction, ETriggerEvent::Started, this, &AObshagaPlayerController::OnChoiceConfess);
	Input->BindAction(LieAction, ETriggerEvent::Started, this, &AObshagaPlayerController::OnChoiceLie);
	Input->BindAction(SilentAction, ETriggerEvent::Started, this, &AObshagaPlayerController::OnChoiceSilent);
	Input->BindAction(AlibiAction, ETriggerEvent::Started, this, &AObshagaPlayerController::OnAlibi);
	Input->BindAction(StartAction, ETriggerEvent::Started, this, &AObshagaPlayerController::OnStart);
	Input->BindAction(TipAction, ETriggerEvent::Started, this, &AObshagaPlayerController::OnTipOff);
	Input->BindAction(AccuseAction, ETriggerEvent::Started, this, &AObshagaPlayerController::OnAccuse);
	Input->BindAction(RoomTipAction, ETriggerEvent::Started, this, &AObshagaPlayerController::OnTipOffRoom);
	Input->BindAction(EmoteWheelAction, ETriggerEvent::Started, this, &AObshagaPlayerController::OnToggleEmoteWheel);
	Input->BindAction(SkipTutorialAction, ETriggerEvent::Started, this, &AObshagaPlayerController::OnSkipTutorial);
	for (int32 Index = 0; Index < DigitActions.Num(); ++Index)
	{
		Input->BindActionValueLambda(DigitActions[Index], ETriggerEvent::Started, [this, Index](const FInputActionValue&) { OnDigit(Index); });
	}
}

void AObshagaPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// Обучение показывается только при первом запуске игры на этом компьютере.
	bool bTutorialDone = false;
	GConfig->GetBool(TutorialConfigSection, TutorialConfigKey, bTutorialDone, GGameUserSettingsIni);
	if (IsLocalController() && !bTutorialDone)
	{
		TutorialStep = 0;
		TutorialStartTime = TutorialStepTime = GetWorld()->GetTimeSeconds();
	}
}

void AObshagaPlayerController::OnToggleEmoteWheel()
{
	const AObshagaCharacter* Me = GetObshagaCharacter();
	bEmoteWheelOpen = !bEmoteWheelOpen && Me && !Me->IsGhost() && !Me->IsHiding() && !IsLocalPlayerInterrogated();
}

void AObshagaPlayerController::OnDigit(int32 Digit)
{
	if (!bEmoteWheelOpen)
	{
		return;
	}
	bEmoteWheelOpen = false;
	if (AObshagaCharacter* Me = GetObshagaCharacter())
	{
		Me->TryEmote(Digit);
	}
}

float AObshagaPlayerController::GetHeartPulse() const
{
	// Двойной удар «тук-тук»: два коротких всплеска в начале каждого цикла.
	const float Cycle = FMath::Frac(HeartPhase);
	const float First = FMath::Exp(-FMath::Square((Cycle - 0.08f) / 0.05f));
	const float Second = 0.7f * FMath::Exp(-FMath::Square((Cycle - 0.30f) / 0.06f));
	return FMath::Clamp(First + Second, 0.f, 1.f);
}

void AObshagaPlayerController::UpdateDanger(float DeltaTime)
{
	const AObshagaCharacter* Me = GetObshagaCharacter();
	const AObshagaGameState* GameState = GetWorld()->GetGameState<AObshagaGameState>();

	// Опасность — это близость коменданта на том же этаже; в погоне он страшнее.
	float Target = 0.f;
	if (Me && !Me->IsGhost() && GameState && GameState->GetRoundState() == ERoundState::InProgress)
	{
		const UObshagaCharacterConfig* Config = Me->GetConfig();
		for (TActorIterator<AKomendantCharacter> It(GetWorld()); It; ++It)
		{
			const FVector Delta = It->GetActorLocation() - Me->GetActorLocation();
			if (FMath::Abs(Delta.Z) > DangerSameFloorHeight)
			{
				continue;
			}
			float Level = 1.f - Delta.Size2D() / Config->DangerRadius;
			if (It->GetAlert() == EKomendantAlert::Chasing)
			{
				Level *= DangerChaseBoost;
				// Погоня только что началась — окрик коменданта слышно оттуда, где он стоит.
				if (!bKomendantWasChasing)
				{
					UObshagaAudioConfig::PlayAt(this, UObshagaAudioConfig::Get()->ChaseStart, It->GetActorLocation());
				}
			}
			bKomendantWasChasing = It->GetAlert() == EKomendantAlert::Chasing;
			Target = FMath::Max(Target, FMath::Clamp(Level, 0.f, 1.f));
		}

		const float OldPhase = HeartPhase;
		HeartPhase += DeltaTime * FMath::Lerp(Config->HeartRateCalm, Config->HeartRatePanic, DangerLevel);
		// Новый цикл пульса — новый удар сердца; чем страшнее, тем громче.
		if (FMath::FloorToInt32(HeartPhase) != FMath::FloorToInt32(OldPhase) && DangerLevel > HeartbeatMinDanger)
		{
			UObshagaAudioConfig::Play2D(this, UObshagaAudioConfig::Get()->Heartbeat, DangerLevel);
		}
	}
	DangerLevel = FMath::FInterpTo(DangerLevel, Target, DeltaTime, DangerInterpSpeed);
}

void AObshagaPlayerController::UpdateCameraShake(float DeltaTime)
{
	AObshagaCharacter* Me = GetObshagaCharacter();
	if (!Me)
	{
		return;
	}

	// Поймали — камеру встряхивает.
	const bool bInterrogated = IsLocalPlayerInterrogated();
	if (bInterrogated && !bWasInterrogated)
	{
		ShakeTimeLeft = Me->GetConfig()->CaughtShakeSeconds;
		bEmoteWheelOpen = false;
		UObshagaAudioConfig::Play2D(this, UObshagaAudioConfig::Get()->Caught);
		UE_LOG(LogObshaga, Verbose, TEXT("[%s] Caught: camera shake %.2f s, amplitude %.0f"), *GetNameSafe(GetWorld()), ShakeTimeLeft, Me->GetConfig()->CaughtShakeAmplitude);
	}
	bWasInterrogated = bInterrogated;

	if (ShakeTimeLeft > 0.f)
	{
		ShakeTimeLeft = FMath::Max(0.f, ShakeTimeLeft - DeltaTime);
		const float Strength = Me->GetConfig()->CaughtShakeAmplitude * ShakeTimeLeft / FMath::Max(Me->GetConfig()->CaughtShakeSeconds, KINDA_SMALL_NUMBER);
		Me->SetCameraShakeOffset(ShakeTimeLeft > 0.f ? FMath::VRand() * Strength : FVector::ZeroVector);
	}
}

void AObshagaPlayerController::UpdateFootsteps()
{
	// Шаги всех, кто ходит рядом (и коменданта): звук ставится на этой машине по пройденному пути.
	const UObshagaAudioConfig* Audio = UObshagaAudioConfig::Get();
	if (!Audio->Footstep)
	{
		return;
	}

	for (TActorIterator<ACharacter> It(GetWorld()); It; ++It)
	{
		const ACharacter* Walker = *It;
		const AObshagaCharacter* Resident = Cast<AObshagaCharacter>(Walker);
		FVector& LastLocation = FootstepLastLocation.FindOrAdd(Walker, Walker->GetActorLocation());
		float& Travelled = FootstepTravelled.FindOrAdd(Walker, 0.f);

		const float Step = FVector::Dist2D(Walker->GetActorLocation(), LastLocation);
		LastLocation = Walker->GetActorLocation();
		// Телепорт (укрытие, реванш) и полёт шагами не считаются; призраки и спрятавшиеся не топают.
		if (Step > FootstepMaxFrameStep || !Walker->GetCharacterMovement()->IsMovingOnGround()
			|| (Resident && (Resident->IsGhost() || Resident->IsHiding())))
		{
			continue;
		}

		Travelled += Step;
		if (Travelled >= Audio->FootstepStride)
		{
			Travelled = 0.f;
			float Volume = Audio->FootstepWalkVolume;
			if (Walker->IsCrouched())
			{
				Volume = Audio->FootstepCrouchVolume;
			}
			else if (Resident && Resident->IsSprinting())
			{
				Volume = Audio->FootstepRunVolume;
			}
			UObshagaAudioConfig::PlayAt(this, Audio->Footstep, Walker->GetActorLocation() - FVector(0.f, 0.f, Walker->GetSimpleCollisionHalfHeight()), Volume);
		}
	}
}

void AObshagaPlayerController::UpdateMusic(float DeltaTime)
{
	if (!IsLocalController())
	{
		return;
	}

	// Три дорожки играют всегда, меняется только громкость: спокойная — в раунде, напряжённая — по чувству опасности, третья — на итогах.
	const UObshagaAudioConfig* Audio = UObshagaAudioConfig::Get();
	if (!bMusicStarted)
	{
		bMusicStarted = true;
		MusicCalm = Audio->MusicCalm ? UGameplayStatics::SpawnSound2D(this, Audio->MusicCalm, 1.f, 1.f, 0.f, nullptr, false, false) : nullptr;
		MusicTense = Audio->MusicTense ? UGameplayStatics::SpawnSound2D(this, Audio->MusicTense, 1.f, 1.f, 0.f, nullptr, false, false) : nullptr;
	}

	const AObshagaGameState* GameState = GetWorld()->GetGameState<AObshagaGameState>();
	const ERoundState RoundState = GameState ? GameState->GetRoundState() : ERoundState::WaitingToStart;
	const bool bResults = RoundState == ERoundState::Finished;
	const float Calm = bResults ? 0.f : 1.f - Audio->CalmDuckAtDanger * DangerLevel;
	const float Tense = RoundState == ERoundState::InProgress ? DangerLevel : 0.f;

	auto Fade = [&](UAudioComponent* Component, float& Current, float Target)
	{
		Current = FMath::FInterpTo(Current, Target * Audio->MusicVolume, DeltaTime, MusicFadeSpeed);
		if (Component)
		{
			// Нулевая громкость остановила бы дорожку — держим её едва слышной.
			Component->SetVolumeMultiplier(FMath::Max(Current, 0.001f));
		}
	};
	Fade(MusicCalm, MusicCalmVolume, Calm);
	Fade(MusicTense, MusicTenseVolume, Tense);

	// На экране итогов — короткая весёлая тема, один раз.
	if (bResults && !bResultsJinglePlayed)
	{
		UObshagaAudioConfig::Play2D(this, Audio->MusicResults, Audio->ResultsJingleVolume);
	}
	bResultsJinglePlayed = bResults;
}

FText AObshagaPlayerController::GetTutorialText() const
{
	switch (TutorialStep)
	{
	case 0:
		return NSLOCTEXT("ObshagaTutorial", "Move", "Ходи на WASD, смотри мышью. Shift — бег, Ctrl — присесть.");
	case 1:
		return NSLOCTEXT("ObshagaTutorial", "Take", "Подойди к любому предмету и нажми E — возьми его. Мелочь у ног видно, если посмотреть вниз.");
	case 2:
		return NSLOCTEXT("ObshagaTutorial", "Hide", "Теперь спрячь: подойди к шкафу или тумбочке и нажми E. Или просто положи — G.");
	case 3:
		return NSLOCTEXT("ObshagaTutorial", "Phone", "Нажми Tab — это телефон. В нём твои секретные задания, очки и подозрение.");
	case 4:
		return NSLOCTEXT("ObshagaTutorial", "Danger", "Края экрана темнеют и стучит сердце — комендант рядом. Не попадайся ему с вещами в руках!");
	default:
		return FText::GetEmpty();
	}
}

void AObshagaPlayerController::UpdateTutorial()
{
	if (TutorialStep >= NumTutorialSteps)
	{
		return;
	}

	const float Now = GetWorld()->GetTimeSeconds();
	const AObshagaCharacter* Me = GetObshagaCharacter();
	if (!Me || Now - TutorialStartTime > TutorialMaxSeconds)
	{
		if (Me)
		{
			FinishTutorial();
		}
		return;
	}

	// Шаг пройден, когда игрок сам сделал то, о чём подсказка; последний просто висит несколько секунд.
	bool bStepDone = false;
	switch (TutorialStep)
	{
	case 0:
		bStepDone = Me->GetVelocity().SizeSquared2D() > FMath::Square(50.f) && Now - TutorialStepTime > 1.5f;
		break;
	case 1:
		bStepDone = Me->GetCarryComponent()->IsCarrying();
		break;
	case 2:
		bStepDone = !Me->GetCarryComponent()->IsCarrying();
		break;
	case 3:
		bStepDone = bPhoneOpen;
		break;
	default:
		bStepDone = Now - TutorialStepTime > TutorialLastStepSeconds;
		break;
	}

	if (bStepDone)
	{
		TutorialStepTime = Now;
		if (++TutorialStep >= NumTutorialSteps)
		{
			FinishTutorial();
		}
	}
}

void AObshagaPlayerController::FinishTutorial()
{
	TutorialStep = NumTutorialSteps;
	GConfig->SetBool(TutorialConfigSection, TutorialConfigKey, true, GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}

void AObshagaPlayerController::OnSkipTutorial()
{
	if (TutorialStep < NumTutorialSteps)
	{
		FinishTutorial();
	}
}

AObshagaCharacter* AObshagaPlayerController::GetObshagaCharacter() const
{
	return Cast<AObshagaCharacter>(GetPawn());
}

void AObshagaPlayerController::OnMoveForward(const FInputActionValue& Value)
{
	if (APawn* ControlledPawn = GetPawn())
	{
		const FRotator YawRotation(0.f, GetControlRotation().Yaw, 0.f);
		ControlledPawn->AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X), Value.Get<float>());
	}
}

void AObshagaPlayerController::OnMoveRight(const FInputActionValue& Value)
{
	if (APawn* ControlledPawn = GetPawn())
	{
		const FRotator YawRotation(0.f, GetControlRotation().Yaw, 0.f);
		ControlledPawn->AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y), Value.Get<float>());
	}
}

void AObshagaPlayerController::OnLook(const FInputActionValue& Value)
{
	const FVector2D LookAxis = Value.Get<FVector2D>();
	AddYawInput(LookAxis.X);
	AddPitchInput(LookAxis.Y);
}

void AObshagaPlayerController::OnJumpStarted()
{
	if (AObshagaCharacter* ObshagaCharacter = GetObshagaCharacter())
	{
		ObshagaCharacter->Jump();
	}
}

void AObshagaPlayerController::OnJumpCompleted()
{
	if (AObshagaCharacter* ObshagaCharacter = GetObshagaCharacter())
	{
		ObshagaCharacter->StopJumping();
	}
}

void AObshagaPlayerController::OnSprintStarted()
{
	if (AObshagaCharacter* ObshagaCharacter = GetObshagaCharacter())
	{
		ObshagaCharacter->SetSprinting(true);
	}
}

void AObshagaPlayerController::OnSprintCompleted()
{
	if (AObshagaCharacter* ObshagaCharacter = GetObshagaCharacter())
	{
		ObshagaCharacter->SetSprinting(false);
	}
}

void AObshagaPlayerController::OnCrouchStarted()
{
	if (AObshagaCharacter* ObshagaCharacter = GetObshagaCharacter())
	{
		ObshagaCharacter->Crouch();
	}
}

void AObshagaPlayerController::OnCrouchCompleted()
{
	if (AObshagaCharacter* ObshagaCharacter = GetObshagaCharacter())
	{
		ObshagaCharacter->UnCrouch();
	}
}

void AObshagaPlayerController::OnInteract()
{
	if (AObshagaCharacter* ObshagaCharacter = GetObshagaCharacter())
	{
		ObshagaCharacter->GetInteractionComponent()->TryInteract(false);
	}
}

void AObshagaPlayerController::OnSecondaryInteract()
{
	if (AObshagaCharacter* ObshagaCharacter = GetObshagaCharacter())
	{
		ObshagaCharacter->GetInteractionComponent()->TryInteract(true);
	}
}

void AObshagaPlayerController::OnDrop()
{
	if (AObshagaCharacter* ObshagaCharacter = GetObshagaCharacter())
	{
		ObshagaCharacter->GetCarryComponent()->TryDrop();
	}
}

void AObshagaPlayerController::OnThrow()
{
	if (AObshagaCharacter* ObshagaCharacter = GetObshagaCharacter())
	{
		ObshagaCharacter->GetCarryComponent()->TryThrow();
	}
}

void AObshagaPlayerController::OnTogglePhone()
{
	bPhoneOpen = !bPhoneOpen;
}

bool AObshagaPlayerController::IsLocalPlayerInterrogated() const
{
	const AObshagaGameState* GameState = GetWorld()->GetGameState<AObshagaGameState>();
	return GameState && GameState->GetInterrogation().bActive && GameState->GetInterrogation().Suspect == PlayerState;
}

void AObshagaPlayerController::OnChoiceConfess()
{
	if (IsLocalPlayerInterrogated())
	{
		ServerInterrogationChoice(static_cast<uint8>(EInterrogationChoice::Confess));
	}
}

void AObshagaPlayerController::OnChoiceLie()
{
	if (IsLocalPlayerInterrogated())
	{
		ServerInterrogationChoice(static_cast<uint8>(EInterrogationChoice::Lie));
	}
}

void AObshagaPlayerController::OnChoiceSilent()
{
	if (IsLocalPlayerInterrogated())
	{
		ServerInterrogationChoice(static_cast<uint8>(EInterrogationChoice::Silent));
	}
}

void AObshagaPlayerController::OnAlibi()
{
	// Призрак и спрятавшийся алиби дать не могут — сервер такое всё равно отклонит.
	const AObshagaGameState* GameState = GetWorld()->GetGameState<AObshagaGameState>();
	const AObshagaCharacter* Me = GetObshagaCharacter();
	if (GameState && GameState->GetInterrogation().bActive && !IsLocalPlayerInterrogated() && Me && !Me->IsGhost() && !Me->IsHiding())
	{
		ServerConfirmAlibi();
	}
}

void AObshagaPlayerController::OnStart()
{
	const AObshagaGameState* GameState = GetWorld()->GetGameState<AObshagaGameState>();
	if (GameState && GameState->GetRoundState() != ERoundState::InProgress)
	{
		ServerRequestStart();
	}
}

void AObshagaPlayerController::OnTipOff()
{
	const AObshagaPlayerState* MyState = GetPlayerState<AObshagaPlayerState>();
	if (MyState && MyState->GetVisibleRole() == EPlayerRole::Rat && !MyState->HasUsedTip())
	{
		ServerTipOff();
	}
}

AObshagaCharacter* AObshagaPlayerController::FindAccuseTarget() const
{
	const AObshagaPlayerState* MyState = GetPlayerState<AObshagaPlayerState>();
	const AObshagaCharacter* Me = GetObshagaCharacter();
	const AObshagaGameState* GameState = GetWorld()->GetGameState<AObshagaGameState>();
	if (!MyState || !MyState->CanAccuse() || !Me || Me->IsHiding() || Me->IsFrozen() || !GameState)
	{
		return nullptr;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector ViewDirection = ViewRotation.Vector();

	// Из тех, кто рядом и на виду, берём того, кто ближе всего к центру экрана.
	AObshagaCharacter* Best = nullptr;
	float BestDot = AccuseAimDot;
	for (APlayerState* OtherState : GameState->PlayerArray)
	{
		AObshagaCharacter* Other = OtherState ? Cast<AObshagaCharacter>(OtherState->GetPawn()) : nullptr;
		if (!Other || Other == Me || Other->IsHiding() || Other->IsGhost()
			|| FVector::Dist(Other->GetActorLocation(), Me->GetActorLocation()) > GameState->GetAccuseDistance())
		{
			continue;
		}

		const float Dot = FVector::DotProduct(ViewDirection, (Other->GetActorLocation() - ViewLocation).GetSafeNormal());
		if (Dot > BestDot && Me->GetInteractionComponent()->HasLineOfSight(Other))
		{
			BestDot = Dot;
			Best = Other;
		}
	}
	return Best;
}

bool AObshagaPlayerController::CanTipRoomNow() const
{
	const AObshagaPlayerState* MyState = GetPlayerState<AObshagaPlayerState>();
	const AObshagaCharacter* Me = GetObshagaCharacter();
	const AObshagaGameState* GameState = GetWorld()->GetGameState<AObshagaGameState>();
	const ARoomVolume* Room = Me ? Me->GetCurrentRoom() : nullptr;
	return MyState && Me && MyState->CanTipRoom() && GameState && GameState->GetRoundState() == ERoundState::InProgress
		&& !Me->IsHiding() && !Me->IsFrozen() && !Me->IsGhost()
		&& Room && Room->RoomType == ERoomType::Bedroom && Room->RoomId != MyState->GetHomeRoomId();
}

void AObshagaPlayerController::OnAccuse()
{
	if (AObshagaCharacter* Suspect = FindAccuseTarget())
	{
		ServerAccuse(Suspect);
	}
}

void AObshagaPlayerController::OnTipOffRoom()
{
	if (CanTipRoomNow())
	{
		ServerTipOffRoom();
	}
}

void AObshagaPlayerController::ServerAccuse_Implementation(AObshagaCharacter* Suspect)
{
	if (AObshagaGameMode* GameMode = GetWorld()->GetAuthGameMode<AObshagaGameMode>())
	{
		GameMode->Accuse(GetPlayerState<AObshagaPlayerState>(), Suspect);
	}
}

void AObshagaPlayerController::ServerTipOffRoom_Implementation()
{
	if (AObshagaGameMode* GameMode = GetWorld()->GetAuthGameMode<AObshagaGameMode>())
	{
		GameMode->TipOffRoom(GetPlayerState<AObshagaPlayerState>());
	}
}

void AObshagaPlayerController::ServerRequestStart_Implementation()
{
	if (AObshagaGameMode* GameMode = GetWorld()->GetAuthGameMode<AObshagaGameMode>())
	{
		GameMode->RequestStart(this);
	}
}

void AObshagaPlayerController::ServerTipOff_Implementation()
{
	if (AObshagaGameMode* GameMode = GetWorld()->GetAuthGameMode<AObshagaGameMode>())
	{
		GameMode->TipOff(GetPlayerState<AObshagaPlayerState>());
	}
}

void AObshagaPlayerController::ServerInterrogationChoice_Implementation(uint8 Choice)
{
	// Клиент присылает только номер варианта; кто на допросе и что из этого выйдет, решает сервер.
	AObshagaGameMode* GameMode = GetWorld()->GetAuthGameMode<AObshagaGameMode>();
	if (GameMode && Choice >= static_cast<uint8>(EInterrogationChoice::Confess) && Choice <= static_cast<uint8>(EInterrogationChoice::Silent))
	{
		GameMode->SubmitInterrogationChoice(GetPlayerState<AObshagaPlayerState>(), static_cast<EInterrogationChoice>(Choice));
	}
}

void AObshagaPlayerController::ServerConfirmAlibi_Implementation()
{
	if (AObshagaGameMode* GameMode = GetWorld()->GetAuthGameMode<AObshagaGameMode>())
	{
		GameMode->ConfirmAlibi(GetObshagaCharacter());
	}
}

void AObshagaPlayerController::ClientHeardNoise_Implementation(FVector_NetQuantize Location, float Loudness, ENoiseKind Kind)
{
	// Звук — по виду шума: скрип двери, треск плиты или удар предмета (тяжёлый грохочет иначе).
	const UObshagaAudioConfig* Audio = UObshagaAudioConfig::Get();
	USoundBase* Sound = Loudness >= Audio->HeavyImpactLoudness ? Audio->ItemImpactHeavy : Audio->ItemImpactLight;
	if (Kind == ENoiseKind::Door)
	{
		Sound = Audio->Door;
	}
	else if (Kind == ENoiseKind::Device)
	{
		Sound = Audio->DeviceBreak;
	}
	UObshagaAudioConfig::PlayAt(this, Sound, Location, FMath::Clamp(Loudness * 1.5f, 0.2f, 1.f));

	const float Now = GetWorld()->GetTimeSeconds();
	RecentNoises.RemoveAll([Now](const FHeardNoise& Noise) { return Now - Noise.Time > 3.f; });

	FHeardNoise& Noise = RecentNoises.AddDefaulted_GetRef();
	Noise.Location = Location;
	Noise.Loudness = Loudness;
	Noise.Time = Now;
}

void AObshagaPlayerController::ClientShowNotice_Implementation(const FText& Text)
{
	// Пока на экране висит прошлое сообщение, новое ждёт своей очереди, а не затирает его.
	const bool bShowing = GetWorld()->GetTimeSeconds() - NoticeTime < NoticeSeconds;
	if (!bShowing && NoticeQueue.IsEmpty())
	{
		Notice = Text;
		NoticeTime = GetWorld()->GetTimeSeconds();
		UObshagaAudioConfig::Play2D(this, UObshagaAudioConfig::Get()->Notice);
		return;
	}

	const auto SameText = [&Text](const FText& Other) { return Other.EqualTo(Text); };
	if ((bShowing && SameText(Notice)) || NoticeQueue.ContainsByPredicate(SameText))
	{
		return;
	}
	if (NoticeQueue.Num() >= MaxQueuedNotices)
	{
		NoticeQueue.RemoveAt(0);
	}
	NoticeQueue.Add(Text);
}

void AObshagaPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	if (IsLocalController())
	{
		UpdateDanger(DeltaTime);
		UpdateCameraShake(DeltaTime);
		UpdateTutorial();
		UpdateFootsteps();
		UpdateMusic(DeltaTime);
	}

	if (!NoticeQueue.IsEmpty() && GetWorld()->GetTimeSeconds() - NoticeTime >= NoticeSeconds)
	{
		Notice = NoticeQueue[0];
		NoticeQueue.RemoveAt(0);
		NoticeTime = GetWorld()->GetTimeSeconds();
		UObshagaAudioConfig::Play2D(this, UObshagaAudioConfig::Get()->Notice);
	}
}
