#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "ObshagaPlayerState.generated.h"

class UTaskComponent;

/** «Карточка игрока»: домашняя комната, очки (APlayerState::Score), секретные задания. Роль и страйки — на M4–M5. */
UCLASS()
class OBSHAGA_API AObshagaPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	AObshagaPlayerState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UTaskComponent* GetTaskComponent() const { return TaskComponent; }

	/** Комната, в которой игрок живёт (ARoomVolume::RoomId). Видна всем. */
	FName GetHomeRoomId() const { return HomeRoomId; }

	/** Только сервер. */
	void SetHomeRoomId(FName NewRoomId);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tasks")
	TObjectPtr<UTaskComponent> TaskComponent;

	UPROPERTY(Replicated)
	FName HomeRoomId;
};
