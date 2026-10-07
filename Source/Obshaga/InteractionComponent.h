#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InteractionComponent.generated.h"

class AObshagaCharacter;

/**
 * Висит на персонаже. На клиенте ищет, на что смотрит игрок (для подсказки),
 * по кнопке просит сервер выполнить взаимодействие. Решает только сервер.
 */
UCLASS(ClassGroup = (Obshaga), meta = (BlueprintSpawnableComponent))
class OBSHAGA_API UInteractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UInteractionComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Вызывается с клиента по нажатию кнопки взаимодействия. */
	void TryInteract();

	/** Объект под прицелом у локального игрока (не реплицируется). */
	AActor* GetFocusedActor() const { return FocusedActor.Get(); }

	/** Текст подсказки для локального игрока; пустой, если взаимодействовать не с чем. */
	FText GetFocusedPrompt() const;

protected:
	UFUNCTION(Server, Reliable)
	void ServerInteract(AActor* Target);

private:
	AObshagaCharacter* GetCharacter() const;
	AActor* FindFocusedActor() const;
	bool IsInRange(const AActor* Target, float Slack) const;

	TWeakObjectPtr<AActor> FocusedActor;
};
