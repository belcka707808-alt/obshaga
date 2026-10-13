#include "ObshagaWidgets.h"

#include "ObshagaMenu.h"
#include "ObshagaSessionSubsystem.h"
#include "ObshagaSettings.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"

#define LOCTEXT_NAMESPACE "ObshagaWidgets"

namespace
{
	const FLinearColor PanelColor(0.03f, 0.03f, 0.07f, 1.f);
	const FLinearColor ButtonColor(0.16f, 0.17f, 0.34f, 1.f);
	const FLinearColor DimColor(0.75f, 0.75f, 0.82f);
	const FLinearColor ErrorColor(1.f, 0.55f, 0.55f);

	void SetFontSize(UTextBlock* Text, int32 Size)
	{
		FSlateFontInfo Font = Text->GetFont();
		Font.Size = Size;
		Text->SetFont(Font);
	}

	void Show(UWidget* Widget, bool bVisible)
	{
		Widget->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	/** Текст виден, но мышь его не ловит. */
	void ShowText(UTextBlock* Text, const FText& Value)
	{
		Text->SetText(Value);
		Text->SetVisibility(Value.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
}

TSharedRef<SWidget> UObshagaMenuWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		Build();
	}
	return Super::RebuildWidget();
}

UTextBlock* UObshagaMenuWidget::AddText(UVerticalBox* Box, int32 FontSize, const FLinearColor& Color, float TopPadding)
{
	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>();
	SetFontSize(Text, FontSize);
	Text->SetColorAndOpacity(FSlateColor(Color));
	Text->SetJustification(ETextJustify::Center);
	UVerticalBoxSlot* TextSlot = Box->AddChildToVerticalBox(Text);
	TextSlot->SetHorizontalAlignment(HAlign_Center);
	TextSlot->SetPadding(FMargin(0.f, TopPadding, 0.f, 0.f));
	return Text;
}

void UObshagaMenuWidget::Build()
{
	// Тёмный фон на весь экран, по центру — столбик: название, состояние, кнопки, подсказка, сообщение.
	UBorder* Background = WidgetTree->ConstructWidget<UBorder>();
	Background->SetBrushColor(PanelColor);
	Background->SetHorizontalAlignment(HAlign_Center);
	Background->SetVerticalAlignment(VAlign_Center);
	WidgetTree->RootWidget = Background;

	UVerticalBox* Box = WidgetTree->ConstructWidget<UVerticalBox>();
	Background->SetContent(Box);

	UTextBlock* Title = AddText(Box, 44, FLinearColor::Yellow, 0.f);
	Title->SetText(LOCTEXT("Title", "ОБЩАГА — НЕ СПАЛИМСЯ"));
	Title->SetVisibility(ESlateVisibility::HitTestInvisible);
	Status = AddText(Box, 30, FLinearColor::White, 36.f);

	using FClick = void (UObshagaMenuWidget::*)();
	const FName Handlers[NumButtons] = { GET_FUNCTION_NAME_CHECKED(UObshagaMenuWidget, OnButton0), GET_FUNCTION_NAME_CHECKED(UObshagaMenuWidget, OnButton1),
		GET_FUNCTION_NAME_CHECKED(UObshagaMenuWidget, OnButton2), GET_FUNCTION_NAME_CHECKED(UObshagaMenuWidget, OnButton3),
		GET_FUNCTION_NAME_CHECKED(UObshagaMenuWidget, OnButton4), GET_FUNCTION_NAME_CHECKED(UObshagaMenuWidget, OnButton5) };
	ButtonKeys.SetNum(NumButtons);
	for (int32 Index = 0; Index < NumButtons; ++Index)
	{
		UButton* Button = WidgetTree->ConstructWidget<UButton>();
		Button->SetBackgroundColor(ButtonColor);
		FScriptDelegate Delegate;
		Delegate.BindUFunction(this, Handlers[Index]);
		Button->OnClicked.Add(Delegate);
		UVerticalBoxSlot* ButtonSlot = Box->AddChildToVerticalBox(Button);
		ButtonSlot->SetHorizontalAlignment(HAlign_Fill);
		ButtonSlot->SetPadding(FMargin(0.f, 10.f, 0.f, 0.f));

		UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>();
		SetFontSize(Text, 26);
		Text->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		Text->SetJustification(ETextJustify::Center);
		Button->SetContent(Text);

		Buttons.Add(Button);
		ButtonTexts.Add(Text);
	}

	Hint = AddText(Box, 20, DimColor, 22.f);
	Message = AddText(Box, 22, ErrorColor, 22.f);
	Network = AddText(Box, 18, DimColor, 40.f);
}

void UObshagaMenuWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	const AObshagaMenuController* Controller = Cast<AObshagaMenuController>(GetOwningPlayer());
	const UObshagaSessionSubsystem* Rooms = GetGameInstance() ? GetGameInstance()->GetSubsystem<UObshagaSessionSubsystem>() : nullptr;
	if (!Controller || !Rooms || !Status)
	{
		return;
	}

	// Что показать на текущем экране меню: строка состояния, кнопки (подпись и клавиша) и подсказка.
	FText StatusText;
	FText HintText;
	TArray<TPair<FText, FKey>> Items;
	switch (Rooms->GetBusy())
	{
	case ERoomBusy::Creating:
		StatusText = LOCTEXT("Creating", "Создаём комнату…");
		break;
	case ERoomBusy::Searching:
		StatusText = LOCTEXT("Searching", "Ищем комнату…");
		break;
	case ERoomBusy::Joining:
		StatusText = LOCTEXT("Joining", "Заходим в комнату…");
		break;
	case ERoomBusy::Leaving:
		StatusText = LOCTEXT("Leaving", "Выходим…");
		break;
	default:
		if (Controller->IsInSettings())
		{
			static const FKey OptionKeys[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five };
			StatusText = LOCTEXT("SettingsTitle", "НАСТРОЙКИ");
			for (int32 Option = 0; Option < ObshagaSettings::NumOptions; ++Option)
			{
				Items.Emplace(ObshagaSettings::Describe(Option), OptionKeys[Option]);
			}
			Items.Emplace(LOCTEXT("Back", "[Esc] Назад"), EKeys::Escape);
			HintText = LOCTEXT("SettingsHint", "Нажми на строку или её цифру, чтобы поменять настройку");
		}
		else if (Controller->IsEnteringCode())
		{
			// Ненабранные символы показываем подчёркиванием, чтобы была видна длина кода.
			FString Shown = Controller->GetTypedCode();
			while (Shown.Len() < UObshagaSessionSubsystem::CodeLength)
			{
				Shown.AppendChar(TEXT('_'));
			}
			FString Spaced;
			for (const TCHAR Char : Shown)
			{
				Spaced += Spaced.IsEmpty() ? FString() : FString(TEXT("  "));
				Spaced.AppendChar(Char);
			}
			StatusText = FText::FromString(Spaced);
			Items.Emplace(LOCTEXT("JoinNow", "[Enter] Войти"), EKeys::Enter);
			Items.Emplace(LOCTEXT("Back", "[Esc] Назад"), EKeys::Escape);
			HintText = LOCTEXT("EnterHint", "Набери код комнаты на клавиатуре — его видит хост у себя на экране.  [Ctrl+V] вставить");
		}
		else
		{
			Items.Emplace(LOCTEXT("ItemCreate", "[1] Создать комнату"), EKeys::One);
			Items.Emplace(LOCTEXT("ItemJoin", "[2] Войти по коду"), EKeys::Two);
			Items.Emplace(LOCTEXT("ItemSettings", "[3] Настройки"), EKeys::Three);
			Items.Emplace(LOCTEXT("ItemQuit", "[Esc] Выход"), EKeys::Escape);
		}
		break;
	}

	ShowText(Status, StatusText);
	ShowText(Hint, HintText);
	ShowText(Message, Rooms->GetMessage());
	for (int32 Index = 0; Index < NumButtons; ++Index)
	{
		const bool bUsed = Items.IsValidIndex(Index);
		Show(Buttons[Index], bUsed);
		if (bUsed)
		{
			ButtonTexts[Index]->SetText(Items[Index].Key);
			ButtonKeys[Index] = Items[Index].Value;
		}
	}
	ShowText(Network, Rooms->IsSteam()
		? FText::Format(LOCTEXT("NetSteam", "Сеть: Steam. Ты — {0}"), FText::FromString(Rooms->GetLocalPlayerName()))
		: LOCTEXT("NetLan", "Steam не запущен: комнаты видны только в твоей локальной сети"));
}

void UObshagaMenuWidget::Click(int32 Index)
{
	// Кнопка забрала бы клавиатуру себе — возвращаем её игре, иначе после щелчка перестал бы работать ввод кода.
	UWidgetBlueprintLibrary::SetFocusToGameViewport();
	AObshagaMenuController* Controller = Cast<AObshagaMenuController>(GetOwningPlayer());
	if (Controller && ButtonKeys.IsValidIndex(Index))
	{
		Controller->HandleMenuKey(ButtonKeys[Index]);
	}
}

void UObshagaMenuWidget::OnButton0() { Click(0); }
void UObshagaMenuWidget::OnButton1() { Click(1); }
void UObshagaMenuWidget::OnButton2() { Click(2); }
void UObshagaMenuWidget::OnButton3() { Click(3); }
void UObshagaMenuWidget::OnButton4() { Click(4); }
void UObshagaMenuWidget::OnButton5() { Click(5); }

TSharedRef<SWidget> UObshagaPhoneWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		Build();
	}
	return Super::RebuildWidget();
}

void UObshagaPhoneWidget::Build()
{
	// Панель у правого края экрана, на всю высоту с отступами; внутри — столбик строк.
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>();
	WidgetTree->RootWidget = Root;

	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>();
	Panel->SetBrushColor(FLinearColor(0.02f, 0.02f, 0.05f, 0.88f));
	Panel->SetPadding(FMargin(18.f, 14.f));
	UCanvasPanelSlot* PanelSlot = Root->AddChildToCanvas(Panel);
	PanelSlot->SetAnchors(FAnchors(1.f, 0.f, 1.f, 1.f));
	PanelSlot->SetAlignment(FVector2D(1.f, 0.f));
	PanelSlot->SetOffsets(FMargin(-20.f, 110.f, 620.f, 90.f));

	UVerticalBox* Box = WidgetTree->ConstructWidget<UVerticalBox>();
	Panel->SetContent(Box);
	for (int32 Index = 0; Index < MaxLines; ++Index)
	{
		UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>();
		Text->SetAutoWrapText(true);
		Text->SetVisibility(ESlateVisibility::Collapsed);
		Box->AddChildToVerticalBox(Text);
		Texts.Add(Text);
	}
	// Телефон — только картинка: мышь и клавиатуру он не трогает.
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UObshagaPhoneWidget::SetLines(const TArray<FPhoneLine>& Lines)
{
	for (int32 Index = 0; Index < Texts.Num(); ++Index)
	{
		UTextBlock* Text = Texts[Index];
		if (!Lines.IsValidIndex(Index))
		{
			Text->SetVisibility(ESlateVisibility::Collapsed);
			continue;
		}
		const FPhoneLine& Line = Lines[Index];
		Text->SetText(FText::FromString(Line.Text));
		Text->SetColorAndOpacity(FSlateColor(Line.Color));
		SetFontSize(Text, Line.bHeader ? 26 : 21);
		if (UVerticalBoxSlot* TextSlot = Cast<UVerticalBoxSlot>(Text->Slot))
		{
			TextSlot->SetPadding(FMargin(0.f, Line.bHeader && Index > 0 ? 12.f : 2.f, 0.f, 0.f));
		}
		Text->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

#undef LOCTEXT_NAMESPACE
