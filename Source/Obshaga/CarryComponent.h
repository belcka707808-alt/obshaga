#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CarryComponent.generated.h"

class AItemActor;
class AObshagaCharacter;

/** «Что у меня в руках». Висит на персонаже; нести можно один предмет. */
UCLASS(ClassGroup = (Obshaga), meta = (BlueprintSpawnableComponent))
class OBSHAGA_API UCarryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCarryComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	AItemActor* GetCarriedItem() const { return CarriedItem; }
	bool IsCarrying() const { return CarriedItem != nullptr; }
	bool IsCarryingHeavy() const;

	// Вызываются с клиента по нажатию кнопки.
	void TryDrop();
	void TryThrow();

	// Только сервер.
	bool PickUp(AItemActor* Item);
	void Drop();
	void Throw();
	/** Отдаёт предмет тайнику: руки пустеют, само состояние предмета меняет тайник. */
	AItemActor* ReleaseForHiding();

protected:
	UFUNCTION(Server, Reliable)
	void ServerDrop();

	UFUNCTION(Server, Reliable)
	void ServerThrow();

	UFUNCTION()
	void OnRep_CarriedItem();

	UPROPERTY(ReplicatedUsing = OnRep_CarriedItem)
	TObjectPtr<AItemActor> CarriedItem;

private:
	AObshagaCharacter* GetCharacter() const;
	void SetCarriedItem(AItemActor* NewItem);
	/** Точка перед персонажем, где предмет не окажется внутри стены. */
	FVector FindReleaseLocation(const FVector& Direction, float Distance, float Height) const;
};
