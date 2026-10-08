#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "KomendantCharacter.generated.h"

class UKomendantPersonality;

UENUM()
enum class EKomendantAlert : uint8
{
	Calm,
	Suspicious,
	Chasing
};

/** Тело коменданта. Думает за него AKomendantAIController, который существует только на сервере. */
UCLASS()
class OBSHAGA_API AKomendantCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AKomendantCharacter();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Случайная личность из списка; если список пуст — значения по умолчанию. */
	const UKomendantPersonality* PickPersonality() const;

	/** Насторожен ли комендант. Видно всем: по этому игроки понимают, что пора бежать. */
	EKomendantAlert GetAlert() const { return Alert; }
	void SetAlert(EKomendantAlert NewAlert);

protected:
	/** Личности, из которых выбирается одна на раунд. Назначаются в BP_Komendant. */
	UPROPERTY(EditDefaultsOnly, Category = "Komendant")
	TArray<TObjectPtr<UKomendantPersonality>> Personalities;

	UPROPERTY(Replicated)
	EKomendantAlert Alert = EKomendantAlert::Calm;

	UPROPERTY(VisibleAnywhere, Category = "Komendant")
	TObjectPtr<class UObshagaLookComponent> LookComponent;
};
