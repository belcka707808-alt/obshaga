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

	/** Вызывается с клиента по нажатию кнопки: E — основное действие, F — дополнительное. */
	void TryInteract(bool bSecondary);

	/** Объект под прицелом у локального игрока (не реплицируется). */
	AActor* GetFocusedActor() const { return FocusedActor.Get(); }

	/** Тексты подсказок для локального игрока; пустые, если действия нет. */
	FText GetFocusedPrompt() const;
	FText GetFocusedSecondaryPrompt() const;

	/** Достаточно ли близко персонаж к объекту. Slack — допуск на лаг для серверных проверок. */
	bool IsInRange(const AActor* Target, float Slack) const;

	/** Нет ли стены между глазами персонажа и объектом. */
	bool HasLineOfSight(const AActor* Target) const;

	/** Допуск по дистанции, с которым сервер перепроверяет клиента. */
	static constexpr float ServerRangeSlack = 75.f;

protected:
	UFUNCTION(Server, Reliable)
	void ServerInteract(AActor* Target, bool bSecondary);

private:
	AObshagaCharacter* GetCharacter() const;
	AActor* FindFocusedActor() const;

	TWeakObjectPtr<AActor> FocusedActor;
};
