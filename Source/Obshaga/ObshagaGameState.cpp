#include "ObshagaGameState.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

AObshagaGameState::AObshagaGameState()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AObshagaGameState::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Ночью в общаге гаснет свет: солнце и небо плавно тускнеют, утром возвращаются.
	const bool bNight = RoundState == ERoundState::InProgress && Phase == ERoundPhase::Night;
	const float Target = bNight ? 1.f : 0.f;
	if (!FMath::IsNearlyEqual(Darkness, Target))
	{
		Darkness = FMath::FInterpConstantTo(Darkness, Target, DeltaSeconds, 1.f / FMath::Max(NightFadeSeconds, 0.1f));
		ApplyDarkness();
	}

	// Событие ночи меняет свет (выбило пробки) — включаем и выключаем его в нужный момент.
	const bool bEventLight = IsNightEventActive() && !FMath::IsNearlyEqual(NightEventSkyScale, 1.f);
	if (bEventLight != bEventLightApplied)
	{
		bEventLightApplied = bEventLight;
		ApplyDarkness();
	}
}

void AObshagaGameState::ApplyDarkness()
{
	if (!bLightsFound)
	{
		bLightsFound = true;
		for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
		{
			Sun = Cast<UDirectionalLightComponent>(It->GetLightComponent());
			break;
		}
		for (TActorIterator<ASkyLight> It(GetWorld()); It; ++It)
		{
			Sky = It->GetLightComponent();
			break;
		}
		if (Sun.IsValid())
		{
			SunBaseIntensity = Sun->Intensity;
			SunBaseColor = Sun->GetLightColor();
		}
		if (Sky.IsValid())
		{
			SkyBaseIntensity = Sky->Intensity;
		}
		// Нарисованное дневное небо (сфера с облаками) от света не зависит — ночью его надо просто убрать.
		for (TActorIterator<AStaticMeshActor> It(GetWorld()); It; ++It)
		{
			const UStaticMesh* Mesh = It->GetStaticMeshComponent()->GetStaticMesh();
			if (Mesh && Mesh->GetName().Contains(TEXT("SkySphere")))
			{
				DaySkySphere = *It;
				break;
			}
		}
	}

	if (DaySkySphere.IsValid())
	{
		DaySkySphere->SetActorHiddenInGame(Darkness > 0.5f);
	}

	// Ночной свет — холодный, лунный.
	static const FLinearColor MoonColor(0.55f, 0.65f, 1.f);
	if (Sun.IsValid())
	{
		// Событие «пробки» гасит и лунный свет: иначе в комнатах с окнами темнее почти не становилось.
		Sun->SetIntensity(SunBaseIntensity * FMath::Lerp(1.f, NightSunScale, Darkness) * (bEventLightApplied ? NightEventSkyScale : 1.f));
		Sun->SetLightColor(FMath::Lerp(SunBaseColor, MoonColor, Darkness));
	}
	if (Sky.IsValid())
	{
		Sky->SetIntensity(SkyBaseIntensity * FMath::Lerp(1.f, NightSkyScale, Darkness) * (bEventLightApplied ? NightEventSkyScale : 1.f));
	}
}

void AObshagaGameState::SetNightLighting(float InSunScale, float InSkyScale, float InFadeSeconds)
{
	if (HasAuthority())
	{
		NightSunScale = InSunScale;
		NightSkyScale = InSkyScale;
		NightFadeSeconds = InFadeSeconds;
	}
}

void AObshagaGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AObshagaGameState, RoundState);
	DOREPLIFETIME(AObshagaGameState, Phase);
	DOREPLIFETIME(AObshagaGameState, PhaseEndServerTime);
	DOREPLIFETIME(AObshagaGameState, RevealedTasks);
	DOREPLIFETIME(AObshagaGameState, RevealedPlayers);
	DOREPLIFETIME(AObshagaGameState, Chronicle);
	DOREPLIFETIME(AObshagaGameState, StoryIndex);
	DOREPLIFETIME(AObshagaGameState, IntroEndServerTime);
	DOREPLIFETIME(AObshagaGameState, IntroSeconds);
	DOREPLIFETIME(AObshagaGameState, NightEvent);
	DOREPLIFETIME(AObshagaGameState, NightEventEndServerTime);
	DOREPLIFETIME(AObshagaGameState, NightEventSkyScale);
	DOREPLIFETIME(AObshagaGameState, FlashlightIntensity);
	DOREPLIFETIME(AObshagaGameState, FlashlightRadius);
	DOREPLIFETIME(AObshagaGameState, FlashlightCone);
	DOREPLIFETIME(AObshagaGameState, Headline);
	DOREPLIFETIME(AObshagaGameState, Subhead);
	DOREPLIFETIME(AObshagaGameState, PaperNotes);
	DOREPLIFETIME(AObshagaGameState, Interrogation);
	DOREPLIFETIME(AObshagaGameState, AlibiRadius);
	DOREPLIFETIME(AObshagaGameState, AccuseDistance);
	DOREPLIFETIME(AObshagaGameState, NightSunScale);
	DOREPLIFETIME(AObshagaGameState, NightSkyScale);
	DOREPLIFETIME(AObshagaGameState, NightFadeSeconds);
}

float AObshagaGameState::GetPhaseRemainingSeconds() const
{
	if (RoundState != ERoundState::InProgress)
	{
		return 0.f;
	}
	return FMath::Max(0.f, PhaseEndServerTime - static_cast<float>(GetServerWorldTimeSeconds()));
}

float AObshagaGameState::GetInterrogationRemainingSeconds() const
{
	return Interrogation.bActive ? FMath::Max(0.f, Interrogation.EndServerTime - static_cast<float>(GetServerWorldTimeSeconds())) : 0.f;
}

void AObshagaGameState::SetInterrogation(const FInterrogationInfo& NewInfo)
{
	if (HasAuthority())
	{
		Interrogation = NewInfo;
	}
}

void AObshagaGameState::StartRound(float InAlibiRadius, float InAccuseDistance)
{
	if (HasAuthority())
	{
		AlibiRadius = InAlibiRadius;
		AccuseDistance = InAccuseDistance;
		NightEvent = ENightEvent::None;
		NightEventEndServerTime = 0.f;
		NightEventSkyScale = 1.f;
		RevealedTasks.Reset();
		RevealedPlayers.Reset();
		Chronicle.Reset();
		RoundState = ERoundState::InProgress;
	}
}

void AObshagaGameState::SetPhase(ERoundPhase NewPhase, float DurationSeconds)
{
	if (HasAuthority())
	{
		Phase = NewPhase;
		PhaseEndServerTime = static_cast<float>(GetServerWorldTimeSeconds()) + DurationSeconds;
	}
}

bool AObshagaGameState::IsNightEventActive() const
{
	return NightEvent != ENightEvent::None && RoundState == ERoundState::InProgress && GetServerWorldTimeSeconds() < NightEventEndServerTime;
}

float AObshagaGameState::GetNightEventRemainingSeconds() const
{
	return FMath::Max(NightEventEndServerTime - GetServerWorldTimeSeconds(), 0.f);
}

void AObshagaGameState::StartNightEvent(ENightEvent Event, float DurationSeconds, float SkyScale)
{
	if (HasAuthority())
	{
		NightEvent = Event;
		NightEventEndServerTime = GetServerWorldTimeSeconds() + DurationSeconds;
		NightEventSkyScale = SkyScale;
	}
}

void AObshagaGameState::SetFlashlight(float Intensity, float Radius, float ConeDegrees)
{
	if (HasAuthority())
	{
		FlashlightIntensity = Intensity;
		FlashlightRadius = Radius;
		FlashlightCone = ConeDegrees;
	}
}

void AObshagaGameState::EndNightEvent()
{
	if (HasAuthority())
	{
		NightEventEndServerTime = FMath::Min(NightEventEndServerTime, GetServerWorldTimeSeconds());
	}
}

void AObshagaGameState::SetIntro(float Seconds)
{
	if (HasAuthority())
	{
		IntroSeconds = Seconds;
		IntroEndServerTime = Seconds > 0.f ? GetServerWorldTimeSeconds() + Seconds : 0.f;
	}
}

void AObshagaGameState::SetStory(int32 NewIndex)
{
	if (HasAuthority())
	{
		StoryIndex = static_cast<uint8>(FMath::Max(NewIndex, 0));
	}
}

void AObshagaGameState::SetNewspaper(const FText& InHeadline, const FText& InSubhead, const TArray<FText>& InNotes)
{
	if (HasAuthority())
	{
		Headline = InHeadline;
		Subhead = InSubhead;
		PaperNotes = InNotes;
	}
}

void AObshagaGameState::FinishRound(const TArray<FRevealedTask>& Tasks, const TArray<FRevealedPlayer>& Players, const TArray<FText>& InChronicle)
{
	if (HasAuthority())
	{
		RevealedTasks = Tasks;
		RevealedPlayers = Players;
		Chronicle = InChronicle;
		RoundState = ERoundState::Finished;
	}
}
