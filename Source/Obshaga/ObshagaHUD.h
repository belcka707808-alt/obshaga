#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "ObshagaHUD.generated.h"

class AObshagaCharacter;
class AObshagaGameState;
class AObshagaPlayerController;
class AObshagaPlayerState;

/** Временный HUD серого макета: всё рисуется текстом. На M6 заменяется виджетами. */
UCLASS()
class OBSHAGA_API AObshagaHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	void DrawTopStatus(const AObshagaGameState* GameState);
	void DrawCharacterInfo(const AObshagaCharacter* Character);
	void DrawNoise(const AObshagaPlayerController* Controller, const FVector& ListenerLocation);
	void DrawPhone(const AObshagaPlayerState* MyState, const AObshagaGameState* GameState);
	void DrawRoundResults(const AObshagaGameState* GameState);
	void DrawInterrogation(const AObshagaPlayerState* MyState, const AObshagaGameState* GameState, const FVector& MyLocation);
	void DrawKomendantLabels(const FVector& MyLocation);
	void DrawDanger(const AObshagaPlayerController* Controller);
	void DrawEmotes(const AObshagaCharacter* Me);
	void DrawEmoteWheel(const AObshagaCharacter* Me);
	void DrawTutorial(const AObshagaPlayerController* Controller);

	void DrawCentered(const FString& Line, const FLinearColor& Color, float YFraction);
	/** Рисует текст с переносом по словам; возвращает Y под последней строкой. bDraw = false — только измерить высоту. */
	float DrawWrapped(const FString& Text, const FLinearColor& Color, float X, float Y, float MaxWidth, float LineHeight = 20.f, bool bDraw = true);

	UPROPERTY(Transient)
	TObjectPtr<UFont> Font;

	float Scale = 1.f;
};
