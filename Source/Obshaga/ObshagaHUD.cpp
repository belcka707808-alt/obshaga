#include "ObshagaHUD.h"

#include "CarryComponent.h"
#include "DeviceActor.h"
#include "InteractionComponent.h"
#include "ItemActor.h"
#include "KomendantCharacter.h"
#include "ObshagaCharacter.h"
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
	constexpr float NoticeSeconds = 3.f;
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
	if (bInProgress && GameState->GetPhase() == ERoundPhase::Night)
	{
		// Ночь: пока просто затемняем экран; настоящий свет — на M6.
		DrawRect(FLinearColor(0.f, 0.f, 0.06f, 0.3f), 0.f, 0.f, Canvas->ClipX, Canvas->ClipY);
	}

	const AObshagaCharacter* Character = Cast<AObshagaCharacter>(GetOwningPawn());
	const FVector MyLocation = Character ? Character->GetActorLocation() : FVector::ZeroVector;
	if (Character)
	{
		DrawCharacterInfo(Character);
		DrawKomendantLabels(MyLocation);
		DrawNoise(Controller, MyLocation);
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
		const FString Hint = LOCTEXT("PhoneHint", "[Tab] телефон").ToString();
		float HintWidth = 0.f;
		float HintHeight = 0.f;
		GetTextSize(Hint, HintWidth, HintHeight, Font, Scale);
		DrawText(Hint, FLinearColor(1.f, 1.f, 1.f, 0.6f), Canvas->ClipX - HintWidth - 16.f * Scale, 12.f * Scale, Font, Scale);
	}

	// Сообщения рисуем последними, чтобы телефон их не закрывал.
	if (GetWorld()->GetTimeSeconds() - Controller->GetNoticeTime() < NoticeSeconds && GameState->GetRoundState() != ERoundState::Finished)
	{
		const FString Notice = Controller->GetNotice().ToString();
		float Width = 0.f;
		float Height = 0.f;
		GetTextSize(Notice, Width, Height, Font, Scale);
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.6f), (Canvas->ClipX - Width) * 0.5f - 8.f * Scale, Canvas->ClipY * 0.62f - 4.f * Scale, Width + 16.f * Scale, Height + 8.f * Scale);
		DrawCentered(Notice, FLinearColor::White, 0.62f);
	}
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

	// Клавиши, которые дают задания: показать на вора и настучать на комнату.
	const AObshagaPlayerController* Controller = Cast<AObshagaPlayerController>(GetOwningPlayerController());
	const AObshagaPlayerState* MyState = Controller ? Controller->GetPlayerState<AObshagaPlayerState>() : nullptr;
	const AObshagaCharacter* Suspect = Controller ? Controller->FindAccuseTarget() : nullptr;
	if (Suspect && Suspect->GetPlayerState())
	{
		const FText Line = FText::Format(LOCTEXT("AccusePrompt", "[R] Сказать коменданту: вор — {0}"), FText::FromString(Suspect->GetPlayerState()->GetPlayerName()));
		DrawCentered(Line.ToString(), FLinearColor(1.f, 0.6f, 0.6f), 0.82f);
	}
	const ARoomVolume* TipRoom = Character->GetCurrentRoom();
	if (MyState && MyState->CanTipRoom() && TipRoom && TipRoom->RoomType == ERoomType::Bedroom && TipRoom->RoomId != MyState->GetHomeRoomId())
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
		const FText Tip = MyState->HasUsedTip() ? LOCTEXT("TipUsed", "Ты уже настучал") : LOCTEXT("TipPrompt", "[T] Настучать на него коменданту");
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
	const float PanelWidth = FMath::Min(Canvas->ClipX - 30.f * Scale, 600.f * Scale);
	const float PanelX = (Canvas->ClipX - PanelWidth) * 0.5f;
	const float PanelY = 34.f * Scale;
	const float Pad = 12.f * Scale;
	const float TextWidth = PanelWidth - Pad * 2.f;
	const float Line = 17.f;
	DrawRect(FLinearColor(0.02f, 0.02f, 0.05f, 0.92f), PanelX, PanelY, PanelWidth, Canvas->ClipY - PanelY - 8.f * Scale);

	const FLinearColor Dim(0.82f, 0.82f, 0.88f);
	const float X = PanelX + Pad;
	float Y = PanelY + Pad * 0.6f;

	Y = DrawWrapped(LOCTEXT("ResultsChronicle", "ХРОНИКА РАУНДА").ToString(), FLinearColor::White, X, Y, TextWidth, Line);
	if (GameState->GetChronicle().IsEmpty())
	{
		Y = DrawWrapped(LOCTEXT("ChronicleEmpty", "Тихий вечер. Никто ничего не натворил").ToString(), Dim, X, Y, TextWidth, Line);
	}
	for (const FText& Entry : GameState->GetChronicle())
	{
		Y = DrawWrapped(Entry.ToString(), Dim, X, Y, TextWidth, Line);
	}

	Y += 6.f * Scale;
	Y = DrawWrapped(LOCTEXT("ResultsPlayers", "КТО КЕМ БЫЛ").ToString(), FLinearColor::White, X, Y, TextWidth, Line);
	for (const FRevealedPlayer& Player : GameState->GetRevealedPlayers())
	{
		const FText Text = FText::Format(LOCTEXT("PlayerLine", "{0} — {1}, «{2}». Очки: {3}, страйки: {4}"),
			FText::FromString(Player.PlayerName), RoleName(Player.Role), Player.Title, FText::AsNumber(Player.Score), FText::AsNumber(Player.Strikes));
		const FLinearColor Color = Player.Role == EPlayerRole::Resident ? Dim : FLinearColor(1.f, 0.7f, 0.7f);
		Y = DrawWrapped(Text.ToString(), Color, X, Y, TextWidth, Line);
	}

	Y += 6.f * Scale;
	Y = DrawWrapped(LOCTEXT("ResultsTasks", "СЕКРЕТНЫЕ ЗАДАНИЯ").ToString(), FLinearColor::White, X, Y, TextWidth, Line);
	for (const FRevealedTask& Task : GameState->GetRevealedTasks())
	{
		const bool bDone = Task.Status == ETaskStatus::Completed;
		const FText Outcome = bDone ? FText::Format(LOCTEXT("ResultDone", "выполнено +{0}"), FText::AsNumber(Task.Reward)) : LOCTEXT("ResultFailed", "провалено");
		const FText Text = FText::Format(LOCTEXT("ResultLine", "{0}: «{1}» — {2}"), FText::FromString(Task.PlayerName), Task.TaskTitle, Outcome);
		Y = DrawWrapped(Text.ToString(), bDone ? FLinearColor::Green : FLinearColor(1.f, 0.45f, 0.45f), X, Y, TextWidth, Line);
	}

	Y += 6.f * Scale;
	const bool bHost = GetNetMode() != NM_Client;
	DrawWrapped((bHost ? LOCTEXT("RematchHost", "[Enter] Реванш — новые роли и задания") : LOCTEXT("RematchClient", "Ждём, пока хост начнёт реванш")).ToString(),
		FLinearColor::Yellow, X, Y, TextWidth, Line);
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
	const APawn* SuspectPawn = Info.Suspect->GetPawn();
	if (SuspectPawn && FVector::Dist(SuspectPawn->GetActorLocation(), MyLocation) <= GameState->GetAlibiRadius())
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
