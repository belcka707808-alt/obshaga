#include "ObshagaAudioConfig.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

const UObshagaAudioConfig* UObshagaAudioConfig::Get()
{
	static const TCHAR* AssetPath = TEXT("/Game/Obshaga/Audio/DA_Audio.DA_Audio");
	static TWeakObjectPtr<const UObshagaAudioConfig> Cached;
	if (!Cached.IsValid())
	{
		Cached = LoadObject<UObshagaAudioConfig>(nullptr, AssetPath, nullptr, LOAD_NoWarn);
	}
	return Cached.IsValid() ? Cached.Get() : GetDefault<UObshagaAudioConfig>();
}

void UObshagaAudioConfig::PlayAt(const UObject* WorldContextObject, USoundBase* Sound, const FVector& Location, float Volume)
{
	// На выделенном сервере слушать некому.
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (Sound && Volume > 0.f && World && World->GetNetMode() != NM_DedicatedServer)
	{
		UGameplayStatics::PlaySoundAtLocation(WorldContextObject, Sound, Location, Volume);
	}
}

void UObshagaAudioConfig::Play2D(const UObject* WorldContextObject, USoundBase* Sound, float Volume)
{
	if (Sound && Volume > 0.f)
	{
		UGameplayStatics::PlaySound2D(WorldContextObject, Sound, Volume);
	}
}
