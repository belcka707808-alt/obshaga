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
#include "ObshagaLookComponent.h"
#include "ObshagaVisuals.h"
#include "ObshagaPlayerController.h"
#include "ObshagaPlayerState.h"
#include "ObshagaSessionSubsystem.h"
#include "ObshagaSettings.h"
#include "ObshagaStory.h"
#include "ObshagaWidgets.h"
#include "RoomVolume.h"
#include "SuspicionComponent.h"
#include "TaskComponent.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/SkeletalMeshActor.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Canvas.h"
#include "Engine/PointLight.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"

#define LOCTEXT_NAMESPACE "ObshagaHUD"

namespace
{
	constexpr float NoiseSeconds = 1.5f;
	// Сколько секунд реплика коменданта держится на экране и с какого расстояния её слышно.
	constexpr float KomendantLineSeconds = 4.f;
	constexpr float KomendantVoiceDistance = 1500.f;
	// Сколько секунд в начале раунда висит карточка с завязкой вечера.
	constexpr float StoryCardSeconds = 9.f;
	// Сколько секунд после F12 на экране итогов виден путь к сохранённому снимку.
	constexpr float ScreenshotNoticeSeconds = 6.f;
	// Фотобудка для «фото» героя газеты: глубоко под картой, чтобы её никто не увидел.
	const FVector PhotoBoothOrigin(0.f, 0.f, -6000.f);
	constexpr int32 PhotoPixels = 256;
	constexpr float PhotoCameraDistance = 200.f;
	constexpr float PhotoCameraHeight = 82.f;
	constexpr float PhotoCameraFov = 40.f;
	// Через сколько секунд после сборки будки делается снимок (модель успевает встать в позу) и когда будка убирается.
	constexpr float PhotoPoseSeconds = 0.6f;
	constexpr float PhotoCleanupSeconds = 0.5f;
	constexpr float PhotoLampIntensity = 7.f;
	// Сторона фото на экране итогов (до масштаба интерфейса) и толщина белой рамки.
	constexpr float PhotoSize = 92.f;
	constexpr float PhotoFrame = 4.f;

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

	// Заставка: только чёрные полосы и строки текста, остальной интерфейс прячем.
	if (Controller->IsWatchingIntro())
	{
		if (PhoneWidget)
		{
			PhoneWidget->SetVisibility(ESlateVisibility::Collapsed);
		}
		const float Bar = Canvas->ClipY * 0.12f;
		DrawRect(FLinearColor::Black, 0.f, 0.f, Canvas->ClipX, Bar);
		DrawRect(FLinearColor::Black, 0.f, Canvas->ClipY - Bar, Canvas->ClipX, Bar);
		// Строки заставки — карточка ночи недели: «Ночь 3. Среда», её название и две строки завязки.
		TArray<FString> Lines;
		if (GameState->GetWeekNight() >= 0)
		{
			const FWeekNightInfo Night = ObshagaStory::WeekNight(GameState->GetWeekNight());
			Lines.Add(FText::Format(LOCTEXT("IntroNight", "Ночь {0}. {1}"), FText::AsNumber(GameState->GetWeekNight() + 1), ObshagaStory::WeekdayName(GameState->GetWeekNight())).ToString());
			Lines.Add(Night.Title.ToString());
			TArray<FString> TextLines;
			Night.Text.ToString().ParseIntoArrayLines(TextLines);
			Lines.Append(TextLines);
		}
		else
		{
			Lines = { LOCTEXT("Intro1", "Общага №7.").ToString(), LOCTEXT("Intro2", "Завтра проверка из деканата.").ToString(),
				LOCTEXT("Intro3", "Комендант Палыч не спит.").ToString(), LOCTEXT("Intro4", "Каждому есть что скрывать.").ToString() };
		}
		// Строки появляются по одной, каждая — на свою долю заставки.
		const float Progress = GameState->GetIntroProgress() * Lines.Num();
		const int32 LineIndex = FMath::Clamp(FMath::FloorToInt32(Progress), 0, Lines.Num() - 1);
		// Строка проявляется и гаснет за десятую часть своего времени, остальное время горит в полную силу.
		const float Fade = FMath::Clamp(FMath::Min(Progress - LineIndex, LineIndex + 1.f - Progress) * 10.f, 0.f, 1.f);
		const FString& Line = Lines[LineIndex];
		float Width = 0.f;
		float Height = 0.f;
		// Длинная строка завязки уменьшается, чтобы влезть в ширину экрана.
		GetTextSize(Line, Width, Height, Font, Scale);
		const float LineScale = FMath::Min(Scale * 2.2f, (Canvas->ClipX - 40.f * Scale) / FMath::Max(Width / Scale, 1.f));
		GetTextSize(Line, Width, Height, Font, LineScale);
		DrawText(Line, FLinearColor(1.f, 0.92f, 0.45f, Fade), (Canvas->ClipX - Width) * 0.5f, Canvas->ClipY - Bar * 0.5f - Height * 0.5f, Font, LineScale);
		if (!GameState->GetYesterdayHeadline().IsEmpty())
		{
			// Со вторника вверху — чем кончилась прошлая ночь.
			const FString Yesterday = FText::Format(LOCTEXT("IntroYesterday", "Вчера в газете: «{0}»"), GameState->GetYesterdayHeadline()).ToString();
			GetTextSize(Yesterday, Width, Height, Font, Scale);
			const float YesterdayScale = FMath::Min(Scale * 1.1f, (Canvas->ClipX - 260.f * Scale) / FMath::Max(Width / Scale, 1.f));
			GetTextSize(Yesterday, Width, Height, Font, YesterdayScale);
			DrawText(Yesterday, FLinearColor(0.85f, 0.85f, 0.9f), 16.f * Scale, (Bar - Height) * 0.5f, Font, YesterdayScale);
		}
		const FString Skip = LOCTEXT("IntroSkip", "[Пробел] пропустить").ToString();
		GetTextSize(Skip, Width, Height, Font, Scale);
		DrawText(Skip, FLinearColor(1.f, 1.f, 1.f, 0.5f), Canvas->ClipX - Width - 16.f * Scale, (Bar - Height) * 0.5f, Font, Scale);
		return;
	}

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

	if (GameState->GetRoundState() != ERoundState::Finished)
	{
		// Следующие итоги выберут раскладку заново и снимут новое фото.
		ResultsVariant = INDEX_NONE;
		ClearHeroPhoto();
	}

	if (PhoneWidget)
	{
		const bool bShowPhone = Controller->IsPhoneOpen() && GameState->GetRoundState() != ERoundState::Finished && !Controller->IsWatchingIntro();
		PhoneWidget->SetVisibility(bShowPhone ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}

	// Куда уходят подсказки справа, если им тесно: под обе строки по центру.
	const float RightBelowY = Canvas->ClipY * 0.085f + 24.f * Scale;
	bool bRightBelow = false;
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
		const FString Hint = LOCTEXT("PhoneHint", "[Q] сказать  [Tab] телефон  [Esc] настройки").ToString();
		float HintWidth = 0.f;
		float HintHeight = 0.f;
		GetTextSize(Hint, HintWidth, HintHeight, Font, Scale);
		// В узком окне подсказки доходят до строк по центру (фаза, событие ночи) — тогда они уходят под них.
		bRightBelow = Canvas->ClipX - HintWidth - 16.f * Scale < TopCenterRight + 12.f * Scale;
		DrawText(Hint, FLinearColor(1.f, 1.f, 1.f, 0.6f), Canvas->ClipX - HintWidth - 16.f * Scale, bRightBelow ? RightBelowY : 12.f * Scale, Font, Scale);
	}

	// Код комнаты всегда под рукой: его диктуют друзьям и по нему же заходят снова после вылета.
	const UObshagaSessionSubsystem* Rooms = GetGameInstance() ? GetGameInstance()->GetSubsystem<UObshagaSessionSubsystem>() : nullptr;
	if (Rooms && !Rooms->GetRoomCode().IsEmpty() && GameState->GetRoundState() != ERoundState::Finished)
	{
		const FString RoomLine = FText::Format(LOCTEXT("RoomLine", "Комната {0}   [F10] выйти"), FText::FromString(Rooms->GetRoomCode())).ToString();
		float RoomWidth = 0.f;
		float RoomHeight = 0.f;
		GetTextSize(RoomLine, RoomWidth, RoomHeight, Font, Scale);
		bRightBelow |= Canvas->ClipX - RoomWidth - 16.f * Scale < TopCenterRight + 12.f * Scale;
		DrawText(RoomLine, FLinearColor(1.f, 1.f, 1.f, 0.6f), Canvas->ClipX - RoomWidth - 16.f * Scale, bRightBelow ? RightBelowY + 20.f * Scale : 32.f * Scale, Font, Scale);
		if (GameState->GetRoundState() == ERoundState::WaitingToStart)
		{
			DrawCentered(FText::Format(LOCTEXT("RoomCodeBig", "Код комнаты: {0} — продиктуй его друзьям"), FText::FromString(Rooms->GetRoomCode())).ToString(), FLinearColor::White, 0.05f);
		}
	}

	// Карточка с завязкой вечера — первые секунды раунда.
	if (bInProgress && !bWasInProgress)
	{
		RoundStartLocalTime = GetWorld()->GetRealTimeSeconds();
	}
	bWasInProgress = bInProgress;
	if (bInProgress && GetWorld()->GetRealTimeSeconds() - RoundStartLocalTime < StoryCardSeconds && !Controller->IsPhoneOpen() && !Controller->IsSettingsOpen())
	{
		const float PanelWidth = 460.f * Scale;
		const float PanelX = (Canvas->ClipX - PanelWidth) * 0.5f;
		const float TextX = PanelX + 14.f * Scale;
		const float TextWidth = PanelWidth - 28.f * Scale;
		const FString Who = FText::Format(LOCTEXT("StoryWho", "Ты — {0}. {1}"), FText::FromString(MyState->GetPlayerName()), ObshagaStory::ResidentTrait(Character ? Character->GetLookSlot() : MyState->GetLookSlot())).ToString();
		const bool bWeek = GameState->GetWeekNight() >= 0;
		const FWeekNightInfo Night = ObshagaStory::WeekNight(FMath::Max(GameState->GetWeekNight(), 0));
		const FString Text = bWeek ? Night.Text.ToString().Replace(TEXT("\n"), TEXT(" ")) : ObshagaStory::StoryText(GameState->GetStoryIndex()).ToString();
		// Сначала меряем высоту текста, потом рисуем подложку под неё.
		float Bottom = DrawWrapped(Text, FLinearColor::White, TextX, 0.f, TextWidth, 20.f, false);
		Bottom = DrawWrapped(Who, FLinearColor::White, TextX, Bottom, TextWidth, 20.f, false);
		float Y = Canvas->ClipY * 0.16f;
		DrawRect(FLinearColor(0.02f, 0.02f, 0.05f, 0.88f), PanelX, Y - 10.f * Scale, PanelWidth, Bottom + 78.f * Scale);
		const FString Today = bWeek
			? FText::Format(LOCTEXT("StoryNight", "НОЧЬ {0} · {1} · ОБЩАГА №7"), FText::AsNumber(GameState->GetWeekNight() + 1), ObshagaStory::WeekdayName(GameState->GetWeekNight())).ToUpper().ToString()
			: LOCTEXT("StoryToday", "СЕГОДНЯ В ОБЩАГЕ №7").ToString();
		Y = DrawWrapped(Today, FLinearColor(0.6f, 0.6f, 0.68f), TextX, Y, TextWidth);
		// FText::ToUpper, а не FString: та переводит в заглавные и кириллицу.
		Y = DrawWrapped((bWeek ? Night.Title : ObshagaStory::StoryTitle(GameState->GetStoryIndex())).ToUpper().ToString(), FLinearColor::Yellow, TextX, Y, TextWidth);
		Y = DrawWrapped(Text, FLinearColor::White, TextX, Y, TextWidth);
		DrawWrapped(Who, FLinearColor(0.75f, 0.95f, 0.75f), TextX, Y + 8.f * Scale, TextWidth);
	}

	if (Controller->IsSettingsOpen())
	{
		const float PanelWidth = 380.f * Scale;
		const float PanelX = (Canvas->ClipX - PanelWidth) * 0.5f;
		float Y = Canvas->ClipY * 0.4f;
		DrawRect(FLinearColor(0.02f, 0.02f, 0.05f, 0.92f), PanelX, Y - 10.f * Scale, PanelWidth, (ObshagaSettings::NumOptions + 3) * 20.f * Scale + 20.f * Scale);
		Y = DrawWrapped(LOCTEXT("SettingsTitle", "НАСТРОЙКИ").ToString(), FLinearColor::Yellow, PanelX + 14.f * Scale, Y, PanelWidth);
		for (int32 Option = 0; Option < ObshagaSettings::NumOptions; ++Option)
		{
			Y = DrawWrapped(ObshagaSettings::Describe(Option).ToString(), FLinearColor::White, PanelX + 14.f * Scale, Y, PanelWidth);
		}
		DrawWrapped(LOCTEXT("SettingsHint", "Цифра меняет настройку. [Esc] закрыть. Игра при этом идёт!").ToString(), FLinearColor(0.75f, 0.75f, 0.82f), PanelX + 14.f * Scale, Y + 8.f * Scale, PanelWidth - 28.f * Scale);
	}

	// Сообщения рисуем последними, чтобы телефон их не закрывал.
	// Пойманному во время допроса они не нужны: «Комендант идёт за тобой!» уже неправда, а середину экрана занимает сцена.
	const bool bMyInterrogation = GameState->GetInterrogation().bActive && GameState->GetInterrogation().Suspect == MyState;
	if (GetWorld()->GetTimeSeconds() - Controller->GetNoticeTime() < AObshagaPlayerController::NoticeSeconds && GameState->GetRoundState() != ERoundState::Finished && !bMyInterrogation)
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
	// Расстояние — от камеры: она обычно за спиной игрока, но может смотреть и чужими глазами (допрос, проверки).
	FVector ViewLocation;
	FRotator ViewRotation;
	Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
	for (TActorIterator<AObshagaCharacter> It(GetWorld()); It; ++It)
	{
		const AObshagaCharacter* Speaker = *It;
		const FText Speech = Speaker->GetActiveSpeech();
		if (Speech.IsEmpty() || Speaker->IsHiding() || Speaker->IsGhost()
			|| FVector::Dist(Speaker->GetActorLocation(), ViewLocation) > Config->EmoteVisibleDistance
			|| (Speaker != Me && !Controller->LineOfSightTo(Speaker)))
		{
			continue;
		}

		const FVector Screen = Canvas->Project(Speaker->GetActorLocation() + FVector(0.f, 0.f, 125.f));
		if (Screen.Z <= 0.f)
		{
			continue;
		}

		const FString Text = Speech.ToString();
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

float AObshagaHUD::DrawWrapped(const FString& Text, const FLinearColor& Color, float X, float Y, float MaxWidth, float LineHeight, bool bDraw)
{
	// Строки телефона сейчас собираются для виджета, а не рисуются: белые строки в нём — заголовки.
	if (PhoneCollect)
	{
		PhoneCollect->Add({ Text, Color, Color == FLinearColor::White });
		return Y;
	}

	TArray<FString> Parsed;
	Text.ParseIntoArray(Parsed, TEXT(" "));

	// Слово шире всей строки (длинный ник без пробелов) режем на куски по буквам, иначе оно вылезет за панель.
	TArray<FString> Words;
	for (const FString& Word : Parsed)
	{
		float WordWidth = 0.f;
		float WordHeight = 0.f;
		GetTextSize(Word, WordWidth, WordHeight, Font, Scale);
		if (WordWidth <= MaxWidth || Word.Len() < 2)
		{
			Words.Add(Word);
			continue;
		}
		FString Piece;
		for (const TCHAR Char : Word)
		{
			GetTextSize(Piece + Char, WordWidth, WordHeight, Font, Scale);
			if (WordWidth > MaxWidth && !Piece.IsEmpty())
			{
				Words.Add(Piece);
				Piece.Reset();
			}
			Piece.AppendChar(Char);
		}
		Words.Add(Piece);
	}

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
			if (bDraw)
			{
				DrawText(Line, Color, X, Y, Font, Scale);
			}
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
		if (bDraw)
		{
			DrawText(Line, Color, X, Y, Font, Scale);
		}
		Y += Step;
	}
	return Y;
}

void AObshagaHUD::DrawTopLine(const FString& Line, const FLinearColor& Color, float YFraction)
{
	float Width = 0.f;
	float Height = 0.f;
	GetTextSize(Line, Width, Height, Font, Scale);
	TopCenterRight = FMath::Max(TopCenterRight, (Canvas->ClipX + Width) * 0.5f);
	DrawCentered(Line, Color, YFraction);
}

void AObshagaHUD::DrawTopStatus(const AObshagaGameState* GameState)
{
	TopCenterRight = 0.f;
	// Сверху по центру: в лобби — как начать, в раунде — фаза и таймер.
	const bool bHost = GetNetMode() != NM_Client;
	switch (GameState->GetRoundState())
	{
	case ERoundState::WaitingToStart:
	{
		// Крупно и по центру: без старта раунда нет ни заданий, ни коменданта, и новички этого не понимали.
		const FString Big = (bHost ? LOCTEXT("LobbyHostBig", "Нажми [Enter], чтобы начать раунд") : LOCTEXT("LobbyClientBig", "Ждём, когда хост начнёт раунд")).ToString();
		const FString Small = FText::Format(LOCTEXT("LobbyCount", "Игроков в комнате: {0}"), FText::AsNumber(GameState->PlayerArray.Num())).ToString();
		const float BigScale = Scale * 1.7f;
		// У хоста надпись мигает, у гостей горит ровно.
		const float Blink = bHost ? 0.55f + 0.45f * FMath::Abs(FMath::Sin(GetWorld()->GetRealTimeSeconds() * 3.f)) : 1.f;
		float Width = 0.f;
		float Height = 0.f;
		GetTextSize(Big, Width, Height, Font, BigScale);
		const float Y = Canvas->ClipY * 0.2f;
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.45f), (Canvas->ClipX - Width) * 0.5f - 14.f * Scale, Y - 8.f * Scale, Width + 28.f * Scale, Height + 34.f * Scale);
		DrawText(Big, FLinearColor(1.f, 0.9f, 0.1f, Blink), (Canvas->ClipX - Width) * 0.5f, Y, Font, BigScale);
		float SmallWidth = 0.f;
		float SmallHeight = 0.f;
		GetTextSize(Small, SmallWidth, SmallHeight, Font, Scale);
		DrawText(Small, FLinearColor::White, (Canvas->ClipX - SmallWidth) * 0.5f, Y + Height + 4.f * Scale, Font, Scale);
	}
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
		DrawTopLine(Line.ToString(), Color, 0.04f);

		// Событие ночи — красной строкой под таймером, пока оно идёт.
		if (GameState->IsNightEventActive())
		{
			FText EventName = LOCTEXT("TopBlackout", "ВЫБИЛО ПРОБКИ");
			bool bTimed = true;
			switch (GameState->GetNightEvent())
			{
			case ENightEvent::FireAlarm:
				EventName = LOCTEXT("TopFireAlarm", "ПОЖАРНАЯ ТРЕВОГА — ВСЕ В КОРИДОР!");
				break;
			case ENightEvent::Deanery:
				// Идёт до конца раунда, отсчёт ни к чему.
				EventName = GameState->GetPhase() == ERoundPhase::Morning ? LOCTEXT("TopDeaneryNow", "ПРОВЕРКА ИЗ ДЕКАНАТА") : LOCTEXT("TopDeanery", "УТРОМ ПРОВЕРКА ИЗ ДЕКАНАТА");
				bTimed = false;
				break;
			case ENightEvent::Birthday:
				EventName = LOCTEXT("TopBirthday", "ДЕНЬ РОЖДЕНИЯ В 203-Й");
				break;
			case ENightEvent::PipeBurst:
				EventName = LOCTEXT("TopPipe", "ПРОРВАЛО ТРУБУ — КОМЕНДАНТ В САНУЗЛЕ");
				break;
			default:
				break;
			}
			const float Blink = 0.6f + 0.4f * FMath::Abs(FMath::Sin(GetWorld()->GetRealTimeSeconds() * 4.f));
			const FText EventLine = bTimed
				? FText::Format(LOCTEXT("TopEvent", "{0}  {1}"), EventName, FText::FromString(FormatTime(GameState->GetNightEventRemainingSeconds())))
				: EventName;
			DrawTopLine(EventLine.ToString(), FLinearColor(1.f, 0.3f, 0.2f, Blink), 0.085f);
		}
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
		const AObshagaPlayerState* AccuserState = Controller->GetPlayerState<AObshagaPlayerState>();
		const FText Line = FText::Format(AccuserState && AccuserState->IsAccuseRumor() ? LOCTEXT("AccuseRumorPrompt", "[R] Сказать коменданту: слух пустил {0}") : LOCTEXT("AccusePrompt", "[R] Сказать коменданту: вор — {0}"),
			FText::FromString(Suspect->GetPlayerState()->GetPlayerName()));
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

void AObshagaHUD::BeginPlay()
{
	Super::BeginPlay();

	// Телефон показывает виджет; если он не создался, телефон рисуется текстом, как раньше.
	if (APlayerController* Controller = GetOwningPlayerController())
	{
		PhoneWidget = CreateWidget<UObshagaPhoneWidget>(Controller, UObshagaPhoneWidget::StaticClass());
		if (PhoneWidget)
		{
			PhoneWidget->AddToViewport();
			PhoneWidget->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

void AObshagaHUD::DrawPhone(const AObshagaPlayerState* MyState, const AObshagaGameState* GameState)
{
	if (!PhoneWidget)
	{
		DrawPhoneLines(MyState, GameState);
		return;
	}
	// Те же строки, что рисовались бы текстом, собираем в список и отдаём виджету.
	TArray<FPhoneLine> Lines;
	PhoneCollect = &Lines;
	DrawPhoneLines(MyState, GameState);
	PhoneCollect = nullptr;
	PhoneWidget->SetLines(Lines);
}

void AObshagaHUD::DrawPhoneLines(const AObshagaPlayerState* MyState, const AObshagaGameState* GameState)
{
	const float PanelWidth = 300.f * Scale;
	const float PanelX = Canvas->ClipX - PanelWidth - 12.f * Scale;
	const float PanelY = 40.f * Scale;
	const float Pad = 12.f * Scale;
	const float TextWidth = PanelWidth - Pad * 2.f;
	if (!PhoneCollect)
	{
		DrawRect(FLinearColor(0.02f, 0.02f, 0.05f, 0.85f), PanelX, PanelY, PanelWidth, Canvas->ClipY - PanelY - 50.f * Scale);
	}

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

void AObshagaHUD::ClearHeroPhoto()
{
	for (AActor* Actor : PhotoBooth)
	{
		if (Actor)
		{
			Actor->Destroy();
		}
	}
	PhotoBooth.Reset();
	PhotoCapture = nullptr;
	bHeroPhotoStarted = false;
	bHeroPhotoReady = false;
}

void AObshagaHUD::UpdateHeroPhoto(const AObshagaGameState* GameState)
{
	const float Now = GetWorld()->GetRealTimeSeconds();
	if (bHeroPhotoStarted)
	{
		if (!bHeroPhotoReady && PhotoCapture && Now >= PhotoShootTime)
		{
			PhotoCapture->CaptureScene();
			bHeroPhotoReady = true;
		}
		else if (bHeroPhotoReady && !PhotoBooth.IsEmpty() && Now >= PhotoShootTime + PhotoCleanupSeconds)
		{
			// Снимок сделан — будка больше не нужна; текстура с фото остаётся.
			for (AActor* Actor : PhotoBooth)
			{
				if (Actor)
				{
					Actor->Destroy();
				}
			}
			PhotoBooth.Reset();
			PhotoCapture = nullptr;
		}
		return;
	}

	// Герой известен всем машинам (реплицируется), а модель берём по номеру внешности его персонажа.
	const APlayerState* Hero = GameState->GetPaperHero();
	const AObshagaCharacter* HeroCharacter = Hero ? Cast<AObshagaCharacter>(Hero->GetPawn()) : nullptr;
	const UObshagaLookConfig* Looks = UObshagaLookConfig::Get();
	if (!HeroCharacter || Looks->ResidentMeshes.IsEmpty())
	{
		return;
	}
	USkeletalMesh* Model = Looks->ResidentMeshes[HeroCharacter->GetLookSlot() % Looks->ResidentMeshes.Num()];
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	bHeroPhotoStarted = true;
	if (!Model || !Cube)
	{
		return;
	}
	HeroPhotoName = Hero->GetPlayerName();

	// Пойманный хоть раз снимается в позе «пойман», остальные — с первым жестом.
	bool bCaught = false;
	for (const FRevealedPlayer& Player : GameState->GetRevealedPlayers())
	{
		bCaught |= Player.PlayerName == HeroPhotoName && Player.Strikes > 0;
	}
	UAnimSequenceBase* Pose = bCaught && Looks->Caught ? Looks->Caught.Get() : (Looks->EmoteGestures.IsEmpty() ? nullptr : Looks->EmoteGestures[0].Get());
	Pose = Pose ? Pose : Looks->Idle.Get();

	// Модель: ногами на пол будки, лицом к камере (камера стоит на оси X).
	const FBoxSphereBounds Bounds = Model->GetBounds();
	const float ModelScale = Looks->ResidentHeight / FMath::Max(Bounds.BoxExtent.Z * 2.f, 1.f);
	const float FeetZ = (Bounds.Origin.Z - Bounds.BoxExtent.Z) * ModelScale;
	if (ASkeletalMeshActor* Body = GetWorld()->SpawnActor<ASkeletalMeshActor>(PhotoBoothOrigin - FVector(0.f, 0.f, FeetZ), FRotator(0.f, Looks->ModelYaw, 0.f)))
	{
		USkeletalMeshComponent* Mesh = Body->GetSkeletalMeshComponent();
		Mesh->SetSkeletalMesh(Model);
		Body->SetActorScale3D(FVector(ModelScale));
		// Будку не видит ни одна обычная камера, поэтому позу надо считать принудительно.
		Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Mesh->SetCastShadow(false);
		if (Pose)
		{
			Mesh->PlayAnimation(Pose, true);
		}
		PhotoBooth.Add(Body);
	}

	// Фон и «ростовая линейка», как на фото в личное дело.
	auto AddBox = [this, Cube](const FVector& Offset, const FVector& Size, const FLinearColor& Color)
	{
		if (AStaticMeshActor* Box = GetWorld()->SpawnActor<AStaticMeshActor>(PhotoBoothOrigin + Offset, FRotator::ZeroRotator))
		{
			UStaticMeshComponent* Mesh = Box->GetStaticMeshComponent();
			Mesh->SetMobility(EComponentMobility::Movable);
			Mesh->SetStaticMesh(Cube);
			Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Mesh->SetCastShadow(false);
			Box->SetActorScale3D(Size / 100.f);
			ObshagaVisuals::Tint(Mesh, Color);
			PhotoBooth.Add(Box);
		}
	};
	AddBox(FVector(-70.f, 0.f, 100.f), FVector(4.f, 500.f, 400.f), FLinearColor(0.80f, 0.74f, 0.58f));
	for (const float Height : { 60.f, 90.f, 120.f, 150.f })
	{
		AddBox(FVector(-66.f, 0.f, Height), FVector(4.f, 500.f, 2.5f), FLinearColor(0.25f, 0.22f, 0.18f));
	}

	if (APointLight* Lamp = GetWorld()->SpawnActor<APointLight>(PhotoBoothOrigin + FVector(160.f, 90.f, 190.f), FRotator::ZeroRotator))
	{
		UPointLightComponent* Light = Lamp->PointLightComponent;
		Light->SetMobility(EComponentMobility::Movable);
		Light->bUseInverseSquaredFalloff = false;
		Light->SetLightFalloffExponent(2.f);
		Light->SetCastShadows(false);
		Light->SetIntensity(PhotoLampIntensity);
		Light->SetAttenuationRadius(700.f);
		PhotoBooth.Add(Lamp);
	}

	if (!HeroPhoto)
	{
		HeroPhoto = NewObject<UTextureRenderTarget2D>(this);
		HeroPhoto->RenderTargetFormat = RTF_RGBA8;
		HeroPhoto->InitAutoFormat(PhotoPixels, PhotoPixels);
		HeroPhoto->UpdateResourceImmediate(true);
	}
	const FVector CameraAt = PhotoBoothOrigin + FVector(PhotoCameraDistance, 0.f, PhotoCameraHeight);
	if (ASceneCapture2D* Camera = GetWorld()->SpawnActor<ASceneCapture2D>(CameraAt, FRotator(0.f, 180.f, 0.f)))
	{
		PhotoCapture = Camera->GetCaptureComponent2D();
		PhotoCapture->TextureTarget = HeroPhoto;
		PhotoCapture->CaptureSource = SCS_FinalColorLDR;
		PhotoCapture->FOVAngle = PhotoCameraFov;
		PhotoCapture->bCaptureEveryFrame = false;
		PhotoCapture->bCaptureOnMovement = false;
		// В кадр попадает только будка — ни неба, ни карты над ней.
		PhotoCapture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
		for (AActor* Actor : PhotoBooth)
		{
			PhotoCapture->ShowOnlyActors.Add(Actor);
		}
		PhotoBooth.Add(Camera);
	}
	PhotoShootTime = Now + PhotoPoseSeconds;
}

void AObshagaHUD::DrawRoundResults(const AObshagaGameState* GameState)
{
	UpdateHeroPhoto(GameState);
	const bool bPhoto = bHeroPhotoReady && HeroPhoto;
	const float PanelY = 34.f * Scale;
	const float Pad = 12.f * Scale;
	const FLinearColor Dim(0.82f, 0.82f, 0.88f);
	// «Реванш» — всегда внизу, на одном месте; всё остальное обязано уместиться выше этой строки.
	const float RematchY = Canvas->ClipY - 8.f * Scale - Pad - 17.f * Scale;

	// Блок одного игрока: строка с ролью и титулом, подпись и его задания одной строкой.
	auto DrawPlayer = [&](const FRevealedPlayer& Player, float X, float Y, float TextWidth, float Line, bool bCaptions, bool bDraw) -> float
	{
		const FText Text = GameState->GetWeekNight() >= 0
			? FText::Format(LOCTEXT("PlayerLineWeek", "{0} — {1}, «{2}». Очки: {3}, страйки: {4}. Неделя: {5} очков, {6} страйков"),
				FText::FromString(Player.bBot ? Player.PlayerName + LOCTEXT("BotMark", " (бот)").ToString() : Player.PlayerName), RoleName(Player.Role), Player.Title, FText::AsNumber(Player.Score), FText::AsNumber(Player.Strikes),
				FText::AsNumber(Player.WeekScore), FText::AsNumber(Player.WeekStrikes))
			: FText::Format(LOCTEXT("PlayerLine", "{0} — {1}, «{2}». Очки: {3}, страйки: {4}"),
				FText::FromString(Player.PlayerName), RoleName(Player.Role), Player.Title, FText::AsNumber(Player.Score), FText::AsNumber(Player.Strikes));
		const FLinearColor Color = Player.Role == EPlayerRole::Resident ? Dim : FLinearColor(1.f, 0.7f, 0.7f);
		Y = DrawWrapped(Text.ToString(), Color, X, Y, TextWidth, Line, bDraw);
		if (bCaptions && !Player.Caption.IsEmpty())
		{
			Y = DrawWrapped(Player.Caption.ToString(), FLinearColor(0.6f, 0.6f, 0.68f), X + 14.f * Scale, Y, TextWidth - 14.f * Scale, Line - 2.f, bDraw);
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
			Y = DrawWrapped(Tasks, bAnyDone ? FLinearColor(0.5f, 1.f, 0.5f) : FLinearColor(1.f, 0.55f, 0.55f), X + 14.f * Scale, Y, TextWidth - 14.f * Scale, Line - 2.f, bDraw);
		}
		return Y;
	};

	// Одна и та же раскладка умеет и рисовать, и только мерить высоту (bDraw = false).
	// Возвращает нижний край нарисованного.
	auto Layout = [&](bool bTwoColumns, bool bCaptions, float Line, bool bDraw) -> float
	{
		const float PanelWidth = FMath::Min(Canvas->ClipX - 30.f * Scale, (bTwoColumns ? 940.f : 600.f) * Scale);
		const float PanelX = (Canvas->ClipX - PanelWidth) * 0.5f;
		const float TextWidth = bTwoColumns ? (PanelWidth - Pad * 3.f) * 0.5f : PanelWidth - Pad * 2.f;
		float X = PanelX + Pad;
		float Y = PanelY + Pad * 0.6f;
		if (bDraw)
		{
			DrawRect(FLinearColor(0.02f, 0.02f, 0.05f, 0.92f), PanelX, PanelY, PanelWidth, Canvas->ClipY - PanelY - 8.f * Scale);
			const bool bHost = GetNetMode() != NM_Client;
			const bool bWeekOver = GameState->GetWeekNight() == ObshagaStory::NumWeekNights - 1;
			const FText RematchText = bHost
				? (bWeekOver ? LOCTEXT("RematchNewWeek", "[Enter] Новая неделя — снова понедельник") : FText::Format(LOCTEXT("RematchNextNight", "[Enter] Следующая ночь — {0}"), ObshagaStory::WeekdayName(GameState->GetWeekNight() + 1)))
				: (bWeekOver ? LOCTEXT("RematchClientWeek", "Ждём, пока хост начнёт новую неделю") : LOCTEXT("RematchClient", "Ждём, пока хост начнёт реванш"));
			DrawText((GameState->GetWeekNight() >= 0 ? RematchText : (bHost ? LOCTEXT("RematchHost", "[Enter] Реванш — новые роли и задания") : LOCTEXT("RematchClient", "Ждём, пока хост начнёт реванш"))).ToString(),
				FLinearColor::Yellow, X, RematchY, Font, Scale);
			// Справа внизу — как сохранить этот экран; после F12 несколько секунд показываем, куда он лёг.
			// На сам снимок эта строка с путём не попадает: снимок делается раньше, чем она появится.
			const AObshagaPlayerController* Me = Cast<AObshagaPlayerController>(GetOwningPlayerController());
			const bool bSaved = Me && GetWorld()->GetRealTimeSeconds() - Me->GetScreenshotTime() < ScreenshotNoticeSeconds;
			const FString ShotLine = bSaved ? FText::Format(LOCTEXT("ShotSaved", "Сохранено: {0}"), FText::FromString(Me->GetScreenshotPath())).ToString() : LOCTEXT("ShotHint", "[F12] сохранить снимок").ToString();
			float ShotWidth = 0.f;
			float ShotHeight = 0.f;
			GetTextSize(ShotLine, ShotWidth, ShotHeight, Font, Scale * 0.85f);
			// Путь длинный, поэтому он встаёт строкой выше «Реванша», а не поверх него.
			DrawText(ShotLine, bSaved ? FLinearColor(0.6f, 1.f, 0.6f) : Dim, FMath::Max(PanelX + PanelWidth - Pad - ShotWidth, PanelX + Pad), bSaved ? RematchY - 18.f * Scale : RematchY, Font, Scale * 0.85f);
		}

		if (GameState->GetWeekNight() >= 0)
		{
			// Полоска недели: пройденные ночи, текущая — в скобках.
			FString Strip = LOCTEXT("WeekStrip", "НЕДЕЛЯ В ОБЩАГЕ №7: ").ToString();
			for (int32 Night = 0; Night < ObshagaStory::NumWeekNights; ++Night)
			{
				const FString Day = ObshagaStory::WeekdayShort(Night).ToString();
				Strip += Night == GameState->GetWeekNight() ? FString::Printf(TEXT(" [%s] "), *ObshagaStory::WeekdayShort(Night).ToUpper().ToString()) : FString::Printf(TEXT(" %s "), *Day);
			}
			Y = DrawWrapped(Strip, FLinearColor(0.75f, 0.85f, 1.f), X, Y, TextWidth, Line - 2.f, bDraw);
		}
		if (!GameState->GetHeadline().IsEmpty())
		{
			// Стенгазета: заголовок по самому громкому событию раунда.
			// Слева — фото героя номера в белой рамке с подписью, справа от него — название газеты и заголовок.
			const float Side = bPhoto ? PhotoSize * Scale : 0.f;
			const float Indent = bPhoto ? Side + Pad : 0.f;
			const float PaperTop = Y;
			if (bPhoto && bDraw)
			{
				const float Frame = PhotoFrame * Scale;
				const FString Why = GameState->GetPaperHeroNote().ToString();
				const float CaptionHeight = (Why.IsEmpty() ? 15.f : 27.f) * Scale;
				DrawRect(FLinearColor(0.95f, 0.94f, 0.90f), X, PaperTop, Side, Side + CaptionHeight);
				DrawTexture(HeroPhoto, X + Frame, PaperTop + Frame, Side - Frame * 2.f, Side - Frame * 2.f, 0.f, 0.f, 1.f, 1.f, FLinearColor::White, BLEND_Opaque);
				float NameWidth = 0.f;
				float NameHeight = 0.f;
				GetTextSize(HeroPhotoName, NameWidth, NameHeight, Font, Scale * 0.7f);
				DrawText(HeroPhotoName, FLinearColor(0.1f, 0.1f, 0.12f), X + FMath::Max((Side - NameWidth) * 0.5f, Frame), PaperTop + Side - Frame * 0.5f, Font, Scale * 0.7f);
				if (!Why.IsEmpty())
				{
					float WhyWidth = 0.f;
					float WhyHeight = 0.f;
					GetTextSize(Why, WhyWidth, WhyHeight, Font, Scale * 0.65f);
					DrawText(Why, FLinearColor(0.55f, 0.1f, 0.1f), X + FMath::Max((Side - WhyWidth) * 0.5f, Frame * 0.5f), PaperTop + Side + 10.f * Scale, Font, Scale * 0.65f);
				}
			}
			const bool bWeekIssue = !GameState->GetWeekTitles().IsEmpty();
			Y = DrawWrapped((bWeekIssue ? LOCTEXT("PaperNameWeek", "СТЕНГАЗЕТА ОБЩАГИ №7 · ИТОГИ НЕДЕЛИ") : LOCTEXT("PaperName", "СТЕНГАЗЕТА ОБЩАГИ №7")).ToString(),
				bWeekIssue ? FLinearColor(1.f, 0.6f, 0.3f) : FLinearColor(0.6f, 0.6f, 0.68f), X + Indent, Y, TextWidth - Indent, Line - 2.f, bDraw);
			Y = DrawWrapped(GameState->GetHeadline().ToString(), FLinearColor::Yellow, X + Indent, Y, TextWidth - Indent, Line, bDraw);
			Y = DrawWrapped(GameState->GetSubhead().ToString(), FLinearColor::White, X + Indent, Y, TextWidth - Indent, Line, bDraw);
			if (bPhoto)
			{
				Y = FMath::Max(Y, PaperTop + Side + (GameState->GetPaperHeroNote().IsEmpty() ? 19.f : 31.f) * Scale);
			}
			for (const FText& WeekTitle : GameState->GetWeekTitles())
			{
				Y = DrawWrapped(FText::Format(LOCTEXT("PaperWeekTitle", "★ {0}"), WeekTitle).ToString(), FLinearColor(1.f, 0.85f, 0.4f), X, Y, TextWidth, Line - 2.f, bDraw);
			}
			for (const FText& Note : GameState->GetPaperNotes())
			{
				Y = DrawWrapped(FText::Format(LOCTEXT("PaperNote", "• {0}"), Note).ToString(), Dim, X, Y, TextWidth, Line - 2.f, bDraw);
			}
			Y += 6.f * Scale;
		}
		Y = DrawWrapped(LOCTEXT("ResultsChronicle", "ХРОНИКА РАУНДА").ToString(), FLinearColor::White, X, Y, TextWidth, Line, bDraw);
		if (GameState->GetChronicle().IsEmpty())
		{
			Y = DrawWrapped(LOCTEXT("ChronicleEmpty", "Тихий вечер. Никто ничего не натворил").ToString(), Dim, X, Y, TextWidth, Line, bDraw);
		}
		for (const FText& Entry : GameState->GetChronicle())
		{
			Y = DrawWrapped(Entry.ToString(), Dim, X, Y, TextWidth, Line, bDraw);
		}

		const float Bottom = Y;
		if (bTwoColumns)
		{
			X += TextWidth + Pad;
			Y = PanelY + Pad * 0.6f;
		}
		else
		{
			Y += 6.f * Scale;
		}

		Y = DrawWrapped(LOCTEXT("ResultsPlayers", "КТО КЕМ БЫЛ И ЧТО ДЕЛАЛ").ToString(), FLinearColor::White, X, Y, TextWidth, Line, bDraw);
		const TArray<FRevealedPlayer>& Players = GameState->GetRevealedPlayers();
		for (int32 Index = 0; Index < Players.Num(); ++Index)
		{
			// Страховка: если блок игрока целиком не помещается над «Реваншем», честно пишем, сколько не влезло.
			// Под эту строчку оставляем место всем, кроме последнего игрока.
			if (bDraw)
			{
				const float Reserve = Index < Players.Num() - 1 ? Line * Scale : 0.f;
				if (DrawPlayer(Players[Index], X, Y, TextWidth, Line, bCaptions, false) > RematchY - Reserve)
				{
					DrawText(FText::Format(LOCTEXT("ResultsMore", "… и ещё {0}"), FText::AsNumber(Players.Num() - Index)).ToString(), Dim, X, Y, Font, Scale);
					break;
				}
			}
			Y = DrawPlayer(Players[Index], X, Y, TextWidth, Line, bCaptions, bDraw);
		}
		return FMath::Max(Bottom, Y);
	};

	// Раскладки от самой нарядной к самой плотной.
	struct FVariant
	{
		bool bTwoColumns;
		bool bCaptions;
		float Line;
	};
	static const FVariant Variants[] = { { true, true, 17.f }, { false, true, 17.f }, { true, false, 17.f }, { false, false, 17.f }, { true, false, 15.f }, { false, false, 15.f } };

	// Мерить раскладки дорого, поэтому выбор запоминаем, пока не изменится экран или состав итогов.
	const uint32 Key = HashCombine(HashCombine(GetTypeHash(Canvas->ClipX), GetTypeHash(Canvas->ClipY)),
		HashCombine(GetTypeHash(GameState->GetChronicle().Num()), HashCombine(GetTypeHash(GameState->GetRevealedPlayers().Num()), HashCombine(GetTypeHash(GameState->GetRevealedTasks().Num()), HashCombine(GetTypeHash(bPhoto), GetTypeHash(GameState->GetWeekTitles().Num()))))));
	if (ResultsVariant == INDEX_NONE || ResultsKey != Key)
	{
		// Берём первую раскладку, которая помещается над «Реваншем»; если не помещается ни одна — самую низкую.
		ResultsKey = Key;
		ResultsVariant = INDEX_NONE;
		float BestHeight = TNumericLimits<float>::Max();
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Variants); ++Index)
		{
			// Две колонки имеют смысл, только если экран шире одной колонки.
			if (Variants[Index].bTwoColumns && Canvas->ClipX < 700.f * Scale)
			{
				continue;
			}
			const float Height = Layout(Variants[Index].bTwoColumns, Variants[Index].bCaptions, Variants[Index].Line, false);
			if (Height < BestHeight)
			{
				BestHeight = Height;
				ResultsVariant = Index;
			}
			if (Height <= RematchY)
			{
				ResultsVariant = Index;
				break;
			}
		}
	}

	const FVariant& Chosen = Variants[FMath::Max(ResultsVariant, 0)];
	Layout(Chosen.bTwoColumns, Chosen.bCaptions, Chosen.Line, true);
}

void AObshagaHUD::DrawInterrogation(const AObshagaPlayerState* MyState, const AObshagaGameState* GameState, const FVector& MyLocation)
{
	const FInterrogationInfo& Info = GameState->GetInterrogation();
	if (!Info.Suspect)
	{
		return;
	}

	const int32 Seconds = FMath::CeilToInt32(GameState->GetInterrogationRemainingSeconds());
	// Панель — у левого края: середину и правую часть экрана занимает сама сцена допроса (пойманный и комендант).
	const float PanelWidth = FMath::Min(Canvas->ClipX * 0.42f, 420.f * Scale);
	const float PanelX = 14.f * Scale;
	const float PanelY = Canvas->ClipY * 0.30f;
	const float Pad = 12.f * Scale;
	const float TextWidth = PanelWidth - Pad * 2.f;
	const float X = PanelX + Pad;
	float Y = PanelY + Pad;

	if (Info.Suspect == MyState)
	{
		DrawRect(FLinearColor(0.25f, 0.02f, 0.02f, 0.85f), PanelX, PanelY, PanelWidth, 190.f * Scale);
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

		// Голос коменданта слышно и через стену: субтитр внизу экрана, пока он рядом.
		const bool bSpeaking = GetWorld()->GetTimeSeconds() - Komendant->GetSpokenTime() < KomendantLineSeconds && !Komendant->GetSpokenLine().IsEmpty();
		const float Distance = FVector::Dist(Komendant->GetActorLocation(), MyLocation);
		if (bSpeaking && Distance < KomendantVoiceDistance)
		{
			const FString Subtitle = FText::Format(LOCTEXT("KomendantSubtitle", "Палыч: «{0}»"), Komendant->GetSpokenLine()).ToString();
			float SubWidth = 0.f;
			float SubHeight = 0.f;
			GetTextSize(Subtitle, SubWidth, SubHeight, Font, Scale);
			DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f), (Canvas->ClipX - SubWidth) * 0.5f - 8.f * Scale, Canvas->ClipY * 0.84f - 4.f * Scale, SubWidth + 16.f * Scale, SubHeight + 8.f * Scale);
			DrawCentered(Subtitle, FLinearColor(1.f, 0.9f, 0.6f), 0.84f);
		}

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
		// Подпись не поднимается в верхнюю полосу экрана: там таймер, событие ночи и подсказки.
		DrawText(Label.ToString(), Color, Screen.X - Width * 0.5f, FMath::Max(Screen.Y - Height, Canvas->ClipY * 0.085f + 66.f * Scale), Font, Scale);
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
