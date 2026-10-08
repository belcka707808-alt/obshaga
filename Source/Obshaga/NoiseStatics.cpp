#include "NoiseStatics.h"

#include "GameEventSubsystem.h"
#include "ItemActor.h"
#include "Obshaga.h"
#include "ObshagaCharacter.h"
#include "ObshagaCharacterConfig.h"
#include "ObshagaPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Perception/AISense_Hearing.h"

void UNoiseStatics::MakeGameNoise(const UObject* WorldContextObject, FVector Location, float Loudness, AActor* NoiseInstigator, ENoiseKind Kind)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (!World || World->GetNetMode() == NM_Client || Loudness <= 0.f)
	{
		return;
	}

	UE_LOG(LogObshaga, Verbose, TEXT("Noise %.2f at %s by %s"), Loudness, *Location.ToCompactString(), *GetNameSafe(NoiseInstigator));

	if (AObshagaCharacter* Culprit = Cast<AObshagaCharacter>(NoiseInstigator))
	{
		UGameEventSubsystem::PublishFrom(Culprit, EGameEventType::Noise);
	}

	// Комендант (M4) услышит это через AI Perception.
	// Источник обязателен для движка; если виновника нет, источником считается сам шумевший объект.
	AActor* Source = NoiseInstigator ? NoiseInstigator : const_cast<AActor*>(Cast<AActor>(WorldContextObject));
	if (Source)
	{
		UAISense_Hearing::ReportNoiseEvent(World, Location, Loudness, Source);
	}

	// Игрокам шлём шум адресно: кто далеко, тот о нём не узнаёт даже с читом.
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		AObshagaPlayerController* Controller = Cast<AObshagaPlayerController>(It->Get());
		const AObshagaCharacter* Listener = Controller ? Cast<AObshagaCharacter>(Controller->GetPawn()) : nullptr;
		if (!Listener)
		{
			continue;
		}

		const float HearingRange = Loudness * Listener->GetConfig()->NoiseHearingRange;
		if (FVector::DistSquared(Listener->GetActorLocation(), Location) <= FMath::Square(HearingRange))
		{
			Controller->ClientHeardNoise(Location, Loudness, Kind);
		}
	}
}
