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
	// Вид шума — в Tag стимула: по нему Палыч решает, засчитать ли «ложную тревогу» (только брошенное).
	if (Source)
	{
		UAISense_Hearing::ReportNoiseEvent(World, Location, Loudness, Source, 0.f, FName(GetNoiseKindName(Kind)));
	}

	// Клиентам «брошено» не отличаем от простого удара: звук тот же, а лишнего знать незачем.
	const ENoiseKind HeardKind = Kind == ENoiseKind::Thrown ? ENoiseKind::Impact : Kind;

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
			Controller->ClientHeardNoise(Location, Loudness, HeardKind);
		}
	}
}

const TCHAR* UNoiseStatics::GetNoiseKindName(ENoiseKind Kind)
{
	switch (Kind)
	{
	case ENoiseKind::Impact:
		return TEXT("Impact");
	case ENoiseKind::Door:
		return TEXT("Door");
	case ENoiseKind::Device:
		return TEXT("Device");
	case ENoiseKind::Thrown:
		return TEXT("Thrown");
	default:
		return TEXT("Other");
	}
}

ENoiseKind UNoiseStatics::GetNoiseKind(FName Tag)
{
	const ENoiseKind Known[] = { ENoiseKind::Impact, ENoiseKind::Door, ENoiseKind::Device, ENoiseKind::Thrown };
	for (const ENoiseKind Kind : Known)
	{
		if (Tag == FName(GetNoiseKindName(Kind)))
		{
			return Kind;
		}
	}
	return ENoiseKind::Other;
}
