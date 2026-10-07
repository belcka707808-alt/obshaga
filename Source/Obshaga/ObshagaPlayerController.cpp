#include "ObshagaPlayerController.h"

#include "CarryComponent.h"
#include "InteractionComponent.h"
#include "ObshagaCharacter.h"
#include "ObshagaGameMode.h"
#include "ObshagaGameState.h"
#include "ObshagaPlayerState.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"

namespace
{
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
	const AObshagaGameState* GameState = GetWorld()->GetGameState<AObshagaGameState>();
	if (GameState && GameState->GetInterrogation().bActive && !IsLocalPlayerInterrogated())
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

void AObshagaPlayerController::ClientHeardNoise_Implementation(FVector_NetQuantize Location, float Loudness)
{
	const float Now = GetWorld()->GetTimeSeconds();
	RecentNoises.RemoveAll([Now](const FHeardNoise& Noise) { return Now - Noise.Time > 3.f; });

	FHeardNoise& Noise = RecentNoises.AddDefaulted_GetRef();
	Noise.Location = Location;
	Noise.Loudness = Loudness;
	Noise.Time = Now;
}

void AObshagaPlayerController::ClientShowNotice_Implementation(const FText& Text)
{
	Notice = Text;
	NoticeTime = GetWorld()->GetTimeSeconds();
}
