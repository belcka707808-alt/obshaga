#include "ObshagaPlayerState.h"

#include "SuspicionComponent.h"
#include "TaskComponent.h"
#include "Net/UnrealNetwork.h"

AObshagaPlayerState::AObshagaPlayerState()
{
	TaskComponent = CreateDefaultSubobject<UTaskComponent>(TEXT("TaskComponent"));
	SuspicionComponent = CreateDefaultSubobject<USuspicionComponent>(TEXT("SuspicionComponent"));
}

void AObshagaPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AObshagaPlayerState, HomeRoomId);
}

void AObshagaPlayerState::SetHomeRoomId(FName NewRoomId)
{
	if (HasAuthority())
	{
		HomeRoomId = NewRoomId;
	}
}
