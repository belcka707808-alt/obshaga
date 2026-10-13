#include "ObshagaAnimInstance.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "AnimationRuntime.h"

namespace
{
	/** Двигает время клипа: зацикленный идёт по кругу, разовый останавливается на последнем кадре. */
	float Advance(const UAnimSequence* Sequence, float Time, float DeltaSeconds, bool bLoop)
	{
		const float Length = Sequence ? Sequence->GetPlayLength() : 0.f;
		if (Length <= 0.f)
		{
			return 0.f;
		}
		Time += DeltaSeconds;
		return bLoop ? FMath::Fmod(Time, Length) : FMath::Min(Time, Length);
	}
}

void UObshagaAnimInstance::Play(UAnimSequence* Sequence, bool bLoop, float BlendSeconds)
{
	if (!Sequence || Sequence == Current)
	{
		return;
	}

	// Вернулись к клипу, из которого ещё не до конца вышли (стойка — шаг — стойка): разворачиваем переход,
	// а не начинаем новый, иначе недосмешанная поза пропала бы и персонаж дёрнулся.
	if (Sequence == Previous && Alpha < 1.f)
	{
		Swap(Current, Previous);
		Swap(CurrentTime, PreviousTime);
		Swap(bCurrentLoops, bPreviousLoops);
		bCurrentLoops = bLoop;
		Alpha = 1.f - Alpha;
		BlendTime = BlendSeconds;
		return;
	}

	// То, что играло до сих пор, становится «прежним» и продолжает идти, пока поза перетекает в новый клип.
	Previous = Current;
	PreviousTime = CurrentTime;
	bPreviousLoops = bCurrentLoops;
	Current = Sequence;
	CurrentTime = 0.f;
	bCurrentLoops = bLoop;
	BlendTime = BlendSeconds;
	Alpha = (Previous && BlendSeconds > 0.f) ? 0.f : 1.f;
}

void UObshagaAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	CurrentTime = Advance(Current, CurrentTime, DeltaSeconds * PlayRate, bCurrentLoops);
	if (Alpha < 1.f)
	{
		PreviousTime = Advance(Previous, PreviousTime, DeltaSeconds, bPreviousLoops);
		Alpha = FMath::Min(Alpha + DeltaSeconds / FMath::Max(BlendTime, 0.01f), 1.f);
	}
	else
	{
		Previous = nullptr;
	}
}

FAnimInstanceProxy* UObshagaAnimInstance::CreateAnimInstanceProxy()
{
	return new FObshagaAnimProxy(this);
}

void FObshagaAnimProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);

	// Игровой поток передаёт потоку анимации, что и с какого места играть.
	if (const UObshagaAnimInstance* Instance = Cast<UObshagaAnimInstance>(InAnimInstance))
	{
		Current = Instance->Current;
		Previous = Instance->Previous;
		CurrentTime = Instance->CurrentTime;
		PreviousTime = Instance->PreviousTime;
		Alpha = Instance->Alpha;
	}
}

bool FObshagaAnimProxy::Evaluate(FPoseContext& Output)
{
	if (!Current)
	{
		Output.ResetToRefPose();
		return true;
	}

	if (!Previous || Alpha >= 1.f)
	{
		FAnimationPoseData Data(Output);
		Current->GetAnimationPose(Data, FAnimExtractContext(static_cast<double>(CurrentTime), false));
		return true;
	}

	// Идёт переход: считаем обе позы и смешиваем. Кривая S-образная, чтобы движение не дёргалось в начале и в конце.
	FPoseContext From(Output);
	FPoseContext To(Output);
	FAnimationPoseData FromData(From);
	FAnimationPoseData ToData(To);
	Previous->GetAnimationPose(FromData, FAnimExtractContext(static_cast<double>(PreviousTime), false));
	Current->GetAnimationPose(ToData, FAnimExtractContext(static_cast<double>(CurrentTime), false));

	FAnimationPoseData OutData(Output);
	FAnimationRuntime::BlendTwoPosesTogether(FromData, ToData, 1.f - FMath::SmoothStep(0.f, 1.f, Alpha), OutData);
	return true;
}
