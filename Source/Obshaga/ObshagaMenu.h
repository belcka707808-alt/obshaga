#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "ObshagaMenu.generated.h"

/** Правила «карты меню»: персонажа нет, только экран меню и его управление. */
UCLASS()
class OBSHAGA_API AObshagaMenuGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AObshagaMenuGameMode();
};

/** Управление главным меню с клавиатуры: выбор пункта и ввод кода комнаты. */
UCLASS()
class OBSHAGA_API AObshagaMenuController : public APlayerController
{
	GENERATED_BODY()

public:
	virtual bool InputKey(const FInputKeyEventArgs& Params) override;

	bool IsEnteringCode() const { return bEnteringCode; }
	const FString& GetTypedCode() const { return TypedCode; }

private:
	bool bEnteringCode = false;
	FString TypedCode;
};

/** Главное меню, нарисованное текстом (как и остальной интерфейс игры). */
UCLASS()
class OBSHAGA_API AObshagaMenuHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	void DrawLine(const FString& Line, const FLinearColor& Color, float YFraction, float SizeScale = 1.f);
};
