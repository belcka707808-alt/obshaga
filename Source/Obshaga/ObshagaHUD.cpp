#include "ObshagaHUD.h"

#include "CarryComponent.h"
#include "InteractionComponent.h"
#include "ItemActor.h"
#include "ObshagaCharacter.h"
#include "ObshagaPlayerController.h"
#include "RoomVolume.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"

#define LOCTEXT_NAMESPACE "ObshagaHUD"

namespace
{
	constexpr float NoticeSeconds = 2.5f;
	constexpr float NoiseSeconds = 1.5f;
}

void AObshagaHUD::DrawHUD()
{
	Super::DrawHUD();

	const AObshagaCharacter* Character = Cast<AObshagaCharacter>(GetOwningPawn());
	if (!Character || !Canvas)
	{
		return;
	}

	UFont* Font = GEngine->GetMediumFont();
	const float Scale = FMath::Max(1.f, Canvas->ClipY / 540.f);
	const float Now = GetWorld()->GetTimeSeconds();

	auto DrawCentered = [this, Font, Scale](const FString& Line, const FLinearColor& Color, float YFraction)
	{
		float TextWidth = 0.f;
		float TextHeight = 0.f;
		GetTextSize(Line, TextWidth, TextHeight, Font, Scale);
		DrawText(Line, Color, (Canvas->ClipX - TextWidth) * 0.5f, Canvas->ClipY * YFraction, Font, Scale);
	};

	if (const ARoomVolume* Room = Character->GetCurrentRoom())
	{
		DrawText(Room->DisplayName.ToString(), FLinearColor::White, 16.f * Scale, 12.f * Scale, Font, Scale);
	}

	if (Character->IsHiding())
	{
		DrawCentered(LOCTEXT("Hiding", "Ты в укрытии").ToString(), FLinearColor(0.6f, 0.8f, 1.f), 0.2f);
	}

	const UInteractionComponent* Interaction = Character->GetInteractionComponent();
	const FText Prompt = Interaction->GetFocusedPrompt();
	if (!Prompt.IsEmpty())
	{
		DrawCentered(FText::Format(LOCTEXT("PromptFormat", "[E] {0}"), Prompt).ToString(), FLinearColor::Yellow, 0.72f);
	}
	const FText SecondaryPrompt = Interaction->GetFocusedSecondaryPrompt();
	if (!SecondaryPrompt.IsEmpty())
	{
		DrawCentered(FText::Format(LOCTEXT("SecondaryFormat", "[F] {0}"), SecondaryPrompt).ToString(), FLinearColor::Yellow, 0.77f);
	}

	if (const AItemActor* Item = Character->GetCarryComponent()->GetCarriedItem())
	{
		const FText Line = FText::Format(LOCTEXT("Carrying", "В руках: {0}   [G] положить   [ЛКМ] бросить"), Item->GetDisplayName());
		DrawText(Line.ToString(), FLinearColor::White, 16.f * Scale, Canvas->ClipY - 36.f * Scale, Font, Scale);
	}

	const AObshagaPlayerController* Controller = Cast<AObshagaPlayerController>(GetOwningPlayerController());
	if (!Controller)
	{
		return;
	}

	if (Now - Controller->GetNoticeTime() < NoticeSeconds)
	{
		DrawCentered(Controller->GetNotice().ToString(), FLinearColor::White, 0.62f);
	}

	// Индикатор шума: самый громкий из недавних и расстояние до него.
	const FHeardNoise* Loudest = nullptr;
	for (const FHeardNoise& Noise : Controller->GetRecentNoises())
	{
		if (Now - Noise.Time < NoiseSeconds && (!Loudest || Noise.Loudness > Loudest->Loudness))
		{
			Loudest = &Noise;
		}
	}
	if (Loudest)
	{
		const int32 Meters = FMath::RoundToInt32(FVector::Dist(Character->GetActorLocation(), Loudest->Location) / 100.f);
		const FText Strength = Loudest->Loudness > 0.6f ? LOCTEXT("NoiseLoud", "ГРОХОТ") : LOCTEXT("NoiseQuiet", "Шум");
		const FText Line = FText::Format(LOCTEXT("NoiseFormat", "{0}: {1} м"), Strength, FText::AsNumber(Meters));
		DrawCentered(Line.ToString(), FLinearColor(1.f, 0.45f, 0.1f), 0.1f);
	}
}

#undef LOCTEXT_NAMESPACE
