#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "ObshagaCharacter.generated.h"

class AHidingSpot;
class ARoomVolume;
enum class EGameEventType : uint8;
class UCameraComponent;
class UCarryComponent;
class UInteractionComponent;
class UObshagaCharacterConfig;
class USpringArmComponent;

/** Жилец общаги: ходьба, бег, присед, камера от третьего лица, переноска предметов, прятки. */
UCLASS()
class OBSHAGA_API AObshagaCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AObshagaCharacter();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PostInitializeComponents() override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void NotifyActorBeginOverlap(AActor* OtherActor) override;
	virtual void NotifyActorEndOverlap(AActor* OtherActor) override;
	virtual void OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
	virtual void OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;

	/** Числа баланса; если ассет не назначен — значения по умолчанию из класса. */
	const UObshagaCharacterConfig* GetConfig() const;

	UInteractionComponent* GetInteractionComponent() const { return InteractionComponent; }
	UCarryComponent* GetCarryComponent() const { return CarryComponent; }
	/** Точка перед персонажем, к которой цепляется предмет в руках. */
	USceneComponent* GetCarryPoint() const { return CarryPoint; }

	void SetSprinting(bool bNewSprinting);
	bool IsSprinting() const { return bIsSprinting; }

	/** Вызывает UCarryComponent, когда в руках что-то появилось или исчезло. */
	void OnCarriedItemChanged();

	/** Комната, в которой персонаж сейчас находится (считается на каждой машине по позиции). */
	ARoomVolume* GetCurrentRoom() const;

	AHidingSpot* GetHidingSpot() const { return HidingSpot; }
	bool IsHiding() const { return HidingSpot != nullptr; }

	/** Стоит на допросе и не может двигаться. */
	bool IsFrozen() const { return bIsFrozen; }
	/** Только сервер. */
	void SetFrozen(bool bNewFrozen);

	/** Выселен: невидимый призрак, который ходит и смотрит, но ничего не может трогать. */
	bool IsGhost() const { return bIsGhost; }
	/** Только сервер. */
	void SetGhost(bool bNewGhost);

	/** Клиент: сказать фразу из колеса эмоций. Решает сервер, видят все. */
	void TryEmote(int32 Index);
	/** Номер фразы, которая сейчас висит над головой; -1, если персонаж молчит. */
	int32 GetActiveEmote() const;
	/** Фраза над головой прямо сейчас: из колеса фраз или реплика бота. Пусто — молчит. */
	FText GetActiveSpeech() const;
	/** Сервер: бот говорит реплику (Situation, Variant — номера из ObshagaStory::EBotLine). */
	void SayBotLine(uint8 Situation, uint8 Variant);

	/** Сдвиг камеры для тряски (только у локального игрока). */
	void SetCameraShakeOffset(const FVector& Offset);

	// Только сервер. Вызывает AHidingSpot.
	void EnterHidingSpot(AHidingSpot* Spot);
	/** SideStep — куда сдвинуться (и в обратную сторону), если прямо в точке выхода кто-то стоит. */
	void ExitHidingSpot(const FVector& ExitLocation, const FVector& SideStep = FVector::ZeroVector);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	/** Сменился управляющий (сервер и своя машина): камере в укрытии надо знать, чей это персонаж. */
	virtual void NotifyControllerChanged() override;

	UFUNCTION(Server, Reliable)
	void ServerSetSprinting(bool bNewSprinting);

	UFUNCTION(Server, Reliable)
	void ServerEmote(uint8 Index);

	/** Бросил тяжёлое и упал: на это время персонаж не может идти, а у всех играет анимация падения. */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastStumble(float Seconds);

	/** Фраза — мимолётная вещь: кто не получил, тот ничего не потерял. */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastEmote(uint8 Index);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastBotLine(uint8 Situation, uint8 Variant);

	UFUNCTION()
	void OnRep_IsSprinting();

	UFUNCTION()
	void OnRep_LookIndex();

public:
	/** Номер модели жильца (тот же, что у сервера в PlayerState; до клиентов доходит только здесь). */
	int32 GetLookSlot() const { return LookIndex > 0 ? LookIndex - 1 : 0; }

	/** Только сервер: персонаж падает на столько секунд. */
	void Stumble(float Seconds);
	/** Лежит ли персонаж сейчас после броска тяжёлого (на каждой машине своё время, по часам этой машины). */
	bool IsStumbling() const;

private:
	float StumbleEndTime = -100.f;
	FTimerHandle StumbleTimer;

protected:

	UFUNCTION()
	void OnRep_HidingSpot();

	UFUNCTION()
	void OnRep_IsFrozen();

	UFUNCTION()
	void OnRep_IsGhost();

	void UpdateMovementSpeed();
	/** После приседа и вставания движок ставит меш на высоту манекена — возвращаем высоту надетой модели. */
	void FixLookMeshHeight(float CrouchAdjust);
	void ApplyHiding();
	/** Своя машина: в шкафу или кабинке камера смотрит наружу из-за дверцы, на выходе рука камеры прежняя. */
	void UpdateHidingCamera();
	/** Своя машина: запрет на бег кончился, а Shift всё ещё зажат — снова бежим. */
	void ResumeSprintIfHeld();
	/** Этим персонажем управляет игрок за этой машиной (не бот и не чужой игрок). */
	bool IsLocalHuman() const;
	void PublishRoomEvent(EGameEventType Type, const ARoomVolume* Room);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<UCameraComponent> FollowCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interaction")
	TObjectPtr<UInteractionComponent> InteractionComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Carry")
	TObjectPtr<UCarryComponent> CarryComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Carry")
	TObjectPtr<USceneComponent> CarryPoint;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config")
	TObjectPtr<UObshagaCharacterConfig> Config;

	/** Владельцу не шлём: он выставляет бег сам, сразу по нажатию. */
	UPROPERTY(ReplicatedUsing = OnRep_IsSprinting)
	bool bIsSprinting = false;

	/** Тайник, в котором персонаж сейчас прячется. */
	UPROPERTY(ReplicatedUsing = OnRep_HidingSpot)
	TObjectPtr<AHidingSpot> HidingSpot;

	UPROPERTY(ReplicatedUsing = OnRep_IsFrozen)
	bool bIsFrozen = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Look")
	TObjectPtr<class UObshagaLookComponent> LookComponent;

	/** Номер модели персонажа (0 — ещё не назначен). Назначает сервер, видят все: по модели жильцов различают. */
	UPROPERTY(ReplicatedUsing = OnRep_LookIndex)
	uint8 LookIndex = 0;

	UPROPERTY(ReplicatedUsing = OnRep_IsGhost)
	bool bIsGhost = false;

private:
	TArray<TWeakObjectPtr<ARoomVolume>> OverlappingRooms;

	/** Своя машина: игрок просил бег (держит Shift), даже если бежать сейчас нельзя. */
	bool bSprintHeld = false;

	// Камера в укрытии (только своя машина): смотрит ли сейчас из-за дверцы, из какого тайника
	// и какой была рука камеры до этого.
	bool bPeekCamera = false;
	TWeakObjectPtr<AHidingSpot> PeekSpot;
	FVector SavedBoomLocation = FVector::ZeroVector;
	float SavedBoomArmLength = 0.f;
	bool bSavedBoomCollisionTest = true;
	/** Вышел из укрытия, но своя машина ещё видит персонажа внутри мебели: ждём, пока придёт новое место. */
	FTimerHandle PeekRestoreTimer;
	float PeekRestoreDeadline = 0.f;

	// Фраза над головой: у каждой машины своя копия и свои часы.
	int32 EmoteIndex = INDEX_NONE;
	float EmoteStartTime = -100.f;
	FText BotSpeech;
	/** Сервер: когда персонаж говорил в последний раз. */
	float LastEmoteServerTime = -100.f;
};
