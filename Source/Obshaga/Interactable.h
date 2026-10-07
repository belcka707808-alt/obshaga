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

/**
 * «Договор» для всего, с чем игрок взаимодействует: дверь, предмет, тайник.
 * Основное действие — кнопка E, дополнительное — F. Пустая подсказка значит «сейчас нельзя».
 * Подсказки считаются на клиенте, действия выполняются только на сервере и сами проверяют условия.
 */
class OBSHAGA_API IInteractable
{
	GENERATED_BODY()

public:
	/** Текст подсказки основного действия, например «Открыть дверь». */
	virtual FText GetInteractionPrompt(const AObshagaCharacter* By) const = 0;

	/** Основное действие. Вызывается только на сервере. */
	virtual void Interact(AObshagaCharacter* By) = 0;

	/** Текст подсказки дополнительного действия, например «Спрятаться». */
	virtual FText GetSecondaryPrompt(const AObshagaCharacter* By) const { return FText::GetEmpty(); }

	/** Дополнительное действие. Вызывается только на сервере. */
	virtual void SecondaryInteract(AObshagaCharacter* By) {}
};
