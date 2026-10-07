#include "ObshagaPlayerState.h"

#include "TaskComponent.h"
#include "Net/UnrealNetwork.h"

AObshagaPlayerState::AObshagaPlayerState()
{
	TaskComponent = CreateDefaultSubobject<UTaskComponent>(TEXT("TaskComponent"));
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
