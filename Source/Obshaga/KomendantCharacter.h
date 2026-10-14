#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "ObshagaStory.h"
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
	/** Случайная личность; Style >= 0 — только из личностей этой манеры (0 — строгий, 1 — рассеянный, 2 — мстительный), если такая есть. */
	const UKomendantPersonality* PickPersonality(int32 Style = -1) const;

	/** Насторожен ли комендант. Видно всем: по этому игроки понимают, что пора бежать. */
	EKomendantAlert GetAlert() const { return Alert; }
	void SetAlert(EKomendantAlert NewAlert);

	/** Сервер: комендант произносит реплику. Обычные реплики идут не чаще раза в несколько секунд, важные (bForce) — всегда. */
	void Say(EKomendantLine Situation, int32 Style, bool bForce = false);
	/** Что комендант сказал последним и когда (по часам этой машины). Для подписи над головой. */
	const FText& GetSpokenLine() const { return SpokenLine; }
	float GetSpokenTime() const { return SpokenTime; }

protected:
	/** Личности, из которых выбирается одна на раунд. Назначаются в BP_Komendant. */
	UPROPERTY(EditDefaultsOnly, Category = "Komendant")
	TArray<TObjectPtr<UKomendantPersonality>> Personalities;

	/** Реплика — мимолётная вещь: кто не получил, ничего не потерял. Шлём номера, текст каждая машина берёт у себя. */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastSay(uint8 Situation, uint8 Style, uint8 Variant);

	FText SpokenLine;
	float SpokenTime = -100.f;
	float LastSayServerTime = -100.f;

	UPROPERTY(Replicated)
	EKomendantAlert Alert = EKomendantAlert::Calm;

	UPROPERTY(VisibleAnywhere, Category = "Komendant")
	TObjectPtr<class UObshagaLookComponent> LookComponent;
};
