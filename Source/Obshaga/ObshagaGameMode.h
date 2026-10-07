#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ObshagaGameMode.generated.h"

/** «Судья» игры. Существует только на сервере. */
UCLASS()
class OBSHAGA_API AObshagaGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AObshagaGameMode();
};
