#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Interactable.generated.h"

class AObshagaCharacter;

UINTERFACE(MinimalAPI)
class UInteractable : public UInterface
{
	GENERATED_BODY()
};

/** «Договор» для всего, с чем игрок взаимодействует кнопкой E: дверь, предмет, тайник. */
class OBSHAGA_API IInteractable
{
	GENERATED_BODY()

public:
	/** Можно ли сейчас взаимодействовать. Проверяется и на клиенте (подсказка), и на сервере (решение). */
	virtual bool CanInteract(const AObshagaCharacter* By) const { return true; }

	/** Текст подсказки, например «Открыть дверь». */
	virtual FText GetInteractionPrompt(const AObshagaCharacter* By) const = 0;

	/** Само действие. Вызывается только на сервере. */
	virtual void Interact(AObshagaCharacter* By) = 0;
};
