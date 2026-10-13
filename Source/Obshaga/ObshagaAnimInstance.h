#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "ObshagaAnimInstance.generated.h"

class UAnimSequence;

/** Рабочая часть аниматора: считает позу в потоке анимации. Данные получает от UObshagaAnimInstance перед каждым кадром. */
USTRUCT()
struct FObshagaAnimProxy : public FAnimInstanceProxy
{
	GENERATED_BODY()

	FObshagaAnimProxy() = default;
	explicit FObshagaAnimProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}

	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> Current;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> Previous;

	float CurrentTime = 0.f;
	float PreviousTime = 0.f;
	/** Доля новой анимации в позе: 0 — ещё старая, 1 — уже только новая. */
	float Alpha = 1.f;
};

/**
 * Аниматор персонажей без Animation Blueprint: играет один клип и плавно перетекает в следующий.
 * Какой клип играть, решает UObshagaLookComponent; здесь только время клипов и смешивание двух поз.
 */
UCLASS(Transient, NotBlueprintable)
class OBSHAGA_API UObshagaAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	/** Начать клип; прежний за BlendSeconds плавно в него перейдёт. */
	void Play(UAnimSequence* Sequence, bool bLoop, float BlendSeconds);

protected:
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;

private:
	friend struct FObshagaAnimProxy;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> Current;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> Previous;

	float CurrentTime = 0.f;
	float PreviousTime = 0.f;
	float Alpha = 1.f;
	float BlendTime = 0.2f;
	bool bCurrentLoops = true;
	bool bPreviousLoops = true;
};
