#include "ObshagaGameState.h"

#include "Net/UnrealNetwork.h"

void AObshagaGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AObshagaGameState, RoundState);
	DOREPLIFETIME(AObshagaGameState, RoundEndServerTime);
	DOREPLIFETIME(AObshagaGameState, RevealedTasks);
	DOREPLIFETIME(AObshagaGameState, Interrogation);
	DOREPLIFETIME(AObshagaGameState, AlibiRadius);
}

float AObshagaGameState::GetRemainingSeconds() const
{
	if (RoundState != ERoundState::InProgress)
	{
		return 0.f;
	}
	return FMath::Max(0.f, RoundEndServerTime - static_cast<float>(GetServerWorldTimeSeconds()));
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

void AObshagaGameState::StartRound(float DurationSeconds, float InAlibiRadius)
{
	if (HasAuthority())
	{
		AlibiRadius = InAlibiRadius;
		RevealedTasks.Reset();
		RoundEndServerTime = static_cast<float>(GetServerWorldTimeSeconds()) + DurationSeconds;
		RoundState = ERoundState::InProgress;
	}
}

void AObshagaGameState::FinishRound(const TArray<FRevealedTask>& Reveal)
{
	if (HasAuthority())
	{
		RevealedTasks = Reveal;
		RoundState = ERoundState::Finished;
	}
}
