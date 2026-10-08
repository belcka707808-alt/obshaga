#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DataAsset.h"
#include "ObshagaLookComponent.generated.h"

class UAnimSequenceBase;
class USkeletalMesh;

/**
 * Внешность и анимации персонажей. Ассет лежит по постоянному адресу Content/Obshaga/Characters/DA_Looks.
 * Если его нет или он пуст — персонажи остаются манекенами из пака Third Person.
 */
UCLASS(BlueprintType)
class OBSHAGA_API UObshagaLookConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	static const UObshagaLookConfig* Get();

	/** Модели жильцов; игроку достаётся модель по его номеру, так их можно различать. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look")
	TArray<TObjectPtr<USkeletalMesh>> ResidentMeshes;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look")
	TObjectPtr<USkeletalMesh> KomendantMesh;

	/** Рост жильца и коменданта: модель растягивается до него. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look", meta = (ClampMin = "10", Units = "cm"))
	float ResidentHeight = 135.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look", meta = (ClampMin = "10", Units = "cm"))
	float KomendantHeight = 170.f;

	/** Поворот модели, чтобы она смотрела туда же, куда идёт персонаж. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look", meta = (Units = "deg"))
	float ModelYaw = -90.f;

	// --- Анимации (общий скелет) ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UAnimSequenceBase> Idle;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UAnimSequenceBase> Walk;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UAnimSequenceBase> Sprint;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UAnimSequenceBase> Crouch;

	/** Несёт предмет. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UAnimSequenceBase> Carry;

	/** Взял предмет (один раз). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UAnimSequenceBase> PickUp;

	/** Бросил предмет (один раз). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UAnimSequenceBase> Throw;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UAnimSequenceBase> Fall;

	/** Пойман, стоит на допросе. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UAnimSequenceBase> Caught;

	/** Жесты к фразам колеса эмоций: фраза №N играет жест №N (по кругу, если жестов меньше). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TArray<TObjectPtr<UAnimSequenceBase>> EmoteGestures;

	/** Быстрее этой скорости персонаж считается идущим. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation", meta = (ClampMin = "0", Units = "cm/s"))
	float MoveSpeedThreshold = 20.f;

	/** Быстрее этой — бегущим. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation", meta = (ClampMin = "0", Units = "cm/s"))
	float RunSpeedThreshold = 420.f;
};

/**
 * «Костюмер и аниматор» персонажа. Висит на жильце и на коменданте, работает на каждой машине отдельно:
 * надевает модель и каждый кадр выбирает анимацию по состоянию, которое и так реплицируется
 * (скорость, присед, предмет в руках, допрос, фраза). Поэтому все игроки видят одно и то же.
 */
UCLASS(ClassGroup = (Obshaga))
class OBSHAGA_API UObshagaLookComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UObshagaLookComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Надеть модель: Index — номер модели жильца; для коменданта не важен. */
	void ApplyLook(int32 Index);

	/** Высота меша стоящего персонажа; false, если модель ещё не надета (тогда меш — манекен, и трогать его не надо). */
	bool GetStandingMeshZ(float& OutZ) const { OutZ = StandingMeshZ; return bLookApplied; }

	/** Название текущей анимации — для самопроверки. */
	FString GetCurrentAnimName() const;

protected:
	virtual void BeginPlay() override;

private:
	UAnimSequenceBase* PickAnimation(bool& bOutLoop);
	void PlayOnce(UAnimSequenceBase* Animation);

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequenceBase> Current;

	/** Разовая анимация (взял, бросил, жест) играет до этого времени. */
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequenceBase> OneShot;

	float OneShotUntil = 0.f;
	bool bOneShotStarted = false;
	float StandingMeshZ = 0.f;
	float SmoothedSpeed = 0.f;
	bool bLookApplied = false;
	bool bWasCarrying = false;
	int32 LastEmote = INDEX_NONE;
};
