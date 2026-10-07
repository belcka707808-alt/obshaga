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

	/** Во сколько раз медленнее идёт игрок с тяжёлым предметом. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.1", ClampMax = "1"))
	float HeavyCarrySpeedMultiplier = 0.55f;

	/** Насколько близко надо стоять к объекту, чтобы с ним взаимодействовать. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction", meta = (ClampMin = "0", Units = "cm"))
	float InteractDistance = 250.f;

	/** Насколько можно промахнуться взглядом мимо объекта, чтобы он всё равно выбрался. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction", meta = (ClampMin = "0", Units = "cm"))
	float AimAssistRadius = 45.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "0", Units = "cm"))
	float CameraArmLength = 350.f;

	/** На каком расстоянии перед собой игрок кладёт или выпускает предмет. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Carry", meta = (ClampMin = "0", Units = "cm"))
	float DropDistance = 70.f;

	/** С какой высоты (от центра персонажа) вылетает брошенный предмет. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Carry", meta = (Units = "cm"))
	float ThrowHeight = 40.f;

	/** Насколько бросок задран вверх относительно взгляда. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Carry", meta = (ClampMin = "0", ClampMax = "1"))
	float ThrowUpBias = 0.25f;

	/** Радиус проверки «нет ли стены там, куда кладём предмет». */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Carry", meta = (ClampMin = "1", Units = "cm"))
	float ReleaseProbeRadius = 20.f;

	/** С какого расстояния игрок слышит самый громкий шум (громкость 1). Тише — пропорционально ближе. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Noise", meta = (ClampMin = "0", Units = "cm"))
	float NoiseHearingRange = 2500.f;
};
