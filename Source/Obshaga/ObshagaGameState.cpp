#include "ObshagaGameState.h"

#include "Net/UnrealNetwork.h"

void AObshagaGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AObshagaGameState, RoundState);
	DOREPLIFETIME(AObshagaGameState, Phase);
	DOREPLIFETIME(AObshagaGameState, PhaseEndServerTime);
	DOREPLIFETIME(AObshagaGameState, RevealedTasks);
	DOREPLIFETIME(AObshagaGameState, RevealedPlayers);
	DOREPLIFETIME(AObshagaGameState, Chronicle);
	DOREPLIFETIME(AObshagaGameState, Interrogation);
	DOREPLIFETIME(AObshagaGameState, AlibiRadius);
	DOREPLIFETIME(AObshagaGameState, AccuseDistance);
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
