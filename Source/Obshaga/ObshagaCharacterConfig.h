#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ObshagaCharacterConfig.generated.h"

/** Баланс-числа персонажа. Правятся в ассете DA_CharacterConfig, не в коде. */
UCLASS(BlueprintType)
class OBSHAGA_API UObshagaCharacterConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0", Units = "cm/s"))
	float WalkSpeed = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0", Units = "cm/s"))
	float SprintSpeed = 550.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0", Units = "cm/s"))
	float CrouchSpeed = 150.f;

	/** Насколько близко надо стоять к объекту, чтобы с ним взаимодействовать. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction", meta = (ClampMin = "0", Units = "cm"))
	float InteractDistance = 250.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "0", Units = "cm"))
	float CameraArmLength = 350.f;
};
