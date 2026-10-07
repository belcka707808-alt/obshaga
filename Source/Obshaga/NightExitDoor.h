#pragma once

#include "CoreMinimal.h"
#include "HidingSpot.h"
#include "NightExitDoor.generated.h"

/**
 * Ночной выход на улицу. Устроен как укрытие: кто вышел — «спрятан» за дверью, пока не вернётся.
 * Открыт только ночью; предметы сюда не прячут. Комендант может проверить дверь и застать гуляку.
 */
UCLASS()
class OBSHAGA_API ANightExitDoor : public AHidingSpot
{
	GENERATED_BODY()

public:
	ANightExitDoor();

	//~ IInteractable
	virtual FText GetInteractionPrompt(const AObshagaCharacter* By) const override;
	virtual void Interact(AObshagaCharacter* By) override;
	virtual FText GetSecondaryPrompt(const AObshagaCharacter* By) const override;
	virtual void SecondaryInteract(AObshagaCharacter* By) override;

	/** Гуляющий стоит за дверью, со стороны улицы. */
	virtual FVector GetHiddenPlayerLocation(float HalfHeight) const override;

	/** Сервер: ночь кончилась — того, кто на улице, загоняют обратно. */
	void ForceReturn();

protected:
	virtual void EjectHiddenPlayer() override;

private:
	bool IsNight() const;

	float LeftTime = 0.f;
};
