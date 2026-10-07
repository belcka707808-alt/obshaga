#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "ObshagaHUD.generated.h"

/** Временный HUD серого макета: подсказка взаимодействия и название комнаты. На M3 заменяется виджетами. */
UCLASS()
class OBSHAGA_API AObshagaHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
};
