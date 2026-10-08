#include "ObshagaMenu.h"

#include "ObshagaSessionSubsystem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Kismet/KismetSystemLibrary.h"

#define LOCTEXT_NAMESPACE "ObshagaMenu"

namespace
{
	/** Символ кода комнаты для клавиши. По клавише, а не по букве: русская раскладка вводу не мешает. */
	TCHAR KeyToCodeChar(const FKey& Key)
	{
		static const TMap<FKey, TCHAR> Digits = {
			{ EKeys::Two, TEXT('2') }, { EKeys::Three, TEXT('3') }, { EKeys::Four, TEXT('4') }, { EKeys::Five, TEXT('5') },
			{ EKeys::Six, TEXT('6') }, { EKeys::Seven, TEXT('7') }, { EKeys::Eight, TEXT('8') }, { EKeys::Nine, TEXT('9') },
			{ EKeys::NumPadTwo, TEXT('2') }, { EKeys::NumPadThree, TEXT('3') }, { EKeys::NumPadFour, TEXT('4') }, { EKeys::NumPadFive, TEXT('5') },
			{ EKeys::NumPadSix, TEXT('6') }, { EKeys::NumPadSeven, TEXT('7') }, { EKeys::NumPadEight, TEXT('8') }, { EKeys::NumPadNine, TEXT('9') },
		};
		if (const TCHAR* Digit = Digits.Find(Key))
		{
			return *Digit;
		}
		const FString Name = Key.GetFName().ToString();
		return Name.Len() == 1 && UObshagaSessionSubsystem::IsCodeChar(Name[0]) ? Name[0] : TCHAR(0);
	}
}

AObshagaMenuGameMode::AObshagaMenuGameMode()
{
	DefaultPawnClass = nullptr;
	PlayerControllerClass = AObshagaMenuController::StaticClass();
	HUDClass = AObshagaMenuHUD::StaticClass();
}

bool AObshagaMenuController::InputKey(const FInputKeyEventArgs& Params)
{
	UObshagaSessionSubsystem* Rooms = GetGameInstance() ? GetGameInstance()->GetSubsystem<UObshagaSessionSubsystem>() : nullptr;
	if (!Rooms || Params.Event != IE_Pressed || Rooms->IsBusy())
	{
		return Super::InputKey(Params);
	}

	const FKey Key = Params.Key;
	if (!bEnteringCode)
	{
		if (Key == EKeys::One || Key == EKeys::NumPadOne)
		{
			Rooms->CreateRoom();
		}
		else if (Key == EKeys::Two || Key == EKeys::NumPadTwo)
		{
			Rooms->SetMessage(FText::GetEmpty());
			bEnteringCode = true;
			TypedCode.Reset();
		}
		else if (Key == EKeys::Escape)
		{
			UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, false);
		}
		else
		{
			return Super::InputKey(Params);
		}
		return true;
	}

	if (Key == EKeys::Escape)
	{
		bEnteringCode = false;
	}
	else if (Key == EKeys::BackSpace)
	{
		TypedCode.LeftChopInline(1);
	}
	else if (Key == EKeys::Enter)
	{
		// Остаёмся на экране ввода: если код не подошёл, его можно поправить, а не набирать заново.
		Rooms->JoinByCode(TypedCode);
	}
	else if (Key == EKeys::V && (IsInputKeyDown(EKeys::LeftControl) || IsInputKeyDown(EKeys::RightControl)))
	{
		// Код обычно присылают в переписке — даём вставить.
		FString Pasted;
		FPlatformApplicationMisc::ClipboardPaste(Pasted);
		TypedCode = UObshagaSessionSubsystem::NormalizeCode(Pasted);
	}
	else if (const TCHAR Char = KeyToCodeChar(Key))
	{
		if (TypedCode.Len() < UObshagaSessionSubsystem::CodeLength)
		{
			TypedCode.AppendChar(Char);
		}
	}
	else
	{
		return Super::InputKey(Params);
	}
	return true;
}

void AObshagaMenuHUD::DrawLine(const FString& Line, const FLinearColor& Color, float YFraction, float SizeScale)
{
	UFont* Font = GEngine->GetMediumFont();
	const float Scale = FMath::Max(1.f, Canvas->ClipY / 540.f) * SizeScale;
	float Width = 0.f;
	float Height = 0.f;
	GetTextSize(Line, Width, Height, Font, Scale);
	DrawText(Line, Color, (Canvas->ClipX - Width) * 0.5f, Canvas->ClipY * YFraction, Font, Scale);
}

void AObshagaMenuHUD::DrawHUD()
{
	Super::DrawHUD();

	const AObshagaMenuController* Controller = Cast<AObshagaMenuController>(GetOwningPlayerController());
	const UObshagaSessionSubsystem* Rooms = GetGameInstance() ? GetGameInstance()->GetSubsystem<UObshagaSessionSubsystem>() : nullptr;
	if (!Canvas || !Controller || !Rooms)
	{
		return;
	}

	const FLinearColor Dim(0.75f, 0.75f, 0.82f);
	DrawRect(FLinearColor(0.03f, 0.03f, 0.07f, 1.f), 0.f, 0.f, Canvas->ClipX, Canvas->ClipY);
	DrawLine(LOCTEXT("Title", "ОБЩАГА — НЕ СПАЛИМСЯ").ToString(), FLinearColor::Yellow, 0.16f, 2.f);

	switch (Rooms->GetBusy())
	{
	case ERoomBusy::Creating:
		DrawLine(LOCTEXT("Creating", "Создаём комнату…").ToString(), FLinearColor::White, 0.42f);
		break;
	case ERoomBusy::Searching:
		DrawLine(LOCTEXT("Searching", "Ищем комнату…").ToString(), FLinearColor::White, 0.42f);
		break;
	case ERoomBusy::Joining:
		DrawLine(LOCTEXT("Joining", "Заходим в комнату…").ToString(), FLinearColor::White, 0.42f);
		break;
	case ERoomBusy::Leaving:
		DrawLine(LOCTEXT("Leaving", "Выходим…").ToString(), FLinearColor::White, 0.42f);
		break;
	default:
		if (Controller->IsEnteringCode())
		{
			// Ненабранные символы показываем подчёркиванием, чтобы была видна длина кода.
			FString Shown = Controller->GetTypedCode();
			while (Shown.Len() < UObshagaSessionSubsystem::CodeLength)
			{
				Shown.AppendChar(TEXT('_'));
			}
			DrawLine(LOCTEXT("EnterCode", "Код комнаты (его видит хост у себя на экране):").ToString(), FLinearColor::White, 0.36f);
			FString Spaced;
			for (const TCHAR Char : Shown)
			{
				Spaced += Spaced.IsEmpty() ? FString() : FString(TEXT(" "));
				Spaced.AppendChar(Char);
			}
			DrawLine(Spaced, FLinearColor::Yellow, 0.43f, 2.f);
			DrawLine(LOCTEXT("EnterHint", "[Enter] войти   [Backspace] стереть   [Ctrl+V] вставить   [Esc] назад").ToString(), Dim, 0.56f);
		}
		else
		{
			DrawLine(LOCTEXT("ItemCreate", "[1] Создать комнату").ToString(), FLinearColor::White, 0.38f, 1.3f);
			DrawLine(LOCTEXT("ItemJoin", "[2] Войти по коду").ToString(), FLinearColor::White, 0.46f, 1.3f);
			DrawLine(LOCTEXT("ItemQuit", "[Esc] Выход").ToString(), FLinearColor::White, 0.54f, 1.3f);
		}
		break;
	}

	if (!Rooms->GetMessage().IsEmpty())
	{
		DrawLine(Rooms->GetMessage().ToString(), FLinearColor(1.f, 0.55f, 0.55f), 0.68f);
	}

	const FString Name = Rooms->GetLocalPlayerName();
	const FText Network = Rooms->IsSteam()
		? FText::Format(LOCTEXT("NetSteam", "Сеть: Steam. Ты — {0}"), FText::FromString(Name))
		: LOCTEXT("NetLan", "Steam не запущен: комнаты видны только в твоей локальной сети");
	DrawLine(Network.ToString(), Dim, 0.9f, 0.9f);
}

#undef LOCTEXT_NAMESPACE
