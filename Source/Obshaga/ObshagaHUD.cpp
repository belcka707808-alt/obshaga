#include "ObshagaHUD.h"

#include "InteractionComponent.h"
#include "ObshagaCharacter.h"
#include "RoomVolume.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"

#define LOCTEXT_NAMESPACE "ObshagaHUD"

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

	if (const ARoomVolume* Room = Character->GetCurrentRoom())
	{
		DrawText(Room->DisplayName.ToString(), FLinearColor::White, 16.f * Scale, 12.f * Scale, Font, Scale);
	}

	const FText Prompt = Character->GetInteractionComponent()->GetFocusedPrompt();
	if (!Prompt.IsEmpty())
	{
		const FString Line = FText::Format(LOCTEXT("PromptFormat", "[E] {0}"), Prompt).ToString();
		float TextWidth = 0.f;
		float TextHeight = 0.f;
		GetTextSize(Line, TextWidth, TextHeight, Font, Scale);
		DrawText(Line, FLinearColor::Yellow, (Canvas->ClipX - TextWidth) * 0.5f, Canvas->ClipY * 0.72f, Font, Scale);
	}
}

#undef LOCTEXT_NAMESPACE
