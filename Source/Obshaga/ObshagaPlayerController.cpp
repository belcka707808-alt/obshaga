#include "ObshagaPlayerController.h"

#include "CarryComponent.h"
#include "DoorActor.h"
#include "NightExitDoor.h"
#include "GameFramework/HUD.h"
#include "InteractionComponent.h"
#include "KomendantAIController.h"
#include "KomendantCharacter.h"
#include "ObshagaCharacter.h"
#include "ObshagaCharacterConfig.h"
#include "ObshagaEffect.h"
#include "RenderCore.h"
#include "RHI.h"
#include "Obshaga.h"
#include "Components/AudioComponent.h"
#include "ItemActor.h"
#include "ObshagaItemData.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Camera/CameraActor.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Components/PointLightComponent.h"
#include "Engine/PointLight.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SpotLightComponent.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameUserSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ConfigCacheIni.h"
#include "ObshagaGameMode.h"
#include "ObshagaGameState.h"
#include "ObshagaMenu.h"
#include "ObshagaPlayerState.h"
#include "ObshagaSessionSubsystem.h"
#include "ObshagaSettings.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
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
	constexpr float FrameLogSeconds = 5.f;
	// Ник Steam бывает до 32 символов.
	constexpr int32 MaxDisplayNameLength = 32;
	constexpr float LeaveConfirmSeconds = 3.f;
	// Допрос: высота лампы над серединой сцены, время перехода камеры и расстояние, ближе которого камеру сбоку не ставим.
	constexpr float InterrogationLampHeight = 150.f;
	constexpr float InterrogationCameraBlendSeconds = 0.4f;
	constexpr float InterrogationCameraMinDistance = 190.f;
	// Насколько камера допроса выше середины сцены.
	constexpr float InterrogationCameraLift = 35.f;
	/** Камера целится ниже середины сцены и чуть левее: герои встают правее и выше, панель допроса — слева. */
	constexpr float InterrogationLookDrop = 45.f;
	constexpr float InterrogationLookYaw = -13.f;
	// Радиус шара, которым проверяется место для камеры допроса.
	constexpr float InterrogationCameraProbe = 22.f;
	// Камера «через плечо коменданта»: насколько позади него, сбоку и выше его пояса.
	constexpr float ShoulderBack = 100.f;
	constexpr float ShoulderUp = 150.f;
	constexpr float CameraHideSelfDistance = 130.f;
	constexpr float CameraHideHeadHeight = 45.f;

	// Обучение: общий предел и сколько висит последняя подсказка.
	constexpr float TutorialMaxSeconds = 90.f;
	constexpr float TutorialLastStepSeconds = 8.f;
	const TCHAR* TutorialConfigSection = TEXT("Obshaga");
	const TCHAR* TutorialConfigKey = TEXT("bTutorialDone");
	const TCHAR* QualityConfigKey = TEXT("bQualityChosen");

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

void AObshagaPlayerController::StartTour()
{
#if !UE_BUILD_SHIPPING
	UWorld* World = GetWorld();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(Tour), false);
	// Двери: с каждой стороны проёма в 2,5 м (или ближе, если там стена), на высоте глаз, лицом к проёму.
	int32 DoorNumber = 0;
	for (TActorIterator<ADoorActor> It(World); It; ++It, ++DoorNumber)
	{
		const FVector Target = FVector(It->GetDoorCenter().X, It->GetDoorCenter().Y, It->GetActorLocation().Z + 120.f);
		for (const float Side : { 1.f, -1.f })
		{
			const FVector Wanted = Target + It->GetActorRightVector() * Side * 250.f + FVector(0.f, 0.f, 40.f);
			FHitResult Hit;
			Params.ClearIgnoredSourceObjects();
			Params.AddIgnoredActor(*It);
			const bool bBlocked = World->SweepSingleByChannel(Hit, Target + It->GetActorRightVector() * Side * 30.f, Wanted, FQuat::Identity, ECC_Camera, FCollisionShape::MakeSphere(12.f), Params);
			const FVector Location = bBlocked ? Hit.Location : Wanted;
			const ARoomVolume* Room = ARoomVolume::FindRoomAt(this, Location);
			TourViews.Add(FTransform((Target - Location).Rotation(), Location));
			TourNames.Add(FString::Printf(TEXT("door%02d_%s_%s"), DoorNumber, Room ? *Room->RoomId.ToString() : TEXT("outside"), Side > 0.f ? TEXT("a") : TEXT("b")));
			TourDoors.Add(*It);
		}
	}
	// Зоны: из двух противоположных углов, лицом к середине.
	for (TActorIterator<ARoomVolume> It(World); It; ++It)
	{
		const FBox Bounds = It->GetRootComponent()->Bounds.GetBox();
		const FVector Center = Bounds.GetCenter();
		const FVector Target(Center.X, Center.Y, Bounds.Min.Z + 110.f);
		for (const float Corner : { 1.f, -1.f })
		{
			const FVector Location(Center.X + Corner * (Bounds.GetExtent().X - 70.f), Center.Y + Corner * (Bounds.GetExtent().Y - 70.f), Bounds.Min.Z + 190.f);
			TourViews.Add(FTransform((Target - Location).Rotation(), Location));
			TourNames.Add(FString::Printf(TEXT("room_%s_%s"), *It->RoomId.ToString(), Corner > 0.f ? TEXT("a") : TEXT("b")));
			TourDoors.Add(nullptr);
		}
	}
	TourCamera = World->SpawnActor<ACameraActor>();
	if (TourCamera.IsValid())
	{
		TourCamera->GetCameraComponent()->bConstrainAspectRatio = false;
		TourCamera->GetCameraComponent()->SetFieldOfView(100.f);
	}
	if (MyHUD)
	{
		MyHUD->bShowHUD = false;
	}
	TourIndex = 0;
	bTourShot = false;
	UE_LOG(LogObshaga, Log, TEXT("Tour started: %d views"), TourViews.Num());
	GetWorldTimerManager().SetTimer(TourTimer, this, &AObshagaPlayerController::TourStep, 0.7f, true);
#endif
}

void AObshagaPlayerController::TourStep()
{
#if !UE_BUILD_SHIPPING
	// Такт 1 — открыть дверь и поставить камеру; такт 2 — снять и перейти к следующему виду.
	if (!TourViews.IsValidIndex(TourIndex) || !TourCamera.IsValid())
	{
		GetWorldTimerManager().ClearTimer(TourTimer);
		SetViewTarget(GetPawn());
		if (MyHUD)
		{
			MyHUD->bShowHUD = true;
		}
		UE_LOG(LogObshaga, Log, TEXT("Tour done: %d views"), TourViews.Num());
		return;
	}
	ADoorActor* Door = TourDoors[TourIndex].Get();
	if (!bTourShot)
	{
		if (Door)
		{
			Door->OpenFor(TourViews[TourIndex].GetLocation());
		}
		TourCamera->SetActorLocationAndRotation(TourViews[TourIndex].GetLocation(), TourViews[TourIndex].GetRotation());
		SetViewTarget(TourCamera.Get());
		bTourShot = true;
		return;
	}
	const FString File = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Tour"), TourNames[TourIndex] + TEXT(".png")));
	FScreenshotRequest::RequestScreenshot(File, false, false);
	bTourShot = false;
	// Дверь закрываем после второго снимка (со второй стороны).
	if (Door && (!TourDoors.IsValidIndex(TourIndex + 1) || TourDoors[TourIndex + 1].Get() != Door))
	{
		FTimerHandle Unused;
		GetWorldTimerManager().SetTimer(Unused, FTimerDelegate::CreateWeakLambda(Door, [Door]() { Door->ResetDoor(); }), 0.3f, false);
	}
	++TourIndex;
#endif
}

void AObshagaPlayerController::BeginPlay()
{
	Super::BeginPlay();

#if !UE_BUILD_SHIPPING
	if (IsLocalController() && HasAuthority() && FParse::Param(FCommandLine::Get(), TEXT("ObshagaTour")))
	{
		FTimerHandle Unused;
		GetWorldTimerManager().SetTimer(Unused, this, &AObshagaPlayerController::StartTour, 5.f, false);
	}
#endif

	// После меню с мышью возвращаем управление игре: курсор скрыт, клавиши и мышь идут персонажу.
	if (IsLocalController())
	{
		SetInputMode(FInputModeGameOnly());
		SetShowMouseCursor(false);
	}

	// Обучение показывается только при первом запуске игры на этом компьютере.
	bool bTutorialDone = false;
	GConfig->GetBool(TutorialConfigSection, TutorialConfigKey, bTutorialDone, GGameUserSettingsIni);
	// При первом запуске игра сама меряет компьютер и подбирает качество графики: на слабой встроенной
	// видеокарте выйдет «низкое», на игровой — «высокое». Результат сохраняется; дальше игрок правит его сам.
	// В редакторе не трогаем: там это настройки самого редактора.
	bool bQualityChosen = false;
	GConfig->GetBool(TutorialConfigSection, QualityConfigKey, bQualityChosen, GGameUserSettingsIni);
	UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (IsLocalController() && !GIsEditor && !bQualityChosen && Settings)
	{
		Settings->RunHardwareBenchmark();
		Settings->ApplyHardwareBenchmarkResults();
		GConfig->SetBool(TutorialConfigSection, QualityConfigKey, true, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
		UE_LOG(LogObshaga, Log, TEXT("Graphics quality picked by benchmark: overall level %d, resolution %.0f%%"),
			Settings->GetOverallScalabilityLevel(), Settings->GetResolutionScaleNormalized() * 100.f);
	}

	// Имя для остальных игроков: ник Steam (без Steam сервер оставит «Жилец N»).
	const UObshagaSessionSubsystem* Rooms = GetGameInstance() ? GetGameInstance()->GetSubsystem<UObshagaSessionSubsystem>() : nullptr;
	if (IsLocalController() && Rooms)
	{
		const FString Name = Rooms->GetLocalPlayerName();
		if (!Name.IsEmpty())
		{
			ServerSetDisplayName(Name);
		}
	}

	if (IsLocalController() && !bTutorialDone)
	{
		TutorialStep = 0;
		TutorialStartTime = TutorialStepTime = GetWorld()->GetTimeSeconds();
	}
}

void AObshagaPlayerController::ServerSetDisplayName_Implementation(const FString& Wanted)
{
	APlayerState* MyState = GetPlayerState<APlayerState>();
	const AGameStateBase* GameState = GetWorld()->GetGameState();
	if (bDisplayNameSet || !MyState || !GameState)
	{
		return;
	}

	// Ник приходит от клиента, поэтому чистим его здесь: шрифт игры не умеет эмодзи, а управляющие символы ломают строки.
	FString Clean;
	for (const TCHAR Char : Wanted)
	{
		const bool bControl = Char < 0x20 || Char == 0x7F;
		const bool bSurrogate = Char >= 0xD800 && Char <= 0xDFFF;
		const bool bSymbol = (Char >= 0x2190 && Char <= 0x2BFF) || (Char >= 0xE000 && Char <= 0xF8FF) || (Char >= 0xFE00 && Char <= 0xFE0F) || (Char >= 0x200B && Char <= 0x200F);
		if (!bControl && !bSurrogate && !bSymbol)
		{
			Clean.AppendChar(Char);
		}
	}
	Clean.TrimStartAndEndInline();
	Clean.LeftInline(MaxDisplayNameLength);
	Clean.TrimEndInline();
	if (Clean.IsEmpty())
	{
		return;
	}

	// Итоги раунда сопоставляют задания с игроком по имени, поэтому двух одинаковых имён быть не должно.
	auto IsTaken = [GameState, MyState](const FString& Name)
	{
		return GameState->PlayerArray.ContainsByPredicate([&Name, MyState](const APlayerState* Other) { return Other && Other != MyState && Other->GetPlayerName() == Name; });
	};
	FString Unique = Clean;
	for (int32 Number = 2; IsTaken(Unique); ++Number)
	{
		Unique = FString::Printf(TEXT("%s %d"), *Clean, Number);
	}

	bDisplayNameSet = true;
	UE_LOG(LogObshaga, Verbose, TEXT("%s is now called %s"), *MyState->GetPlayerName(), *Unique);
	MyState->SetPlayerName(Unique);
}

void AObshagaPlayerController::ClientReturnToMainMenuWithTextReason_Implementation(const FText& ReturnReason)
{
	if (UObshagaSessionSubsystem* Rooms = GetGameInstance() ? GetGameInstance()->GetSubsystem<UObshagaSessionSubsystem>() : nullptr)
	{
		Rooms->LeaveRoom(ReturnReason);
		return;
	}
	Super::ClientReturnToMainMenuWithTextReason_Implementation(ReturnReason);
}

void AObshagaPlayerController::ServerDebug_Implementation(uint8 What)
{
#if !UE_BUILD_SHIPPING
	AObshagaCharacter* Me = GetObshagaCharacter();
	AObshagaGameMode* GameMode = GetWorld()->GetAuthGameMode<AObshagaGameMode>();
	if (!Me || !GameMode || !FParse::Param(FCommandLine::Get(), TEXT("ObshagaDebugKeys")))
	{
		return;
	}
	if (What == 4 || What == 5)
	{
		// 4 — встать на улице лицом к ночному выходу, 5 — встать на улице подальше от двери (проверка «застрял снаружи»).
		for (TActorIterator<ANightExitDoor> It(GetWorld()); It; ++It)
		{
			const FVector Street = It->GetActorLocation() - It->GetActorForwardVector() * (What == 4 ? 200.f : 600.f) + It->GetActorRightVector() * (What == 4 ? 0.f : 400.f) + FVector(0.f, 0.f, 20.f);
			Me->TeleportTo(Street, It->GetActorForwardVector().Rotation());
			SetControlRotation(It->GetActorForwardVector().Rotation());
			UE_LOG(LogObshaga, Log, TEXT("Debug: %s put outside at %s (exit %s at %s, forward %s)"), *Me->GetName(), *Street.ToCompactString(), *It->GetName(), *It->GetActorLocation().ToCompactString(), *It->GetActorForwardVector().ToCompactString());
			for (TActorIterator<ADoorActor> Door(GetWorld()); Door; ++Door)
			{
				if (FVector::Dist(Door->GetActorLocation(), It->GetActorLocation()) < 600.f)
				{
					UE_LOG(LogObshaga, Log, TEXT("Debug: door %s near the exit at %s, rotation %s"), *Door->GetName(), *Door->GetActorLocation().ToCompactString(), *Door->GetActorRotation().ToCompactString());
				}
			}
			break;
		}
		return;
	}
	if (What == 3)
	{
		// Палыч гонится за ближайшим ботом.
		AObshagaCharacter* Nearest = nullptr;
		for (TActorIterator<AObshagaCharacter> It(GetWorld()); It; ++It)
		{
			const APlayerState* Other = It->GetPlayerState();
			if (Other && Other->IsABot() && !It->IsGhost() && (!Nearest || FVector::Dist(It->GetActorLocation(), Me->GetActorLocation()) < FVector::Dist(Nearest->GetActorLocation(), Me->GetActorLocation())))
			{
				Nearest = *It;
			}
		}
		for (TActorIterator<AKomendantCharacter> It(GetWorld()); It && Nearest; ++It)
		{
			It->SetActorLocation(Nearest->GetActorLocation() + Nearest->GetActorForwardVector() * 120.f, false, nullptr, ETeleportType::TeleportPhysics);
			if (AKomendantAIController* Brain = Cast<AKomendantAIController>(It->GetController()))
			{
				Brain->DebugChase(Nearest);
			}
			break;
		}
		return;
	}
	if (What == 0)
	{
		// Комендант встаёт перед игроком и начинает обычный допрос.
		for (TActorIterator<AKomendantCharacter> It(GetWorld()); It; ++It)
		{
			// Ставим его вплотную и запускаем обычную погоню: поймает он сам, и допрос пойдёт как в игре.
			const FVector Front = Me->GetActorLocation() + Me->GetActorForwardVector() * 120.f;
			It->SetActorLocationAndRotation(Front, (-Me->GetActorForwardVector()).Rotation(), false, nullptr, ETeleportType::TeleportPhysics);
			if (AKomendantAIController* Brain = Cast<AKomendantAIController>(It->GetController()))
			{
				Brain->DebugChase(Me);
			}
			break;
		}
	}
	else
	{
		for (TActorIterator<AItemActor> It(GetWorld()); It; ++It)
		{
			// 1 — тяжёлый предмет (проверка падения после броска), 2 — записка (проверка заданий про слух).
			if (It->GetItemData() && (What == 2 ? It->GetItemData()->bReadable : It->GetItemData()->bHeavy))
			{
				It->SetActorLocation(Me->GetActorLocation() + Me->GetActorForwardVector() * 80.f, false, nullptr, ETeleportType::TeleportPhysics);
				Me->GetCarryComponent()->PickUp(*It);
				break;
			}
		}
	}
#endif
}

void AObshagaPlayerController::UpdateInterrogationScene()
{
	// Допрос — маленькая сцена: над пойманным загорается тёплая лампа (её видят все), а сам пойманный
	// смотрит на себя и коменданта со стороны. Всё это картинка на каждой машине, по сети не ходит.
	const AObshagaGameState* GameState = GetWorld()->GetGameState<AObshagaGameState>();
	const FInterrogationInfo* Info = GameState && GameState->GetInterrogation().bActive ? &GameState->GetInterrogation() : nullptr;
	const AObshagaCharacter* Suspect = Info && Info->Suspect ? Cast<AObshagaCharacter>(Info->Suspect->GetPawn()) : nullptr;
	const AObshagaCharacter* Me = GetObshagaCharacter();
	const UObshagaCharacterConfig* Cfg = Me ? Me->GetConfig() : nullptr;
	const AKomendantCharacter* Komendant = nullptr;
	for (TActorIterator<AKomendantCharacter> It(GetWorld()); It && !Komendant; ++It)
	{
		Komendant = *It;
	}

	const bool bScene = Suspect && Komendant && Cfg;
	if (bScene && !InterrogationLamp && Cfg->InterrogationLampIntensity > 0.f)
	{
		InterrogationLamp = GetWorld()->SpawnActor<APointLight>();
		if (InterrogationLamp)
		{
			UPointLightComponent* Light = InterrogationLamp->PointLightComponent;
			Light->SetMobility(EComponentMobility::Movable);
			Light->bUseInverseSquaredFalloff = false;
			Light->SetLightFalloffExponent(2.f);
			Light->SetCastShadows(false);
		}
	}
	if (InterrogationLamp)
	{
		InterrogationLamp->SetActorHiddenInGame(!bScene);
		InterrogationLamp->PointLightComponent->SetVisibility(bScene);
		if (bScene)
		{
			InterrogationLamp->SetActorLocation((Suspect->GetActorLocation() + Komendant->GetActorLocation()) * 0.5f + FVector(0.f, 0.f, InterrogationLampHeight));
			InterrogationLamp->PointLightComponent->SetIntensity(Cfg->InterrogationLampIntensity);
			InterrogationLamp->PointLightComponent->SetAttenuationRadius(Cfg->InterrogationLampRadius);
			InterrogationLamp->PointLightComponent->SetLightColor(Cfg->InterrogationLampColor);
		}
	}

	// Камера сбоку — только у самого пойманного.
	const bool bMine = bScene && Suspect == Me && !bIntroCamera;
	if (bMine && !bInterrogationCamera)
	{
		const FVector Middle = (Suspect->GetActorLocation() + Komendant->GetActorLocation()) * 0.5f + FVector(0.f, 0.f, Cfg->InterrogationCameraHeight);
		const FVector Between = (Komendant->GetActorLocation() - Suspect->GetActorLocation()).GetSafeNormal2D();
		const FVector Forward = Between.IsNearlyZero() ? Suspect->GetActorForwardVector() : Between;
		// Лучше всего смотреть сбоку, но в узком коридоре сбоку стена. Поэтому пробуем по очереди: сбоку,
		// потом наискосок со стороны пойманного и со стороны коменданта — и берём первое место, где камере
		// хватает места, а если такого нет — самое просторное. Камера стоит выше голов и смотрит чуть вниз.
		FVector Best = FVector::ZeroVector;
		float BestDistance = InterrogationCameraMinDistance;
		for (const float Angle : { 90.f, -90.f, 125.f, -125.f, 55.f, -55.f })
		{
			if (BestDistance >= Cfg->InterrogationCameraDistance * 0.85f)
			{
				break;
			}
			const FVector Direction = Forward.RotateAngleAxis(Angle, FVector::UpVector);
			const FVector Wanted = Middle + Direction * Cfg->InterrogationCameraDistance + FVector(0.f, 0.f, InterrogationCameraLift);
			FHitResult Hit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(InterrogationCamera), false);
			Params.AddIgnoredActor(Suspect);
			Params.AddIgnoredActor(Komendant);
			// Шаром, а не лучом: луч проходил в открытую дверь, и камера вставала в соседней комнате за косяком.
			const bool bBlocked = GetWorld()->SweepSingleByChannel(Hit, Middle, Wanted, FQuat::Identity, ECC_Camera, FCollisionShape::MakeSphere(InterrogationCameraProbe), Params);
			const float Full = FVector::Dist(Middle, Wanted);
			const float Distance = bBlocked ? Hit.Distance - 10.f : Full;
			if (Distance <= BestDistance)
			{
				continue;
			}
			// С выбранного места должно быть видно обоих — иначе это место не годится.
			const FVector Candidate = Middle + (Wanted - Middle).GetSafeNormal() * Distance;
			FHitResult SightHit;
			if (GetWorld()->LineTraceSingleByChannel(SightHit, Candidate, Suspect->GetActorLocation(), ECC_Camera, Params)
				|| GetWorld()->LineTraceSingleByChannel(SightHit, Candidate, Komendant->GetActorLocation(), ECC_Camera, Params))
			{
				continue;
			}
			BestDistance = Distance;
			Best = Candidate;
			UE_LOG(LogObshaga, Verbose, TEXT("Interrogation camera: side %.0f deg, %.0f cm"), Angle, Distance);
		}
		if (Best.IsZero())
		{
			// Сбоку места нет (узкий коридор): смотрим через плечо коменданта на пойманного, лицом к лицу.
			const FVector Side = FVector::CrossProduct(Forward, FVector::UpVector);
			FCollisionQueryParams Params(SCENE_QUERY_STAT(InterrogationShoulder), false);
			Params.AddIgnoredActor(Suspect);
			Params.AddIgnoredActor(Komendant);
			// Чем дальше камера вбок, тем меньше спина коменданта закрывает пойманного; сначала правое плечо
			// (комендант встаёт в кадре слева, под панелью допроса), потом левое, потом ближе к спине.
			for (const float Offset : { 115.f, -115.f, 85.f, -85.f })
			{
				const FVector Shoulder = Komendant->GetActorLocation() + Forward * ShoulderBack + Side * Offset + FVector(0.f, 0.f, ShoulderUp);
				FHitResult Hit;
				if (!GetWorld()->SweepSingleByChannel(Hit, Komendant->GetActorLocation(), Shoulder, FQuat::Identity, ECC_Camera, FCollisionShape::MakeSphere(InterrogationCameraProbe), Params))
				{
					Best = Shoulder;
					break;
				}
			}
			UE_LOG(LogObshaga, Verbose, TEXT("Interrogation camera: no room at the side, over the shoulder %s"), Best.IsZero() ? TEXT("blocked too") : TEXT("ok"));
		}
		if (!Best.IsZero())
		{
			if (!InterrogationCamera)
			{
				InterrogationCamera = GetWorld()->SpawnActor<ACameraActor>();
				if (InterrogationCamera)
				{
					InterrogationCamera->GetCameraComponent()->bConstrainAspectRatio = false;
				}
			}
			if (InterrogationCamera)
			{
				// Целимся в голову пойманного, если смотрим из-за плеча, и в середину сцены, если сбоку.
				const bool bShoulder = FVector::Dist2D(Best, Komendant->GetActorLocation()) < 160.f;
				const FVector LookAt = bShoulder ? Suspect->GetActorLocation() : Middle - FVector(0.f, 0.f, InterrogationLookDrop);
				FRotator Look = (LookAt - Best).Rotation();
				Look.Yaw += bShoulder ? 0.f : InterrogationLookYaw;
				InterrogationCamera->SetActorLocationAndRotation(Best, Look);
				SetViewTargetWithBlend(InterrogationCamera, InterrogationCameraBlendSeconds);
				bInterrogationCamera = true;
			}
		}
	}
	else if (!bMine && bInterrogationCamera)
	{
		bInterrogationCamera = false;
		SetViewTargetWithBlend(GetPawn(), InterrogationCameraBlendSeconds);
	}
}

void AObshagaPlayerController::UpdateIntro()
{
	const AObshagaGameState* GameState = GetWorld()->GetGameState<AObshagaGameState>();
	const bool bActive = GameState && GameState->IsIntroActive();
	if (!bActive)
	{
		bIntroSkipped = false;
	}
	const bool bShow = bActive && !bIntroSkipped;
	if (bShow != bIntroCamera)
	{
		bIntroCamera = bShow;
		SetIgnoreMoveInput(bShow);
		if (bShow && !IntroCamera)
		{
			IntroCamera = GetWorld()->SpawnActor<ACameraActor>();
			if (IntroCamera)
			{
				IntroCamera->GetCameraComponent()->bConstrainAspectRatio = false;
			}
		}
		SetViewTarget(bShow && IntroCamera ? static_cast<AActor*>(IntroCamera) : static_cast<AActor*>(GetPawn()));
	}
	if (!bShow || !IntroCamera)
	{
		return;
	}

	FVector Location;
	FRotator Rotation;
	if (ObshagaFlyover::GetView(GetWorld(), GameState->GetIntroProgress(), Location, Rotation))
	{
		IntroCamera->SetActorLocationAndRotation(Location, Rotation);
	}
}

bool AObshagaPlayerController::InputKey(const FInputKeyEventArgs& Params)
{
	// Заставку можно пропустить пробелом: у себя — сразу, а хост пропускает её всем.
	if (bIntroCamera && Params.Event == IE_Pressed && Params.Key == EKeys::SpaceBar)
	{
		bIntroSkipped = true;
		if (GetNetMode() != NM_Client)
		{
			ServerRequestStart();
		}
		return true;
	}

#if !UE_BUILD_SHIPPING
	// Проверочные клавиши: K — комендант сразу ловит меня, L — тяжёлый предмет мне в руки.
	if (Params.Event == IE_Pressed && (Params.Key == EKeys::K || Params.Key == EKeys::L || Params.Key == EKeys::N || Params.Key == EKeys::U || Params.Key == EKeys::O || Params.Key == EKeys::P) && FParse::Param(FCommandLine::Get(), TEXT("ObshagaDebugKeys")))
	{
		ServerDebug(Params.Key == EKeys::K ? 0 : (Params.Key == EKeys::L ? 1 : (Params.Key == EKeys::N ? 2 : (Params.Key == EKeys::U ? 3 : (Params.Key == EKeys::O ? 4 : 5)))));
		return true;
	}
	// J — смотреть по очереди на других жильцов (ботов), ещё раз после последнего — снова на себя.
	if (Params.Event == IE_Pressed && Params.Key == EKeys::J && FParse::Param(FCommandLine::Get(), TEXT("ObshagaDebugKeys")))
	{
		TArray<AActor*> Others;
		for (TActorIterator<AObshagaCharacter> It(GetWorld()); It; ++It)
		{
			if (*It != GetPawn())
			{
				Others.Add(*It);
			}
		}
		const int32 Current = Others.IndexOfByKey(GetViewTarget());
		AActor* Next = Others.IsValidIndex(Current + 1) ? Others[Current + 1] : GetPawn();
		SetViewTarget(Next);
		UE_LOG(LogObshaga, Log, TEXT("Debug view: %s"), Next && Cast<APawn>(Next) && Cast<APawn>(Next)->GetPlayerState() ? *Cast<APawn>(Next)->GetPlayerState()->GetPlayerName() : TEXT("self"));
		return true;
	}
#endif

	// F12 — снимок экрана в папку «Изображения\Obshaga» (экран итогов сделан, чтобы им делились).
	if (Params.Event == IE_Pressed && Params.Key == EKeys::F12)
	{
		const FString Folder = FPaths::Combine(FPlatformProcess::UserDir(), TEXT("../Pictures/Obshaga"));
		const FString File = FPaths::ConvertRelativePathToFull(FPaths::Combine(Folder, FString::Printf(TEXT("Obshaga_%s.png"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")))));
		IFileManager::Get().MakeDirectory(*Folder, true);
		// С интерфейсом: на снимке должны быть газета и итоги, а не только комната.
		FScreenshotRequest::RequestScreenshot(File, true, false);
		FString Shown = File;
		FPaths::MakePlatformFilename(Shown);
		ClientShowNotice_Implementation(FText::Format(NSLOCTEXT("ObshagaRoom", "Screenshot", "Снимок сохранён: {0}"), FText::FromString(Shown)));
		ScreenshotPath = Shown;
		ScreenshotTime = GetWorld()->GetRealTimeSeconds();
		return true;
	}

	// Esc открывает и закрывает настройки; пока они открыты, цифры меняют настройки, а не выбирают фразы.
	if (Params.Event == IE_Pressed && Params.Key == EKeys::Escape)
	{
		bSettingsOpen = !bSettingsOpen;
		return true;
	}
	if (bSettingsOpen && Params.Event == IE_Pressed)
	{
		static const FKey OptionKeys[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five };
		for (int32 Option = 0; Option < ObshagaSettings::NumOptions; ++Option)
		{
			if (Params.Key == OptionKeys[Option])
			{
				ObshagaSettings::Step(Option);
				return true;
			}
		}
	}

	// Выход в меню — со второго нажатия F10 подряд, чтобы хост не закрыл комнату всем случайной клавишей.
	UObshagaSessionSubsystem* Rooms = GetGameInstance() ? GetGameInstance()->GetSubsystem<UObshagaSessionSubsystem>() : nullptr;
	if (Params.Key == EKeys::F10 && Params.Event == IE_Pressed && Rooms && !Rooms->GetRoomCode().IsEmpty())
	{
		const float Now = GetWorld()->GetRealTimeSeconds();
		if (Now - LeavePressTime < LeaveConfirmSeconds)
		{
			Rooms->LeaveRoom();
		}
		else
		{
			LeavePressTime = Now;
			ClientShowNotice_Implementation(GetNetMode() == NM_Client
				? NSLOCTEXT("ObshagaRoom", "LeaveConfirm", "Нажми F10 ещё раз, чтобы выйти в меню")
				: NSLOCTEXT("ObshagaRoom", "LeaveConfirmHost", "Нажми F10 ещё раз: комната закроется для всех"));
		}
		return true;
	}
	return Super::InputKey(Params);
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
			// Момент начала погони ловим до отсева по этажу, иначе колокол звякал бы при каждой смене этажа.
			const bool bChasing = It->GetAlert() == EKomendantAlert::Chasing;
			const bool bChaseJustStarted = bChasing && !bKomendantWasChasing;
			bKomendantWasChasing = bChasing;

			const FVector Delta = It->GetActorLocation() - Me->GetActorLocation();
			if (FMath::Abs(Delta.Z) > DangerSameFloorHeight)
			{
				continue;
			}
			float Level = 1.f - Delta.Size2D() / Config->DangerRadius;
			if (bChasing)
			{
				Level *= DangerChaseBoost;
			}
			// Погоня только что началась на твоём этаже — колокол слышно оттуда, где стоит комендант.
			if (bChaseJustStarted)
			{
				UObshagaAudioConfig::PlayAt(this, UObshagaAudioConfig::Get()->ChaseStart, It->GetActorLocation());
			}
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
	else
	{
		// Вне раунда погони нет: следующий раунд начнётся с чистого листа.
		bKomendantWasChasing = false;
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

	FVector ListenerLocation;
	FRotator ListenerRotation;
	GetPlayerViewPoint(ListenerLocation, ListenerRotation);
	for (TActorIterator<ACharacter> It(GetWorld()); It; ++It)
	{
		const ACharacter* Walker = *It;
		const AObshagaCharacter* Resident = Cast<AObshagaCharacter>(Walker);
		FVector& LastLocation = FootstepLastLocation.FindOrAdd(Walker, Walker->GetActorLocation());
		float& Travelled = FootstepTravelled.FindOrAdd(Walker, 0.f);

		const float Step = FVector::Dist2D(Walker->GetActorLocation(), LastLocation);
		LastLocation = Walker->GetActorLocation();
		// Чужие шаги слышно только рядом: дальше радиуса слышимости их нет вовсе.
		if (FVector::DistSquared(Walker->GetActorLocation(), ListenerLocation) > FMath::Square(Audio->WorldSoundRadius))
		{
			Travelled = 0.f;
			continue;
		}
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
		Current = FMath::FInterpTo(Current, Target * Audio->MusicVolume * ObshagaSettings::MusicVolume(), DeltaTime, MusicFadeSpeed);
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

	// Вместе со звуком — картинка: искры от сломанного прибора, пыль от тяжёлого удара.
	if (Kind == ENoiseKind::Device)
	{
		AObshagaEffect::Play(this, EObshagaEffect::Sparks, Location);
	}
	else if (Kind == ENoiseKind::Impact && Loudness >= Audio->HeavyImpactLoudness)
	{
		AObshagaEffect::Play(this, EObshagaEffect::Dust, Location);
	}
	UE_LOG(LogObshaga, Verbose, TEXT("[%s] Heard noise %.2f kind %d at %s"), GetNetMode() == NM_Client ? TEXT("client") : TEXT("host"), Loudness, static_cast<int32>(Kind), *FVector(Location).ToCompactString());

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
		// Прижался спиной к стене — камера въезжает в собственную голову и весь экран закрывает затылок.
		// Пока камера так близко, свою фигурку игрок не видит (остальные видят её как обычно).
		if (ACharacter* Me = GetCharacter())
		{
			// Меряем до головы, а не до пояса: именно она закрывает экран.
			const FVector Head = Me->GetActorLocation() + FVector(0.f, 0.f, CameraHideHeadHeight);
			const bool bTooClose = PlayerCameraManager && FVector::DistSquared(PlayerCameraManager->GetCameraLocation(), Head) < FMath::Square(CameraHideSelfDistance);
			const bool bHideSelf = bTooClose && !bInterrogationCamera;
			if (Me->GetMesh()->bOwnerNoSee != bHideSelf)
			{
				Me->GetMesh()->SetOwnerNoSee(bHideSelf);
			}
		}

		UpdateIntro();
		UpdateInterrogationScene();

		// «Выбило пробки»: у каждого жильца в руке фонарик. Состояние события у всех одно, поэтому каждая машина
		// зажигает фонарики сама — рассылать по сети нечего.
		const AObshagaGameState* LightState = GetWorld()->GetGameState<AObshagaGameState>();
		const bool bFlashlights = LightState && LightState->IsBlackout() && LightState->GetFlashlightIntensity() > 0.f;
		if (bFlashlights || bFlashlightsOn)
		{
			bFlashlightsOn = bFlashlights;
			for (TActorIterator<AObshagaCharacter> It(GetWorld()); It; ++It)
			{
				static const FName FlashlightName(TEXT("Flashlight"));
				// Фонарик — луч вперёд от головы, а не лампочка: вне луча комната остаётся тёмной.
				USpotLightComponent* Flashlight = FindObject<USpotLightComponent>(*It, *FlashlightName.ToString());
				if (!Flashlight && bFlashlights)
				{
					Flashlight = NewObject<USpotLightComponent>(*It, FlashlightName);
					Flashlight->bUseInverseSquaredFalloff = false;
					Flashlight->SetupAttachment(It->GetRootComponent());
					Flashlight->RegisterComponent();
					Flashlight->SetRelativeLocation(FVector(35.f, 0.f, 45.f));
					Flashlight->SetRelativeRotation(FRotator(-6.f, 0.f, 0.f));
					Flashlight->SetLightFalloffExponent(2.f);
					Flashlight->SetCastShadows(false);
					Flashlight->SetLightColor(FLinearColor(1.f, 0.92f, 0.75f));
				}
				if (Flashlight)
				{
					Flashlight->SetIntensity(LightState ? LightState->GetFlashlightIntensity() : 0.f);
					Flashlight->SetAttenuationRadius(LightState ? LightState->GetFlashlightRadius() : 100.f);
					const float Cone = LightState ? LightState->GetFlashlightCone() : 28.f;
					Flashlight->SetOuterConeAngle(Cone);
					Flashlight->SetInnerConeAngle(Cone * 0.5f);
					Flashlight->SetVisibility(bFlashlights && !It->IsGhost() && !It->IsHiding());
				}
			}
		}

		// Замер частоты кадров в журнал раз в несколько секунд — для самопроверки (уровень Verbose).
		FrameSampleSeconds += DeltaTime;
		++FrameSampleCount;
		if (FrameSampleSeconds >= FrameLogSeconds)
		{
			UE_LOG(LogObshaga, Verbose, TEXT("[%s] FPS %.0f (frame %.1f ms; game %.1f, draw %.1f, gpu %.1f)"), GetNetMode() == NM_Client ? TEXT("client") : TEXT("host"),
				FrameSampleCount / FrameSampleSeconds, 1000.f * FrameSampleSeconds / FrameSampleCount,
				FPlatformTime::ToMilliseconds(GGameThreadTime), FPlatformTime::ToMilliseconds(GRenderThreadTime), FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()));
			FrameSampleSeconds = 0.f;
			FrameSampleCount = 0;
		}

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
