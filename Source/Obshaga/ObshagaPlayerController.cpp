#include "ObshagaPlayerController.h"

#include "InteractionComponent.h"
#include "ObshagaCharacter.h"
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
		ObshagaCharacter->GetInteractionComponent()->TryInteract();
	}
}
