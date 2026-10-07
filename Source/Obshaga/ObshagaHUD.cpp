#include "ObshagaHUD.h"

#include "CarryComponent.h"
#include "InteractionComponent.h"
#include "ItemActor.h"
#include "ObshagaCharacter.h"
#include "ObshagaGameState.h"
#include "ObshagaPlayerController.h"
#include "ObshagaPlayerState.h"
#include "RoomVolume.h"
#include "TaskComponent.h"
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
	const AObshagaGameState* GameState = GetWorld()->GetGameState<AObshagaGameState>();
	if (GameState && GameState->GetRoundState() == ERoundState::Finished)
	{
		DrawRoundResults(Font, Scale);
	}
	else if (Controller->IsPhoneOpen())
	{
		DrawPhone(Character, Font, Scale);
	}
	else
	{
		const FString Hint = LOCTEXT("PhoneHint", "[Tab] телефон").ToString();
		float HintWidth = 0.f;
		float HintHeight = 0.f;
		GetTextSize(Hint, HintWidth, HintHeight, Font, Scale);
		DrawText(Hint, FLinearColor(1.f, 1.f, 1.f, 0.6f), Canvas->ClipX - HintWidth - 16.f * Scale, 12.f * Scale, Font, Scale);
	}

	if (Loudest)
	{
		const int32 Meters = FMath::RoundToInt32(FVector::Dist(Character->GetActorLocation(), Loudest->Location) / 100.f);
		const FText Strength = Loudest->Loudness > 0.6f ? LOCTEXT("NoiseLoud", "ГРОХОТ") : LOCTEXT("NoiseQuiet", "Шум");
		const FText Line = FText::Format(LOCTEXT("NoiseFormat", "{0}: {1} м"), Strength, FText::AsNumber(Meters));
		DrawCentered(Line.ToString(), FLinearColor(1.f, 0.45f, 0.1f), 0.1f);
	}
}

float AObshagaHUD::DrawWrapped(const FString& Text, const FLinearColor& Color, float X, float Y, float MaxWidth, UFont* Font, float Scale)
{
	TArray<FString> Words;
	Text.ParseIntoArray(Words, TEXT(" "));

	const float LineHeight = 20.f * Scale;
	FString Line;
	for (const FString& Word : Words)
	{
		const FString Candidate = Line.IsEmpty() ? Word : Line + TEXT(" ") + Word;
		float Width = 0.f;
		float Height = 0.f;
		GetTextSize(Candidate, Width, Height, Font, Scale);
		if (Width > MaxWidth && !Line.IsEmpty())
		{
			DrawText(Line, Color, X, Y, Font, Scale);
			Y += LineHeight;
			Line = Word;
		}
		else
		{
			Line = Candidate;
		}
	}
	if (!Line.IsEmpty())
	{
		DrawText(Line, Color, X, Y, Font, Scale);
		Y += LineHeight;
	}
	return Y;
}

void AObshagaHUD::DrawPhone(const AObshagaCharacter* Character, UFont* Font, float Scale)
{
	const AObshagaPlayerState* PlayerState = Character->GetPlayerState<AObshagaPlayerState>();
	const AObshagaGameState* GameState = GetWorld()->GetGameState<AObshagaGameState>();
	if (!PlayerState || !GameState)
	{
		return;
	}

	const float PanelWidth = 290.f * Scale;
	const float PanelX = Canvas->ClipX - PanelWidth - 12.f * Scale;
	const float PanelY = 40.f * Scale;
	const float Pad = 12.f * Scale;
	const float TextWidth = PanelWidth - Pad * 2.f;
	DrawRect(FLinearColor(0.02f, 0.02f, 0.05f, 0.82f), PanelX, PanelY, PanelWidth, Canvas->ClipY - PanelY - 60.f * Scale);

	const FLinearColor Dim(0.7f, 0.7f, 0.75f);
	const float X = PanelX + Pad;
	float Y = PanelY + Pad;

	Y = DrawWrapped(LOCTEXT("PhoneTitle", "ТЕЛЕФОН").ToString(), FLinearColor::White, X, Y, TextWidth, Font, Scale);

	const ARoomVolume* HomeRoom = ARoomVolume::FindRoomById(this, PlayerState->GetHomeRoomId());
	const FText HomeName = HomeRoom ? HomeRoom->DisplayName : LOCTEXT("NoHome", "нет");
	Y = DrawWrapped(FText::Format(LOCTEXT("PhoneHome", "Твоя комната: {0}"), HomeName).ToString(), Dim, X, Y, TextWidth, Font, Scale);

	if (GameState->GetRoundState() == ERoundState::InProgress)
	{
		const int32 Seconds = FMath::CeilToInt32(GameState->GetRemainingSeconds());
		const FString Timer = FString::Printf(TEXT("%02d:%02d"), Seconds / 60, Seconds % 60);
		Y = DrawWrapped(FText::Format(LOCTEXT("PhoneTimer", "До конца раунда: {0}"), FText::FromString(Timer)).ToString(), Dim, X, Y, TextWidth, Font, Scale);
	}
	else
	{
		Y = DrawWrapped(LOCTEXT("PhoneWaiting", "Раунд вот-вот начнётся").ToString(), Dim, X, Y, TextWidth, Font, Scale);
	}

	Y = DrawWrapped(FText::Format(LOCTEXT("PhoneScore", "Очки: {0}"), FText::AsNumber(FMath::RoundToInt32(PlayerState->GetScore()))).ToString(), Dim, X, Y, TextWidth, Font, Scale);
	Y += 10.f * Scale;
	Y = DrawWrapped(LOCTEXT("PhoneTasks", "СЕКРЕТНЫЕ ЗАДАНИЯ").ToString(), FLinearColor::White, X, Y, TextWidth, Font, Scale);

	const TArray<FTaskState>& Tasks = PlayerState->GetTaskComponent()->GetTasks();
	if (Tasks.IsEmpty())
	{
		DrawWrapped(LOCTEXT("PhoneNoTasks", "Пока нет").ToString(), Dim, X, Y, TextWidth, Font, Scale);
		return;
	}

	for (const FTaskState& Task : Tasks)
	{
		Y = DrawWrapped(FText::Format(LOCTEXT("PhoneTaskTitle", "{0}  (+{1})"), Task.Title, FText::AsNumber(Task.Reward)).ToString(), FLinearColor::Yellow, X, Y, TextWidth, Font, Scale);
		Y = DrawWrapped(Task.Description.ToString(), Dim, X, Y, TextWidth, Font, Scale);

		FText Status = LOCTEXT("StatusInProgress", "Пока не выполнено");
		FLinearColor StatusColor = Dim;
		if (Task.Status == ETaskStatus::Completed)
		{
			Status = LOCTEXT("StatusCompleted", "ВЫПОЛНЕНО");
			StatusColor = FLinearColor::Green;
		}
		else if (Task.Status == ETaskStatus::Failed)
		{
			Status = LOCTEXT("StatusFailed", "ПРОВАЛЕНО");
			StatusColor = FLinearColor::Red;
		}
		else if (Task.bSatisfiedNow)
		{
			Status = LOCTEXT("StatusSatisfied", "Сейчас выполнено — продержись до конца");
			StatusColor = FLinearColor(0.5f, 1.f, 0.5f);
		}
		Y = DrawWrapped(Status.ToString(), StatusColor, X, Y, TextWidth, Font, Scale);
		Y += 8.f * Scale;
	}
}

void AObshagaHUD::DrawRoundResults(UFont* Font, float Scale)
{
	const AObshagaGameState* GameState = GetWorld()->GetGameState<AObshagaGameState>();

	const float PanelWidth = FMath::Min(Canvas->ClipX - 40.f * Scale, 560.f * Scale);
	const float PanelX = (Canvas->ClipX - PanelWidth) * 0.5f;
	const float PanelY = 50.f * Scale;
	const float Pad = 14.f * Scale;
	const float TextWidth = PanelWidth - Pad * 2.f;
	DrawRect(FLinearColor(0.02f, 0.02f, 0.05f, 0.88f), PanelX, PanelY, PanelWidth, Canvas->ClipY - PanelY * 2.f);

	const float X = PanelX + Pad;
	float Y = PanelY + Pad;
	Y = DrawWrapped(LOCTEXT("ResultsTitle", "ИТОГИ РАУНДА — кто чем занимался").ToString(), FLinearColor::White, X, Y, TextWidth, Font, Scale);
	Y += 6.f * Scale;

	for (const FRevealedTask& Line : GameState->GetRevealedTasks())
	{
		const bool bDone = Line.Status == ETaskStatus::Completed;
		const FText Outcome = bDone
			? FText::Format(LOCTEXT("ResultDone", "ВЫПОЛНЕНО +{0}"), FText::AsNumber(Line.Reward))
			: LOCTEXT("ResultFailed", "ПРОВАЛЕНО");
		const FText Text = FText::Format(LOCTEXT("ResultLine", "{0}: «{1}» — {2}"), FText::FromString(Line.PlayerName.Left(28)), Line.TaskTitle, Outcome);
		Y = DrawWrapped(Text.ToString(), bDone ? FLinearColor::Green : FLinearColor(1.f, 0.4f, 0.4f), X, Y, TextWidth, Font, Scale);
	}

	Y += 10.f * Scale;
	Y = DrawWrapped(LOCTEXT("ResultsScores", "ОЧКИ").ToString(), FLinearColor::White, X, Y, TextWidth, Font, Scale);
	for (const APlayerState* Player : GameState->PlayerArray)
	{
		if (Player)
		{
			const FText Text = FText::Format(LOCTEXT("ScoreLine", "{0}: {1}"), FText::FromString(Player->GetPlayerName().Left(28)), FText::AsNumber(FMath::RoundToInt32(Player->GetScore())));
			Y = DrawWrapped(Text.ToString(), FLinearColor(0.85f, 0.85f, 0.9f), X, Y, TextWidth, Font, Scale);
		}
	}
}

#undef LOCTEXT_NAMESPACE
