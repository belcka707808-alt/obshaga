#include "ObshagaGameMode.h"

#include "ObshagaCharacter.h"
#include "ObshagaGameState.h"
#include "ObshagaHUD.h"
#include "ObshagaPlayerController.h"
#include "ObshagaPlayerState.h"

AObshagaGameMode::AObshagaGameMode()
{
	// Внешность персонажа назначается в BP_ObshagaGameMode (Default Pawn Class).
	DefaultPawnClass = AObshagaCharacter::StaticClass();
	PlayerControllerClass = AObshagaPlayerController::StaticClass();
	PlayerStateClass = AObshagaPlayerState::StaticClass();
	GameStateClass = AObshagaGameState::StaticClass();
	HUDClass = AObshagaHUD::StaticClass();
}
