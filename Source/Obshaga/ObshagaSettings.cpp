#include "ObshagaSettings.h"

#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"
#include "Misc/ConfigCacheIni.h"

#define LOCTEXT_NAMESPACE "ObshagaSettings"

namespace
{
	const TCHAR* Section = TEXT("Obshaga");
	const TCHAR* MusicKey = TEXT("MusicVolume");
	const TCHAR* SoundKey = TEXT("SoundVolume");

	// Громкость меняется шагами по 20%, чёткость картинки — шагами по 10% от 50% до 100%.
	constexpr int32 VolumeStep = 20;
	constexpr int32 ResolutionStep = 10;
	constexpr int32 MinResolution = 50;
	// Размер окна в оконном режиме.
	const FIntPoint WindowSize(1280, 720);

	int32 MusicPercent = 100;
	int32 SoundPercent = 100;
	bool bLoaded = false;

	void LoadVolumes()
	{
		if (!bLoaded && GConfig)
		{
			bLoaded = true;
			GConfig->GetInt(Section, MusicKey, MusicPercent, GGameUserSettingsIni);
			GConfig->GetInt(Section, SoundKey, SoundPercent, GGameUserSettingsIni);
			MusicPercent = FMath::Clamp(MusicPercent, 0, 100);
			SoundPercent = FMath::Clamp(SoundPercent, 0, 100);
		}
	}

	void SaveVolumes()
	{
		GConfig->SetInt(Section, MusicKey, MusicPercent, GGameUserSettingsIni);
		GConfig->SetInt(Section, SoundKey, SoundPercent, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}

	UGameUserSettings* Video()
	{
		return GEngine ? GEngine->GetGameUserSettings() : nullptr;
	}

	/** Качество графики как одно из трёх: 0 — низкое, 1 — среднее, 2 — высокое. */
	int32 QualityLevel(const UGameUserSettings* Settings)
	{
		const int32 Level = Settings->GetOverallScalabilityLevel();
		// -1 — уровни разные (так выходит после автоподбора); берём по качеству теней как самому заметному.
		return FMath::Clamp(Level < 0 ? Settings->GetShadowQuality() : Level, 0, 2);
	}

	int32 ResolutionPercent(const UGameUserSettings* Settings)
	{
		return FMath::Clamp(FMath::RoundToInt32(Settings->GetResolutionScaleNormalized() * 100.f / ResolutionStep) * ResolutionStep, MinResolution, 100);
	}
}

float ObshagaSettings::MusicVolume()
{
	LoadVolumes();
	return MusicPercent / 100.f;
}

float ObshagaSettings::SoundVolume()
{
	LoadVolumes();
	return SoundPercent / 100.f;
}

FText ObshagaSettings::Describe(int32 Option)
{
	LoadVolumes();
	const UGameUserSettings* Settings = Video();
	switch (Option)
	{
	case 0:
	{
		static const FText Names[] = { LOCTEXT("QualityLow", "низкое"), LOCTEXT("QualityMedium", "среднее"), LOCTEXT("QualityHigh", "высокое") };
		return FText::Format(LOCTEXT("Quality", "[1] Качество графики: {0}"), Settings ? Names[QualityLevel(Settings)] : FText::GetEmpty());
	}
	case 1:
		return FText::Format(LOCTEXT("Resolution", "[2] Чёткость картинки: {0}%  (меньше — быстрее)"), FText::AsNumber(Settings ? ResolutionPercent(Settings) : 100));
	case 2:
		return Settings && Settings->GetFullscreenMode() == EWindowMode::Windowed
			? LOCTEXT("Windowed", "[3] Режим: в окне")
			: LOCTEXT("Fullscreen", "[3] Режим: на весь экран");
	case 3:
		return FText::Format(LOCTEXT("Music", "[4] Громкость музыки: {0}%"), FText::AsNumber(MusicPercent));
	default:
		return FText::Format(LOCTEXT("Sound", "[5] Громкость звуков: {0}%"), FText::AsNumber(SoundPercent));
	}
}

void ObshagaSettings::Step(int32 Option)
{
	LoadVolumes();
	UGameUserSettings* Settings = Video();
	switch (Option)
	{
	case 0:
		if (Settings)
		{
			// Чёткость картинки — отдельная настройка, её общий уровень качества не должен сбрасывать.
			const float Resolution = Settings->GetResolutionScaleNormalized();
			Settings->SetOverallScalabilityLevel((QualityLevel(Settings) + 1) % 3);
			Settings->SetResolutionScaleNormalized(Resolution);
		}
		break;
	case 1:
		if (Settings)
		{
			const int32 Next = ResolutionPercent(Settings) + ResolutionStep;
			Settings->SetResolutionScaleNormalized((Next > 100 ? MinResolution : Next) / 100.f);
		}
		break;
	case 2:
		if (Settings)
		{
			const bool bToWindow = Settings->GetFullscreenMode() != EWindowMode::Windowed;
			Settings->SetFullscreenMode(bToWindow ? EWindowMode::Windowed : EWindowMode::WindowedFullscreen);
			Settings->SetScreenResolution(bToWindow ? WindowSize : Settings->GetDesktopResolution());
		}
		break;
	case 3:
		MusicPercent = MusicPercent + VolumeStep > 100 ? 0 : MusicPercent + VolumeStep;
		SaveVolumes();
		return;
	default:
		SoundPercent = SoundPercent + VolumeStep > 100 ? 0 : SoundPercent + VolumeStep;
		SaveVolumes();
		return;
	}

	if (Settings)
	{
		Settings->ApplySettings(false);
	}
}

#undef LOCTEXT_NAMESPACE
