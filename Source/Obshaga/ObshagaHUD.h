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
	virtual void BeginPlay() override;
	virtual void DrawHUD() override;

private:
	void DrawTopStatus(const AObshagaGameState* GameState);
	void DrawCharacterInfo(const AObshagaCharacter* Character);
	void DrawNoise(const AObshagaPlayerController* Controller, const FVector& ListenerLocation);
	void DrawPhone(const AObshagaPlayerState* MyState, const AObshagaGameState* GameState);
	/** Строки телефона: либо рисует их текстом, либо (если задан PhoneCollect) складывает в список для виджета. */
	void DrawPhoneLines(const AObshagaPlayerState* MyState, const AObshagaGameState* GameState);

	UPROPERTY(Transient)
	TObjectPtr<class UObshagaPhoneWidget> PhoneWidget;

	TArray<struct FPhoneLine>* PhoneCollect = nullptr;
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

	/** Правый край строк по центру сверху (фаза, событие ночи) в этом кадре — чтобы подсказки справа на них не налезали. */
	float TopCenterRight = 0.f;
	/** Рисует строку по центру сверху и запоминает её правый край. */
	void DrawTopLine(const FString& Line, const FLinearColor& Color, float YFraction);

	/** Выбранная раскладка экрана итогов и то, для чего она выбрана (чтобы не мерить заново каждый кадр). */
	int32 ResultsVariant = INDEX_NONE;
	uint32 ResultsKey = 0;

	/**
	 * «Фото» героя газеты. Под картой на секунду собирается фотобудка: копия модели героя, фон с ростовой
	 * линейкой, лампа и камера, которая один раз снимает в текстуру. Всё на своей машине, по сети не ходит.
	 */
	void UpdateHeroPhoto(const AObshagaGameState* GameState);
	void ClearHeroPhoto();

	UPROPERTY(Transient)
	TObjectPtr<class UTextureRenderTarget2D> HeroPhoto;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> PhotoBooth;

	UPROPERTY(Transient)
	TObjectPtr<class USceneCaptureComponent2D> PhotoCapture;

	FString HeroPhotoName;
	float PhotoShootTime = 0.f;
	bool bHeroPhotoStarted = false;
	bool bHeroPhotoReady = false;

	/** Когда на этой машине начался текущий раунд — для карточки с завязкой вечера. */
	float RoundStartLocalTime = -100.f;
	bool bWasInProgress = false;
};
