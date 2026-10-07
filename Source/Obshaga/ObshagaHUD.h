#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "ObshagaHUD.generated.h"

/** Временный HUD серого макета: подсказки, шум, телефон (Tab) и итоги раунда. На M6 заменяется виджетами. */
UCLASS()
class OBSHAGA_API AObshagaHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	void DrawPhone(const class AObshagaCharacter* Character, UFont* Font, float Scale);
	void DrawRoundResults(UFont* Font, float Scale);
	void DrawInterrogation(const class AObshagaCharacter* Character, UFont* Font, float Scale);
	void DrawKomendantLabels(const class AObshagaCharacter* Character, UFont* Font, float Scale);
	/** Рисует текст с переносом по словам; возвращает Y под последней строкой. */
	float DrawWrapped(const FString& Text, const FLinearColor& Color, float X, float Y, float MaxWidth, UFont* Font, float Scale);
};
