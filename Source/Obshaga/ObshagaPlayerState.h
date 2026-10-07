#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "TaskTypes.h"
#include "ObshagaPlayerState.generated.h"

class USuspicionComponent;
class UTaskComponent;

/**
 * «Карточка игрока»: домашняя комната, очки (APlayerState::Score), роль, задания, подозрение, СМС.
 * Всё секретное уходит по сети только владельцу; настоящая роль Параноика не покидает сервер до итогов.
 */
UCLASS()
class OBSHAGA_API AObshagaPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	AObshagaPlayerState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UTaskComponent* GetTaskComponent() const { return TaskComponent; }
	USuspicionComponent* GetSuspicionComponent() const { return SuspicionComponent; }

	/** Комната, в которой игрок живёт (ARoomVolume::RoomId). Видна всем. */
	FName GetHomeRoomId() const { return HomeRoomId; }

	/** Роль, какой её видит сам игрок: Параноик видит «Жилец». */
	EPlayerRole GetVisibleRole() const { return VisibleRole; }
	/** Настоящая роль; есть только на сервере. */
	EPlayerRole GetTrueRole() const { return TrueRole; }

	/** Подсказка Крысе про чужое задание; у остальных пусто. */
	const FText& GetRatIntel() const { return RatIntel; }
	const TArray<FText>& GetSmsMessages() const { return SmsMessages; }
	bool IsEvicted() const { return bEvicted; }
	bool HasUsedTip() const { return bUsedTip; }
	/** Может ли игрок прямо сейчас назвать коменданту вора (есть такое задание, и он ещё не называл). */
	bool CanAccuse() const { return bCanAccuse; }
	/** Может ли настучать на комнату (есть такое задание, и он ещё не стучал). */
	bool CanTipRoom() const { return bCanTipRoom; }

	// Только сервер.
	void SetTaskAbilities(bool bNewCanAccuse, bool bNewCanTipRoom);
	void SetHomeRoomId(FName NewRoomId);
	void SetRole(EPlayerRole NewRole);
	void SetRatIntel(const FText& Intel, APlayerState* Target);
	/** Жертва Крысы; у Крысы есть и на её клиенте, у остальных пусто. */
	APlayerState* GetRatTarget() const { return RatTarget; }
	/** Бонус Крысы за удачный стук копится здесь и попадает в очки только на итогах: иначе скачок очков выдал бы её. */
	void AddPendingRatBonus(int32 Bonus) { PendingRatBonus += Bonus; }
	int32 TakePendingRatBonus();
	void AddSms(const FText& Message);
	void SetEvicted(bool bNewEvicted);
	void MarkTipUsed();
	/** Новый раунд: роль, СМС, задания, подозрение и страйки сбрасываются; очки остаются. */
	void ResetForRound();

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tasks")
	TObjectPtr<UTaskComponent> TaskComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Suspicion")
	TObjectPtr<USuspicionComponent> SuspicionComponent;

	UPROPERTY(Replicated)
	FName HomeRoomId;

	UPROPERTY(Replicated)
	EPlayerRole VisibleRole = EPlayerRole::Resident;

	UPROPERTY(Replicated)
	FText RatIntel;

	UPROPERTY(Replicated)
	TObjectPtr<APlayerState> RatTarget;

	UPROPERTY(Replicated)
	TArray<FText> SmsMessages;

	UPROPERTY(Replicated)
	bool bUsedTip = false;

	UPROPERTY(Replicated)
	bool bCanAccuse = false;

	UPROPERTY(Replicated)
	bool bCanTipRoom = false;

	/** Выселен: это видят все. */
	UPROPERTY(Replicated)
	bool bEvicted = false;

private:
	EPlayerRole TrueRole = EPlayerRole::Resident;
	int32 PendingRatBonus = 0;
};
