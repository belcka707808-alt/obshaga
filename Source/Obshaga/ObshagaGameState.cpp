#include "ObshagaGameState.h"

#include "Net/UnrealNetwork.h"

void AObshagaGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AObshagaGameState, RoundState);
	DOREPLIFETIME(AObshagaGameState, RoundEndServerTime);
	DOREPLIFETIME(AObshagaGameState, RevealedTasks);
}

float AObshagaGameState::GetRemainingSeconds() const
{
	if (RoundState != ERoundState::InProgress)
	{
		return 0.f;
	}
	return FMath::Max(0.f, RoundEndServerTime - static_cast<float>(GetServerWorldTimeSeconds()));
}

void AObshagaGameState::StartRound(float DurationSeconds)
{
	if (HasAuthority())
	{
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
