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
	if (!Sequence)
	{
		return;
	}
	// Тот же клип, сменилось только «по кругу или один раз» (жест «нет», а следом допрос с тем же клипом):
	// клип идёт дальше с того же места, без перехода.
	if (Sequence == Current)
	{
		bCurrentLoops = bLoop;
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

void UObshagaAnimInstance::SetOverlay(UAnimSequence* Sequence, const TArray<FName>& Bones, float BlendSeconds)
{
	OverlayBlendTime = BlendSeconds;
	if (!Sequence)
	{
		// Клип не пропадает сразу: он ещё BlendSeconds плавно уходит, а потом его забудет NativeUpdateAnimation.
		OverlayTarget = 0.f;
		return;
	}
	if (Sequence != Overlay)
	{
		Overlay = Sequence;
		OverlayTime = 0.f;
	}
	if (OverlayBones != Bones)
	{
		OverlayBones = Bones;
	}
	OverlayTarget = 1.f;
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

	if (Overlay)
	{
		OverlayTime = Advance(Overlay, OverlayTime, DeltaSeconds, true);
		OverlayWeight = FMath::FInterpConstantTo(OverlayWeight, OverlayTarget, DeltaSeconds, 1.f / FMath::Max(OverlayBlendTime, 0.01f));
		if (OverlayTarget <= 0.f && OverlayWeight <= 0.f)
		{
			Overlay = nullptr;
		}
	}
	else
	{
		OverlayWeight = 0.f;
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
		Overlay = Instance->Overlay;
		OverlayTime = Instance->OverlayTime;
		OverlayWeight = Instance->OverlayWeight;
		if (OverlayBones != Instance->OverlayBones)
		{
			OverlayBones = Instance->OverlayBones;
		}
	}
}

bool FObshagaAnimProxy::Evaluate(FPoseContext& Output)
{
	EvaluateBase(Output);
	if (Current && Overlay && OverlayWeight > 0.f && !OverlayBones.IsEmpty())
	{
		ApplyOverlay(Output);
	}
	return true;
}

void FObshagaAnimProxy::ApplyOverlay(FPoseContext& Output)
{
	FPoseContext OverlayPose(Output);
	FAnimationPoseData OverlayData(OverlayPose);
	Overlay->GetAnimationPose(OverlayData, FAnimExtractContext(static_cast<double>(OverlayTime), false));

	// Кости идут от родителя к детям, поэтому ребёнок кости из списка узнаёт об этом по родителю за один проход.
	const FBoneContainer& BoneContainer = Output.Pose.GetBoneContainer();
	const FReferenceSkeleton& RefSkeleton = BoneContainer.GetReferenceSkeleton();
	const float Weight = FMath::SmoothStep(0.f, 1.f, OverlayWeight);
	TArray<bool, TInlineAllocator<64>> Masked;
	Masked.SetNumZeroed(Output.Pose.GetNumBones());
	for (const FCompactPoseBoneIndex Index : Output.Pose.ForEachBoneIndex())
	{
		const FCompactPoseBoneIndex Parent = BoneContainer.GetParentBoneIndex(Index);
		const FName BoneName = RefSkeleton.GetBoneName(BoneContainer.MakeMeshPoseIndex(Index).GetInt());
		const bool bMasked = OverlayBones.Contains(BoneName) || (Parent.IsValid() && Masked[Parent.GetInt()]);
		Masked[Index.GetInt()] = bMasked;
		if (bMasked)
		{
			FTransform Blended;
			Blended.Blend(Output.Pose[Index], OverlayPose.Pose[Index], Weight);
			Output.Pose[Index] = Blended;
		}
	}
}

void FObshagaAnimProxy::EvaluateBase(FPoseContext& Output)
{
	if (!Current)
	{
		Output.ResetToRefPose();
		return;
	}

	if (!Previous || Alpha >= 1.f)
	{
		FAnimationPoseData Data(Output);
		Current->GetAnimationPose(Data, FAnimExtractContext(static_cast<double>(CurrentTime), false));
		return;
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
}
