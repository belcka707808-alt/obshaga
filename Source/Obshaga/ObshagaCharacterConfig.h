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
	UObshagaCharacterConfig();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0", Units = "cm/s"))
	float WalkSpeed = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0", Units = "cm/s"))
	float SprintSpeed = 550.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0", Units = "cm/s"))
	float CrouchSpeed = 150.f;

	/** Во сколько раз медленнее идёт игрок с тяжёлым предметом. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.1", ClampMax = "1"))
	float HeavyCarrySpeedMultiplier = 0.55f;

	/**
	 * Скорость вверх в начале прыжка. Высота прыжка = скорость² / (2 × 980 × GravityScale): 420 — 90 см, 470 — 113 см.
	 * Выше ~120 см не поднимать: капсула (180 см, на 45 см выше модели) билась бы о потолок первого этажа (~300 см).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0", Units = "cm/s"))
	float JumpZVelocity = 470.f;

	/** Насколько можно рулить в прыжке: 0 — летит, куда оттолкнулся, 1 — как по земле. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0", ClampMax = "1"))
	float AirControl = 0.35f;

	/** Сила тяжести жильца: меньше 1 — прыжок дольше и «воздушнее», падение с балкона медленнее. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.1"))
	float GravityScale = 1.f;

	/** Насколько близко надо стоять к объекту, чтобы с ним взаимодействовать. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction", meta = (ClampMin = "0", Units = "cm"))
	float InteractDistance = 250.f;

	/** Насколько можно промахнуться взглядом мимо объекта, чтобы он всё равно выбрался. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction", meta = (ClampMin = "0", Units = "cm"))
	float AimAssistRadius = 45.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "0", Units = "cm"))
	float CameraArmLength = 350.f;

	/** Стена за спиной сжала руку камеры короче этого — камера начинает подниматься над головой. 0 — не поднимается. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "0", Units = "cm"))
	float CloseCameraArm = 200.f;

	/** Насколько выше обычного встаёт камера, когда рука сжата до нуля (вплотную к стене — почти на столько). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "0", Units = "cm"))
	float CloseCameraLift = 40.f;

	/** Камера внутри своей модели или ближе этого к её габаритам (низкий потолок не дал подняться) — свою фигурку не рисуем. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "0", Units = "cm"))
	float CameraHideSelfMargin = 10.f;

	/** Камера ближе этого к тому, что игрок несёт (телевизор на голове, спиной к стене), — эту вещь у себя не рисуем. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "0", Units = "cm"))
	float CameraHideCarriedDistance = 80.f;

	/** С телевизором на голове камера смотрит поверх него: на столько выше его верха проходит середина экрана. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "0", Units = "cm"))
	float CarryCameraMargin = 15.f;

	/**
	 * Постоянная экспозиция камеры: к какой яркости сцены она «привыкла». 0 — автоматическая.
	 * С автоматической ночь не выглядит ночью: камера сама высветляет потемневшую сцену.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "0"))
	float FixedExposureBrightness = 1.f;

	/** Где предмет в руках: точка перед грудью, от центра персонажа (вперёд, вбок, вверх). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Carry", meta = (Units = "cm"))
	FVector HandsCarryOffset = FVector(65.f, 0.f, 10.f);

	/**
	 * Тяжёлое (телевизор) несут на голове: перед грудью его с камеры сзади целиком закрывает большая голова жильца.
	 * Куда ставится низ предмета относительно макушки модели (рост модели — ResidentHeight в DA_Looks): вперёд, вбок, вверх.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Carry", meta = (Units = "cm"))
	FVector HeadCarryOffset = FVector(0.f, 0.f, 2.f);

	/**
	 * В приседе капсула на 30 см ниже, а модель на ходу идёт в полный рост и только на месте приседает (примерно на 15 см).
	 * На столько в приседе поднимаются точки переноски: предмет не висит под руками, телевизор не тонет в голове.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Carry", meta = (ClampMin = "0", Units = "cm"))
	float CrouchCarryLift = 25.f;

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

	// --- Эмоции ---

	/** Быстрые фразы колеса эмоций [Q]; выбираются цифрами по порядку. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Emotes")
	TArray<FText> Emotes;

	/** Сколько секунд фраза висит над головой. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Emotes", meta = (ClampMin = "0.5", Units = "s"))
	float EmoteSeconds = 3.f;

	/** Как часто можно говорить (защита от спама). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Emotes", meta = (ClampMin = "0", Units = "s"))
	float EmoteCooldown = 1.f;

	/** С какого расстояния фразу видно над головой. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Emotes", meta = (ClampMin = "0", Units = "cm"))
	float EmoteVisibleDistance = 2500.f;

	// --- Чувство опасности ---

	/** С какого расстояния до коменданта начинает темнеть экран и стучать сердце. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Danger", meta = (ClampMin = "1", Units = "cm"))
	float DangerRadius = 1400.f;

	/** Пульс в спокойствии и на пике опасности, ударов в секунду. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Danger", meta = (ClampMin = "0.1"))
	float HeartRateCalm = 1.1f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Danger", meta = (ClampMin = "0.1"))
	float HeartRatePanic = 2.8f;

	/** Тряска камеры в момент поимки: длительность и размах. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Danger", meta = (ClampMin = "0", Units = "s"))
	float CaughtShakeSeconds = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Danger", meta = (ClampMin = "0", Units = "cm"))
	float CaughtShakeAmplitude = 14.f;

	/** Бросил тяжёлое — на столько секунд персонаж падает и не может идти. 0 — не падает. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Carry", meta = (ClampMin = "0", Units = "s"))
	float HeavyThrowStumbleSeconds = 1.f;

	/** Допрос: камера пойманного встаёт сбоку и показывает его и коменданта. Расстояние от них и высота. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Danger", meta = (ClampMin = "50", Units = "cm"))
	float InterrogationCameraDistance = 260.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Danger", meta = (Units = "cm"))
	float InterrogationCameraHeight = 40.f;

	/** Допрос: лампа над пойманным — яркость (0 — без лампы), дальность и цвет. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Danger", meta = (ClampMin = "0"))
	float InterrogationLampIntensity = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Danger", meta = (ClampMin = "0", Units = "cm"))
	float InterrogationLampRadius = 420.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Danger")
	FLinearColor InterrogationLampColor = FLinearColor(1.f, 0.78f, 0.45f);
};
