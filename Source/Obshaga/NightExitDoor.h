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

protected:
	virtual void EjectHiddenPlayer() override;

private:
	bool IsNight() const;

	float LeftTime = 0.f;
};
