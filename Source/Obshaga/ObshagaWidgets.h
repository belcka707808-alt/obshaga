#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InputCoreTypes.h"
#include "ObshagaWidgets.generated.h"

class UButton;
class UTextBlock;
class UVerticalBox;

/** Одна строка экрана телефона: текст, цвет и заголовок ли это. */
struct FPhoneLine
{
	FString Text;
	FLinearColor Color = FLinearColor::White;
	bool bHeader = false;
};

/**
 * Главное меню виджетом: заголовок, кнопки, сообщения. Кнопки нажимаются мышью и делают то же, что клавиши:
 * каждая кнопка просто «нажимает» свою клавишу у AObshagaMenuController, поэтому вся логика меню одна.
 * Собран в коде, без ассета-виджета.
 */
UCLASS()
class OBSHAGA_API UObshagaMenuWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UFUNCTION()
	void OnButton0();
	UFUNCTION()
	void OnButton1();
	UFUNCTION()
	void OnButton2();
	UFUNCTION()
	void OnButton3();
	UFUNCTION()
	void OnButton4();
	UFUNCTION()
	void OnButton5();

private:
	void Build();
	void Click(int32 Index);
	UTextBlock* AddText(UVerticalBox* Box, int32 FontSize, const FLinearColor& Color, float TopPadding);

	static constexpr int32 NumButtons = 6;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> Status;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> Hint;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> Message;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> Network;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UButton>> Buttons;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> ButtonTexts;

	/** Какую клавишу «нажимает» каждая кнопка на текущем экране меню. */
	TArray<FKey> ButtonKeys;
};

/** Телефон виджетом: панель справа со строками, которые готовит HUD (задания, подозрение, СМС). */
UCLASS()
class OBSHAGA_API UObshagaPhoneWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetLines(const TArray<FPhoneLine>& Lines);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	void Build();

	static constexpr int32 MaxLines = 28;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> Texts;
};
