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

	/** Основной клип (и перетекание в него из прежнего). */
	void EvaluateBase(FPoseContext& Output);
	/** Кости из OverlayBones и всё, что к ним прикреплено, берут позу из верхнего клипа. */
	void ApplyOverlay(FPoseContext& Output);

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> Current;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> Previous;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> Overlay;

	TArray<FName> OverlayBones;

	float CurrentTime = 0.f;
	float PreviousTime = 0.f;
	/** Доля новой анимации в позе: 0 — ещё старая, 1 — уже только новая. */
	float Alpha = 1.f;
	float OverlayTime = 0.f;
	/** Доля верхнего клипа в позе его костей: 0 — его нет, 1 — кости целиком из него. */
	float OverlayWeight = 0.f;
};

/**
 * Аниматор персонажей без Animation Blueprint: играет один клип и плавно перетекает в следующий.
 * Поверх него может идти «верхний» клип для части костей: руки держат предмет, а ноги шагают по основному.
 * Какой клип играть, решает UObshagaLookComponent; здесь только время клипов и смешивание поз.
 */
UCLASS(Transient, NotBlueprintable)
class OBSHAGA_API UObshagaAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	/** Начать клип; прежний за BlendSeconds плавно в него перейдёт. */
	void Play(UAnimSequence* Sequence, bool bLoop, float BlendSeconds);
	/** Скорость текущего клипа: шаги подстраиваются под настоящую скорость персонажа, чтобы ноги не скользили. */
	void SetPlayRate(float NewRate) { PlayRate = NewRate; }
	/**
	 * Верхний клип для костей Bones (и всего, что к ним прикреплено) поверх основного; за BlendSeconds плавно появляется.
	 * nullptr — плавно убрать.
	 */
	void SetOverlay(UAnimSequence* Sequence, const TArray<FName>& Bones, float BlendSeconds);

protected:
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;

private:
	friend struct FObshagaAnimProxy;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> Current;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> Previous;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> Overlay;

	TArray<FName> OverlayBones;

	float CurrentTime = 0.f;
	float PreviousTime = 0.f;
	float Alpha = 1.f;
	float BlendTime = 0.2f;
	float PlayRate = 1.f;
	bool bCurrentLoops = true;
	bool bPreviousLoops = true;
	float OverlayTime = 0.f;
	float OverlayWeight = 0.f;
	/** 1 — верхний клип должен быть виден, 0 — должен уйти. */
	float OverlayTarget = 0.f;
	float OverlayBlendTime = 0.2f;
};
