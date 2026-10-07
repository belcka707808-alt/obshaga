#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SuspicionComponent.generated.h"

/**
 * Подозрение коменданта к игроку (0–100) и страйки. Висит на PlayerState.
 * Меняет только сервер; по сети уходит только самому игроку.
 */
UCLASS(ClassGroup = (Obshaga))
class OBSHAGA_API USuspicionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USuspicionComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Точное значение; есть только на сервере. */
	float GetSuspicion() const { return Suspicion; }
	/** Округлённое значение для телефона владельца. */
	int32 GetSuspicionPercent() const { return SuspicionPercent; }
	int32 GetStrikes() const { return Strikes; }

	// Только сервер.
	void AddSuspicion(float Delta);
	void SetSuspicion(float NewValue);
	void AddStrike();

protected:
	UPROPERTY(Replicated)
	uint8 SuspicionPercent = 0;

	UPROPERTY(Replicated)
	uint8 Strikes = 0;

private:
	float Suspicion = 0.f;
};
