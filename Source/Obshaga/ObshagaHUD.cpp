#include "ObshagaHUD.h"

#include "CarryComponent.h"
#include "DeviceActor.h"
#include "InteractionComponent.h"
#include "ItemActor.h"
#include "HidingSpot.h"
#include "KomendantCharacter.h"
#include "NightExitDoor.h"
#include "ObshagaCharacter.h"
#include "ObshagaCharacterConfig.h"
#include "ObshagaGameState.h"
#include "ObshagaPlayerController.h"
#include "ObshagaPlayerState.h"
#include "RoomVolume.h"
#include "SuspicionComponent.h"
#include "TaskComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"

#define LOCTEXT_NAMESPACE "ObshagaHUD"

namespace
{
	constexpr float NoiseSeconds = 1.5f;

	FText RoleName(EPlayerRole Role)
	{
		switch (Role)
		{
		case EPlayerRole::Rat:
			return LOCTEXT("RoleRat", "Крыса");
		case EPlayerRole::Paranoid:
			return LOCTEXT("RoleParanoid", "Параноик");
		default:
			return LOCTEXT("RoleResident", "Жилец");
		}
	}

	FString FormatTime(float Seconds)
	{
		const int32 Whole = FMath::CeilToInt32(Seconds);
		return FString::Printf(TEXT("%02d:%02d"), Whole / 60, Whole % 60);
	}
}

void AObshagaHUD::DrawHUD()
{
	Super::DrawHUD();

	const AObshagaPlayerController* Controller = Cast<AObshagaPlayerController>(GetOwningPlayerController());
	const AObshagaPlayerState* MyState = Controller ? Controller->GetPlayerState<AObshagaPlayerState>() : nullptr;
	const AObshagaGameState* GameState = GetWorld()->GetGameState<AObshagaGameState>();
	if (!Canvas || !MyState || !GameState)
	{
		return;
	}

	Font = GEngine->GetMediumFont();
	Scale = FMath::Max(1.f, Canvas->ClipY / 540.f);

	const bool bInProgress = GameState->GetRoundState() == ERoundState::InProgress;
	if (GameState->GetDarkness() > 0.f)
	{
		// Ночью гаснет свет в самой сцене (AObshagaGameState); экран лишь слегка подсинён.
		DrawRect(FLinearColor(0.f, 0.f, 0.08f, 0.1f * GameState->GetDarkness()), 0.f, 0.f, Canvas->ClipX, Canvas->ClipY);
	}

	DrawDanger(Controller);

	const AObshagaCharacter* Character = Cast<AObshagaCharacter>(GetOwningPawn());
	const FVector MyLocation = Character ? Character->GetActorLocation() : FVector::ZeroVector;
	if (Character)
	{
		DrawCharacterInfo(Character);
		DrawKomendantLabels(MyLocation);
		DrawEmotes(Character);
		DrawNoise(Controller, MyLocation);
		if (Controller->IsEmoteWheelOpen())
		{
			DrawEmoteWheel(Character);
		}
	}
	if (GameState->GetRoundState() != ERoundState::Finished)
	{
		DrawTutorial(Controller);
	}

	DrawTopStatus(GameState);

	if (GameState->GetInterrogation().bActive)
	{
		DrawInterrogation(MyState, GameState, MyLocation);
	}

	if (GameState->GetRoundState() == ERoundState::Finished)
	{
		DrawRoundResults(GameState);
	}
	else if (Controller->IsPhoneOpen())
	{
		DrawPhone(MyState, GameState);
	}
	else
	{
		const FString Hint = LOCTEXT("PhoneHint", "[Q] сказать  [Tab] телефон").ToString();
		float HintWidth = 0.f;
		float HintHeight = 0.f;
		GetTextSize(Hint, HintWidth, HintHeight, Font, Scale);
		DrawText(Hint, FLinearColor(1.f, 1.f, 1.f, 0.6f), Canvas->ClipX - HintWidth - 16.f * Scale, 12.f * Scale, Font, Scale);
	}

	// Сообщения рисуем последними, чтобы телефон их не закрывал.
	if (GetWorld()->GetTimeSeconds() - Controller->GetNoticeTime() < AObshagaPlayerController::NoticeSeconds && GameState->GetRoundState() != ERoundState::Finished)
	{
		const FString Notice = Controller->GetNotice().ToString();
		float Width = 0.f;
		float Height = 0.f;
		GetTextSize(Notice, Width, Height, Font, Scale);
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.6f), (Canvas->ClipX - Width) * 0.5f - 8.f * Scale, Canvas->ClipY * 0.62f - 4.f * Scale, Width + 16.f * Scale, Height + 8.f * Scale);
		DrawCentered(Notice, FLinearColor::White, 0.62f);
	}
}

void AObshagaHUD::DrawDanger(const AObshagaPlayerController* Controller)
{
	// Комендант рядом: края экрана темнеют и вздрагивают в такт пульсу. Чем он ближе, тем сильнее и чаще.
	const float Level = Controller->GetDangerLevel();
	if (Level <= 0.02f)
	{
		return;
	}

	const float Pulse = Controller->GetHeartPulse();
	const float Strength = Level * (0.55f + 0.45f * Pulse);
	const float MaxThickness = FMath::Min(Canvas->ClipX, Canvas->ClipY) * (0.16f + 0.10f * Level);
	constexpr int32 NumBands = 28;
	for (int32 Band = 0; Band < NumBands; ++Band)
	{
		// Вложенные рамки: у самого края темнее всего, к центру сходит на нет.
		const float Inset = MaxThickness * Band / NumBands;
		const float Thickness = MaxThickness / NumBands + 1.f;
		const float Falloff = 1.f - static_cast<float>(Band) / NumBands;
		const FLinearColor Color(0.18f, 0.f, 0.f, 0.5f * Strength * Falloff * Falloff);
		DrawRect(Color, Inset, Inset, Canvas->ClipX - Inset * 2.f, Thickness);
		DrawRect(Color, Inset, Canvas->ClipY - Inset - Thickness, Canvas->ClipX - Inset * 2.f, Thickness);
		DrawRect(Color, Inset, Inset + Thickness, Thickness, Canvas->ClipY - (Inset + Thickness) * 2.f);
		DrawRect(Color, Canvas->ClipX - Inset - Thickness, Inset + Thickness, Thickness, Canvas->ClipY - (Inset + Thickness) * 2.f);
	}
}

void AObshagaHUD::DrawEmotes(const AObshagaCharacter* Me)
{
	// Фразы над головами — и над своей тоже. Сквозь стены не видны.
	const APlayerController* Controller = GetOwningPlayerController();
	const UObshagaCharacterConfig* Config = Me->GetConfig();
	for (TActorIterator<AObshagaCharacter> It(GetWorld()); It; ++It)
	{
		const AObshagaCharacter* Speaker = *It;
		const int32 Emote = Speaker->GetActiveEmote();
		if (Emote == INDEX_NONE || Speaker->IsHiding() || Speaker->IsGhost()
			|| FVector::Dist(Speaker->GetActorLocation(), Me->GetActorLocation()) > Config->EmoteVisibleDistance
			|| (Speaker != Me && !Controller->LineOfSightTo(Speaker)))
		{
			continue;
		}

		const FVector Screen = Canvas->Project(Speaker->GetActorLocation() + FVector(0.f, 0.f, 125.f));
		if (Screen.Z <= 0.f)
		{
			continue;
		}

		const FString Text = Config->Emotes[Emote].ToString();
		float Width = 0.f;
		float Height = 0.f;
		GetTextSize(Text, Width, Height, Font, Scale);
		const float Pad = 5.f * Scale;
		DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.9f), Screen.X - Width * 0.5f - Pad, Screen.Y - Height - Pad * 2.f, Width + Pad * 2.f, Height + Pad * 2.f);
		DrawText(Text, FLinearColor(0.05f, 0.05f, 0.1f), Screen.X - Width * 0.5f, Screen.Y - Height - Pad, Font, Scale);
	}
}

void AObshagaHUD::DrawEmoteWheel(const AObshagaCharacter* Me)
{
	const TArray<FText>& Emotes = Me->GetConfig()->Emotes;
	const float Line = 20.f;
	const float PanelWidth = 230.f * Scale;
	const float PanelHeight = (Emotes.Num() + 1) * Line * Scale + 16.f * Scale;
	const float PanelX = 16.f * Scale;
	const float PanelY = Canvas->ClipY * 0.5f - PanelHeight * 0.5f;
	DrawRect(FLinearColor(0.02f, 0.02f, 0.05f, 0.85f), PanelX, PanelY, PanelWidth, PanelHeight);

	const float X = PanelX + 10.f * Scale;
	float Y = PanelY + 8.f * Scale;
	DrawText(LOCTEXT("EmoteTitle", "СКАЗАТЬ  [Q] закрыть").ToString(), FLinearColor::White, X, Y, Font, Scale);
	for (int32 Index = 0; Index < Emotes.Num(); ++Index)
	{
		Y += Line * Scale;
		const FText Entry = FText::Format(LOCTEXT("EmoteEntry", "[{0}] {1}"), FText::AsNumber(Index + 1), Emotes[Index]);
		DrawText(Entry.ToString(), FLinearColor::Yellow, X, Y, Font, Scale);
	}
}

void AObshagaHUD::DrawTutorial(const AObshagaPlayerController* Controller)
{
	const FText Text = Controller->GetTutorialText();
	if (Text.IsEmpty())
	{
		return;
	}

	const float PanelWidth = 250.f * Scale;
	const float PanelX = 12.f * Scale;
	const float PanelY = 34.f * Scale;
	const float Pad = 8.f * Scale;
	DrawRect(FLinearColor(0.03f, 0.10f, 0.05f, 0.85f), PanelX, PanelY, PanelWidth, 104.f * Scale);

	float Y = PanelY + Pad;
	const FText Title = FText::Format(LOCTEXT("TutorialTitle", "ОБУЧЕНИЕ {0} из {1}"),
		FText::AsNumber(Controller->GetTutorialStep() + 1), FText::AsNumber(AObshagaPlayerController::NumTutorialSteps));
	Y = DrawWrapped(Title.ToString(), FLinearColor(0.6f, 1.f, 0.6f), PanelX + Pad, Y, PanelWidth - Pad * 2.f, 18.f);
	Y = DrawWrapped(Text.ToString(), FLinearColor::White, PanelX + Pad, Y, PanelWidth - Pad * 2.f, 17.f);
	DrawText(LOCTEXT("TutorialSkip", "[H] пропустить").ToString(), FLinearColor(0.7f, 0.7f, 0.75f), PanelX + Pad, PanelY + 86.f * Scale, Font, Scale);
}

void AObshagaHUD::DrawCentered(const FString& Line, const FLinearColor& Color, float YFraction)
{
	float TextWidth = 0.f;
	float TextHeight = 0.f;
	GetTextSize(Line, TextWidth, TextHeight, Font, Scale);
	DrawText(Line, Color, (Canvas->ClipX - TextWidth) * 0.5f, Canvas->ClipY * YFraction, Font, Scale);
}

float AObshagaHUD::DrawWrapped(const FString& Text, const FLinearColor& Color, float X, float Y, float MaxWidth, float LineHeight)
{
	TArray<FString> Words;
	Text.ParseIntoArray(Words, TEXT(" "));

	const float Step = LineHeight * Scale;
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
			Y += Step;
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
		Y += Step;
	}
	return Y;
}

void AObshagaHUD::DrawTopStatus(const AObshagaGameState* GameState)
{
	// Сверху по центру: в лобби — как начать, в раунде — фаза и таймер.
	const bool bHost = GetNetMode() != NM_Client;
	switch (GameState->GetRoundState())
	{
	case ERoundState::WaitingToStart:
		DrawCentered((bHost ? LOCTEXT("LobbyHost", "Все зашли? Нажми [Enter], чтобы начать раунд")
			: LOCTEXT("LobbyClient", "Ждём, пока хост начнёт раунд")).ToString(), FLinearColor::Yellow, 0.04f);
		// Лицензия музыки (CC BY) требует назвать автора в самой игре.
		DrawCentered(LOCTEXT("Credits", "Музыка: Kevin MacLeod (incompetech.com), CC BY 4.0. Звуки и мебель: Kenney (kenney.nl), CC0").ToString(),
			FLinearColor(1.f, 1.f, 1.f, 0.45f), 0.95f);
		break;

	case ERoundState::InProgress:
	{
		FText PhaseName = LOCTEXT("TopEvening", "ВЕЧЕР");
		FLinearColor Color(1.f, 0.85f, 0.5f);
		if (GameState->GetPhase() == ERoundPhase::Night)
		{
			PhaseName = LOCTEXT("TopNight", "НОЧЬ — ОТБОЙ");
			Color = FLinearColor(0.55f, 0.65f, 1.f);
		}
		else if (GameState->GetPhase() == ERoundPhase::Morning)
		{
			PhaseName = LOCTEXT("TopMorning", "УТРО — ПРОВЕРКА");
			Color = FLinearColor(1.f, 0.6f, 0.4f);
		}
		const FText Line = FText::Format(LOCTEXT("TopFormat", "{0}  {1}"), PhaseName, FText::FromString(FormatTime(GameState->GetPhaseRemainingSeconds())));
		DrawCentered(Line.ToString(), Color, 0.04f);
		break;
	}

	default:
		break;
	}
}

void AObshagaHUD::DrawCharacterInfo(const AObshagaCharacter* Character)
{
	if (const ARoomVolume* Room = Character->GetCurrentRoom())
	{
		DrawText(Room->DisplayName.ToString(), FLinearColor::White, 16.f * Scale, 12.f * Scale, Font, Scale);
	}

	if (Character->IsGhost())
	{
		DrawCentered(LOCTEXT("Ghost", "ТЫ ВЫСЕЛЕН. Ходи и смотри, чем всё кончится").ToString(), FLinearColor(0.7f, 0.7f, 1.f), 0.2f);
		return;
	}

	if (Character->IsHiding())
	{
		const bool bOutside = Character->GetHidingSpot()->IsA<ANightExitDoor>();
		DrawCentered((bOutside ? LOCTEXT("Outside", "Ты на улице") : LOCTEXT("Hiding", "Ты в укрытии")).ToString(), FLinearColor(0.6f, 0.8f, 1.f), 0.2f);
	}
	else
	{
		// Точка-прицел: по ней видно, на что нацелена кнопка взаимодействия.
		const float Dot = 3.f * Scale;
		DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.55f), (Canvas->ClipX - Dot) * 0.5f, (Canvas->ClipY - Dot) * 0.5f, Dot, Dot);
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

	// Клавиши, которые дают задания: показать на вора и настучать на комнату.
	const AObshagaPlayerController* Controller = Cast<AObshagaPlayerController>(GetOwningPlayerController());
	const AObshagaCharacter* Suspect = Controller ? Controller->FindAccuseTarget() : nullptr;
	if (Suspect && Suspect->GetPlayerState())
	{
		const FText Line = FText::Format(LOCTEXT("AccusePrompt", "[R] Сказать коменданту: вор — {0}"), FText::FromString(Suspect->GetPlayerState()->GetPlayerName()));
		DrawCentered(Line.ToString(), FLinearColor(1.f, 0.6f, 0.6f), 0.82f);
	}
	const ARoomVolume* TipRoom = Character->GetCurrentRoom();
	if (Controller && Controller->CanTipRoomNow() && TipRoom)
	{
		const FText Line = FText::Format(LOCTEXT("RoomTipPrompt", "[B] Настучать коменданту: {0}"), TipRoom->DisplayName);
		DrawCentered(Line.ToString(), FLinearColor(1.f, 0.6f, 0.6f), 0.87f);
	}

	if (const AItemActor* Item = Character->GetCarryComponent()->GetCarriedItem())
	{
		const FText Line = FText::Format(LOCTEXT("Carrying", "В руках: {0}   [G] положить   [ЛКМ] бросить"), Item->GetDisplayName());
		DrawText(Line.ToString(), FLinearColor::White, 16.f * Scale, Canvas->ClipY - 36.f * Scale, Font, Scale);
	}
}

void AObshagaHUD::DrawNoise(const AObshagaPlayerController* Controller, const FVector& ListenerLocation)
{
	// Индикатор шума: самый громкий из недавних и расстояние до него.
	const float Now = GetWorld()->GetTimeSeconds();
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
		const int32 Meters = FMath::RoundToInt32(FVector::Dist(ListenerLocation, Loudest->Location) / 100.f);
		const FText Strength = Loudest->Loudness > 0.6f ? LOCTEXT("NoiseLoud", "ГРОХОТ") : LOCTEXT("NoiseQuiet", "Шум");
		const FText Line = FText::Format(LOCTEXT("NoiseFormat", "{0}: {1} м"), Strength, FText::AsNumber(Meters));
		DrawCentered(Line.ToString(), FLinearColor(1.f, 0.45f, 0.1f), 0.1f);
	}
}

void AObshagaHUD::DrawPhone(const AObshagaPlayerState* MyState, const AObshagaGameState* GameState)
{
	const float PanelWidth = 300.f * Scale;
	const float PanelX = Canvas->ClipX - PanelWidth - 12.f * Scale;
	const float PanelY = 40.f * Scale;
	const float Pad = 12.f * Scale;
	const float TextWidth = PanelWidth - Pad * 2.f;
	DrawRect(FLinearColor(0.02f, 0.02f, 0.05f, 0.85f), PanelX, PanelY, PanelWidth, Canvas->ClipY - PanelY - 50.f * Scale);

	const FLinearColor Dim(0.7f, 0.7f, 0.75f);
	const float X = PanelX + Pad;
	float Y = PanelY + Pad;

	Y = DrawWrapped(FText::Format(LOCTEXT("PhoneTitle", "ТЕЛЕФОН — {0}"), FText::FromString(MyState->GetPlayerName())).ToString(), FLinearColor::White, X, Y, TextWidth);

	const ARoomVolume* HomeRoom = ARoomVolume::FindRoomById(this, MyState->GetHomeRoomId());
	const FText HomeName = HomeRoom ? HomeRoom->DisplayName : LOCTEXT("NoHome", "нет");
	Y = DrawWrapped(FText::Format(LOCTEXT("PhoneHome", "Твоя комната: {0}"), HomeName).ToString(), Dim, X, Y, TextWidth);

	if (GameState->GetRoundState() != ERoundState::InProgress)
	{
		DrawWrapped(LOCTEXT("PhoneWaiting", "Раунд ещё не начался").ToString(), Dim, X, Y, TextWidth);
		return;
	}

	// Роль. Крыса видит чужое задание и может один раз настучать.
	const bool bRat = MyState->GetVisibleRole() == EPlayerRole::Rat;
	Y = DrawWrapped(FText::Format(LOCTEXT("PhoneRole", "Роль: {0}"), RoleName(MyState->GetVisibleRole())).ToString(), bRat ? FLinearColor(1.f, 0.5f, 0.5f) : Dim, X, Y, TextWidth);
	if (bRat && !MyState->GetRatIntel().IsEmpty())
	{
		Y = DrawWrapped(MyState->GetRatIntel().ToString(), FLinearColor(1.f, 0.7f, 0.7f), X, Y, TextWidth);
		const AObshagaPlayerState* Victim = Cast<AObshagaPlayerState>(MyState->GetRatTarget());
		FText Tip = LOCTEXT("TipPrompt", "[T] Настучать на него коменданту");
		if (MyState->HasUsedTip())
		{
			Tip = LOCTEXT("TipUsed", "Ты уже настучал");
		}
		else if (Victim && Victim->IsEvicted())
		{
			Tip = LOCTEXT("TipVictimEvicted", "Его уже выселили — стучать не на кого");
		}
		Y = DrawWrapped(Tip.ToString(), FLinearColor::Yellow, X, Y, TextWidth);
	}

	const USuspicionComponent* Suspicion = MyState->GetSuspicionComponent();
	const int32 SuspicionPercent = Suspicion->GetSuspicionPercent();
	const FLinearColor SuspicionColor = SuspicionPercent >= 70 ? FLinearColor::Red : (SuspicionPercent >= 35 ? FLinearColor::Yellow : Dim);
	Y = DrawWrapped(FText::Format(LOCTEXT("PhoneStats", "Очки: {0}   Страйки: {1} из 3"), FText::AsNumber(FMath::RoundToInt32(MyState->GetScore())), FText::AsNumber(Suspicion->GetStrikes())).ToString(), Dim, X, Y, TextWidth);
	Y = DrawWrapped(FText::Format(LOCTEXT("PhoneSuspicion", "Подозрение: {0} из 100"), FText::AsNumber(SuspicionPercent)).ToString(), SuspicionColor, X, Y, TextWidth);

	Y += 6.f * Scale;
	Y = DrawWrapped(LOCTEXT("PhoneTasks", "СЕКРЕТНЫЕ ЗАДАНИЯ").ToString(), FLinearColor::White, X, Y, TextWidth);
	for (const FTaskState& Task : MyState->GetTaskComponent()->GetTasks())
	{
		const FText Kind = Task.bMain ? LOCTEXT("TaskMain", "Основное") : LOCTEXT("TaskSide", "Побочное");
		Y = DrawWrapped(FText::Format(LOCTEXT("PhoneTaskTitle", "{0}: {1} (+{2})"), Kind, Task.Title, FText::AsNumber(Task.Reward)).ToString(), FLinearColor::Yellow, X, Y, TextWidth);
		Y = DrawWrapped(Task.Description.ToString(), Dim, X, Y, TextWidth, 18.f);

		if (Task.bSatisfiedNow)
		{
			Y = DrawWrapped(LOCTEXT("StatusSatisfied", "Сейчас выполнено — продержись до конца").ToString(), FLinearColor(0.5f, 1.f, 0.5f), X, Y, TextWidth, 18.f);
		}
		Y += 4.f * Scale;
	}

	if (!MyState->GetSmsMessages().IsEmpty())
	{
		Y += 2.f * Scale;
		Y = DrawWrapped(LOCTEXT("PhoneSms", "СМС ОТ НЕИЗВЕСТНОГО").ToString(), FLinearColor::White, X, Y, TextWidth);
		for (const FText& Sms : MyState->GetSmsMessages())
		{
			Y = DrawWrapped(Sms.ToString(), FLinearColor(0.75f, 0.9f, 1.f), X, Y, TextWidth, 18.f);
		}
	}
}

void AObshagaHUD::DrawRoundResults(const AObshagaGameState* GameState)
{
	// На широком экране — две колонки: слева хроника, справа игроки с их заданиями. Так итоги на 8 игроков
	// помещаются целиком. В узком окне (тесты в редакторе) — одна колонка, и подписи к титулам уступают место.
	const int32 NumPlayers = GameState->GetRevealedPlayers().Num();
	const bool bWide = Canvas->ClipX >= 900.f * Scale;
	const float PanelWidth = FMath::Min(Canvas->ClipX - 30.f * Scale, (bWide ? 940.f : 600.f) * Scale);
	const float PanelX = (Canvas->ClipX - PanelWidth) * 0.5f;
	const float PanelY = 34.f * Scale;
	const float Pad = 12.f * Scale;
	const float TextWidth = bWide ? (PanelWidth - Pad * 3.f) * 0.5f : PanelWidth - Pad * 2.f;
	const float Line = (bWide || NumPlayers <= 4) ? 17.f : 15.f;
	const bool bCaptions = bWide || NumPlayers <= 4;
	DrawRect(FLinearColor(0.02f, 0.02f, 0.05f, 0.92f), PanelX, PanelY, PanelWidth, Canvas->ClipY - PanelY - 8.f * Scale);

	const FLinearColor Dim(0.82f, 0.82f, 0.88f);
	float X = PanelX + Pad;
	float Y = PanelY + Pad * 0.6f;

	// «Реванш» — всегда внизу, на одном месте: его не вытеснит ни длинная хроника, ни восемь игроков.
	const bool bHost = GetNetMode() != NM_Client;
	DrawText((bHost ? LOCTEXT("RematchHost", "[Enter] Реванш — новые роли и задания") : LOCTEXT("RematchClient", "Ждём, пока хост начнёт реванш")).ToString(),
		FLinearColor::Yellow, X, Canvas->ClipY - 8.f * Scale - Pad - Line * Scale, Font, Scale);

	Y = DrawWrapped(LOCTEXT("ResultsChronicle", "ХРОНИКА РАУНДА").ToString(), FLinearColor::White, X, Y, TextWidth, Line);
	if (GameState->GetChronicle().IsEmpty())
	{
		Y = DrawWrapped(LOCTEXT("ChronicleEmpty", "Тихий вечер. Никто ничего не натворил").ToString(), Dim, X, Y, TextWidth, Line);
	}
	for (const FText& Entry : GameState->GetChronicle())
	{
		Y = DrawWrapped(Entry.ToString(), Dim, X, Y, TextWidth, Line);
	}

	if (bWide)
	{
		X += TextWidth + Pad;
		Y = PanelY + Pad * 0.6f;
	}
	else
	{
		Y += 6.f * Scale;
	}

	// Игрок, его титул и сразу под ним — его секретные задания одной строкой.
	Y = DrawWrapped(LOCTEXT("ResultsPlayers", "КТО КЕМ БЫЛ И ЧТО ДЕЛАЛ").ToString(), FLinearColor::White, X, Y, TextWidth, Line);
	for (const FRevealedPlayer& Player : GameState->GetRevealedPlayers())
	{
		const FText Text = FText::Format(LOCTEXT("PlayerLine", "{0} — {1}, «{2}». Очки: {3}, страйки: {4}"),
			FText::FromString(Player.PlayerName), RoleName(Player.Role), Player.Title, FText::AsNumber(Player.Score), FText::AsNumber(Player.Strikes));
		const FLinearColor Color = Player.Role == EPlayerRole::Resident ? Dim : FLinearColor(1.f, 0.7f, 0.7f);
		Y = DrawWrapped(Text.ToString(), Color, X, Y, TextWidth, Line);
		if (bCaptions && !Player.Caption.IsEmpty())
		{
			Y = DrawWrapped(Player.Caption.ToString(), FLinearColor(0.6f, 0.6f, 0.68f), X + 14.f * Scale, Y, TextWidth - 14.f * Scale, Line - 2.f);
		}

		FString Tasks;
		bool bAnyDone = false;
		for (const FRevealedTask& Task : GameState->GetRevealedTasks())
		{
			if (Task.PlayerName != Player.PlayerName)
			{
				continue;
			}
			const bool bDone = Task.Status == ETaskStatus::Completed;
			bAnyDone |= bDone;
			const FText Entry = bDone
				? FText::Format(LOCTEXT("TaskDone", "«{0}» — выполнено +{1}"), Task.TaskTitle, FText::AsNumber(Task.Reward))
				: FText::Format(LOCTEXT("TaskFailed", "«{0}» — провалено"), Task.TaskTitle);
			Tasks += (Tasks.IsEmpty() ? TEXT("") : TEXT("; ")) + Entry.ToString();
		}
		if (!Tasks.IsEmpty())
		{
			Y = DrawWrapped(Tasks, bAnyDone ? FLinearColor(0.5f, 1.f, 0.5f) : FLinearColor(1.f, 0.55f, 0.55f), X + 14.f * Scale, Y, TextWidth - 14.f * Scale, Line - 2.f);
		}
	}
}

void AObshagaHUD::DrawInterrogation(const AObshagaPlayerState* MyState, const AObshagaGameState* GameState, const FVector& MyLocation)
{
	const FInterrogationInfo& Info = GameState->GetInterrogation();
	if (!Info.Suspect)
	{
		return;
	}

	const int32 Seconds = FMath::CeilToInt32(GameState->GetInterrogationRemainingSeconds());
	const float PanelWidth = FMath::Min(Canvas->ClipX - 40.f * Scale, 420.f * Scale);
	const float PanelX = (Canvas->ClipX - PanelWidth) * 0.5f;
	const float PanelY = Canvas->ClipY * 0.28f;
	const float Pad = 12.f * Scale;
	const float TextWidth = PanelWidth - Pad * 2.f;
	const float X = PanelX + Pad;
	float Y = PanelY + Pad;

	if (Info.Suspect == MyState)
	{
		DrawRect(FLinearColor(0.25f, 0.02f, 0.02f, 0.85f), PanelX, PanelY, PanelWidth, 150.f * Scale);
		Y = DrawWrapped(FText::Format(LOCTEXT("InterrogationTitle", "ДОПРОС! Осталось {0} с"), FText::AsNumber(Seconds)).ToString(), FLinearColor::White, X, Y, TextWidth);
		if (Info.Choice == EInterrogationChoice::None)
		{
			Y = DrawWrapped(LOCTEXT("ChoiceConfess", "[1] Сознаться — страйк, маленький штраф").ToString(), FLinearColor::Yellow, X, Y, TextWidth);
			Y = DrawWrapped(LOCTEXT("ChoiceLie", "[2] Соврать — если поверит, уйдёшь чистым").ToString(), FLinearColor::Yellow, X, Y, TextWidth);
			Y = DrawWrapped(LOCTEXT("ChoiceSilent", "[3] Молчать — страйк, средний штраф").ToString(), FLinearColor::Yellow, X, Y, TextWidth);
		}
		else
		{
			Y = DrawWrapped(LOCTEXT("ChoiceMade", "Ты соврал. Комендант думает...").ToString(), FLinearColor::Yellow, X, Y, TextWidth);
		}
		DrawWrapped(FText::Format(LOCTEXT("AlibiCount", "Алиби подтвердили: {0}"), FText::AsNumber(Info.AlibiCount)).ToString(), FLinearColor(0.8f, 0.8f, 0.85f), X, Y, TextWidth);
		return;
	}

	// Остальным — предложение вступиться, если они достаточно близко к пойманному.
	// Призраку и спрятавшемуся не предлагаем: их алиби сервер не примет.
	const AObshagaCharacter* Me = Cast<AObshagaCharacter>(GetOwningPawn());
	const APawn* SuspectPawn = Info.Suspect->GetPawn();
	if (Me && !Me->IsGhost() && !Me->IsHiding() && SuspectPawn && FVector::Dist(SuspectPawn->GetActorLocation(), MyLocation) <= GameState->GetAlibiRadius())
	{
		DrawRect(FLinearColor(0.02f, 0.02f, 0.05f, 0.82f), PanelX, PanelY, PanelWidth, 70.f * Scale);
		Y = DrawWrapped(FText::Format(LOCTEXT("OtherInterrogated", "Комендант допрашивает: {0} ({1} с)"), FText::FromString(Info.Suspect->GetPlayerName()), FText::AsNumber(Seconds)).ToString(), FLinearColor::White, X, Y, TextWidth);
		DrawWrapped(LOCTEXT("AlibiPrompt", "[Y] Подтвердить алиби").ToString(), FLinearColor::Yellow, X, Y, TextWidth);
	}
}

void AObshagaHUD::DrawKomendantLabels(const FVector& MyLocation)
{
	const APlayerController* Controller = GetOwningPlayerController();
	for (TActorIterator<AKomendantCharacter> It(GetWorld()); It; ++It)
	{
		const AKomendantCharacter* Komendant = *It;

		// Подпись видна, только если коменданта видно: сквозь стены она его не выдаёт.
		if (FVector::Dist(Komendant->GetActorLocation(), MyLocation) > 3000.f || !Controller->LineOfSightTo(Komendant))
		{
			continue;
		}

		const FVector Screen = Canvas->Project(Komendant->GetActorLocation() + FVector(0.f, 0.f, 115.f));
		if (Screen.Z <= 0.f)
		{
			continue;
		}

		FText Label = LOCTEXT("KomendantCalm", "КОМЕНДАНТ");
		FLinearColor Color(1.f, 0.85f, 0.3f);
		if (Komendant->GetAlert() == EKomendantAlert::Suspicious)
		{
			Label = LOCTEXT("KomendantSuspicious", "КОМЕНДАНТ ?");
			Color = FLinearColor(1.f, 0.55f, 0.1f);
		}
		else if (Komendant->GetAlert() == EKomendantAlert::Chasing)
		{
			Label = LOCTEXT("KomendantChasing", "КОМЕНДАНТ !");
			Color = FLinearColor::Red;
		}

		float Width = 0.f;
		float Height = 0.f;
		GetTextSize(Label.ToString(), Width, Height, Font, Scale);
		DrawText(Label.ToString(), Color, Screen.X - Width * 0.5f, Screen.Y - Height, Font, Scale);
	}

	// Сломанный прибор видно издалека — тоже только при прямой видимости.
	for (TActorIterator<ADeviceActor> It(GetWorld()); It; ++It)
	{
		const ADeviceActor* Device = *It;
		if (!Device->IsBroken() || FVector::Dist(Device->GetActorLocation(), MyLocation) > 1500.f || !Controller->LineOfSightTo(Device))
		{
			continue;
		}

		const FVector Screen = Canvas->Project(Device->GetLabelLocation());
		if (Screen.Z <= 0.f)
		{
			continue;
		}

		const FString Label = FText::Format(LOCTEXT("DeviceBroken", "{0}: СЛОМАНО"), Device->GetDisplayName()).ToString();
		float Width = 0.f;
		float Height = 0.f;
		GetTextSize(Label, Width, Height, Font, Scale);
		DrawText(Label, FLinearColor(1.f, 0.4f, 0.2f), Screen.X - Width * 0.5f, Screen.Y - Height, Font, Scale);
	}
}

#undef LOCTEXT_NAMESPACE
