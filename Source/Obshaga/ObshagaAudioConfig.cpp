#include "ObshagaAudioConfig.h"

#include "Obshaga.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	/** Затухание звуков мира: звук идёт из своей точки и стихает с расстоянием. Создаётся один раз по числам из ассета. */
	USoundAttenuation* GetWorldAttenuation(const UObshagaAudioConfig* Config)
	{
		static TStrongObjectPtr<USoundAttenuation> Attenuation;
		if (!Attenuation.IsValid())
		{
			Attenuation.Reset(NewObject<USoundAttenuation>(GetTransientPackage(), TEXT("ObshagaWorldAttenuation")));
		}
		FSoundAttenuationSettings& Settings = Attenuation->Attenuation;
		Settings.bAttenuate = true;
		Settings.bSpatialize = true;
		Settings.AttenuationShape = EAttenuationShape::Sphere;
		Settings.AttenuationShapeExtents = FVector(Config->WorldSoundFullVolumeRadius);
		Settings.FalloffDistance = FMath::Max(Config->WorldSoundRadius - Config->WorldSoundFullVolumeRadius, 1.f);
		return Attenuation.Get();
	}
}

const UObshagaAudioConfig* UObshagaAudioConfig::Get()
{
	// Ассет держим сильной ссылкой: иначе сборщик мусора выгружал бы его, и игра подвисала бы на повторной загрузке.
	// Загрузку пробуем один раз: если ассета нет, игра просто молчит.
	static const TCHAR* AssetPath = TEXT("/Game/Obshaga/Audio/DA_Audio.DA_Audio");
	static TStrongObjectPtr<const UObshagaAudioConfig> Cached;
	static bool bTried = false;
	if (!bTried)
	{
		bTried = true;
		Cached.Reset(LoadObject<UObshagaAudioConfig>(nullptr, AssetPath, nullptr, LOAD_NoWarn));
		if (!Cached.IsValid())
		{
			UE_LOG(LogObshaga, Warning, TEXT("Audio config %s not found: the game will be silent"), AssetPath);
		}
	}
	return Cached.IsValid() ? Cached.Get() : GetDefault<UObshagaAudioConfig>();
}

void UObshagaAudioConfig::PlayAt(const UObject* WorldContextObject, USoundBase* Sound, const FVector& Location, float Volume)
{
	// На выделенном сервере слушать некому.
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (Sound && Volume > 0.f && World && World->GetNetMode() != NM_DedicatedServer)
	{
		// Дальше радиуса слышимости звук не запускаем вовсе.
		const bool bAudible = UGameplayStatics::AreAnyListenersWithinRange(World, Location, Get()->WorldSoundRadius);
		UE_LOG(LogObshaga, VeryVerbose, TEXT("[%s] Sound %s at %s: %s"), *GetNameSafe(World), *Sound->GetName(), *Location.ToCompactString(),
			bAudible ? TEXT("audible") : TEXT("too far, skipped"));
		if (!bAudible)
		{
			return;
		}
		UGameplayStatics::PlaySoundAtLocation(WorldContextObject, Sound, Location, FRotator::ZeroRotator, Volume, 1.f, 0.f, GetWorldAttenuation(Get()));
	}
}

void UObshagaAudioConfig::Play2D(const UObject* WorldContextObject, USoundBase* Sound, float Volume)
{
	if (Sound && Volume > 0.f)
	{
		UGameplayStatics::PlaySound2D(WorldContextObject, Sound, Volume);
	}
}
