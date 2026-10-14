#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ObshagaStoryConfig.generated.h"

/** Жилец: имя с прозвищем и строка характера. Порядок — как у моделей жильцов в DA_Looks. */
USTRUCT(BlueprintType)
struct FResidentInfo
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Story")
	FString Name;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Story", meta = (MultiLine = true))
	FText Trait;
};

/** Реплики коменданта для одной ситуации в одной манере речи; в игре берётся случайная. */
USTRUCT(BlueprintType)
struct FKomendantLineSet
{
	GENERATED_BODY()

	/** Подпись для того, кто правит тексты: какая это ситуация и манера. На игру не влияет. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Story")
	FString Label;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Story")
	TArray<FText> Lines;
};

/** Завязка вечера: заголовок и пара строк. */
USTRUCT(BlueprintType)
struct FStoryInfo
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Story")
	FText Title;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Story", meta = (MultiLine = true))
	FText Text;
};

/** Одна ночь «Недели в общаге №7»: название, завязка, событие ночи и манера Палыча. */
USTRUCT(BlueprintType)
struct FWeekNightInfo
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Story")
	FText Title;

	/** Завязка в две строки (строки разделяются переводом строки). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Story", meta = (MultiLine = true))
	FText Text;

	/** Событие ночи: -1 — случайное, 0 — без события, 1 — пробки, 2 — пожарная тревога, 3 — деканат, 4 — день рождения, 5 — труба. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Story", meta = (ClampMin = "-1", ClampMax = "5"))
	int32 NightEvent = -1;

	/** Манера Палыча: -1 — случайная, 0 — строгий, 1 — рассеянный, 2 — мстительный. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Story", meta = (ClampMin = "-1", ClampMax = "2"))
	int32 KomendantStyle = -1;
};

/**
 * Тексты «сюжета» в ассете DA_Story: жильцы, реплики коменданта, завязки вечера. Править можно в редакторе.
 * Если ассета нет или какой-то список в нём пуст, игра берёт тексты, зашитые в код (ObshagaStory.cpp).
 */
UCLASS()
class OBSHAGA_API UObshagaStoryConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Тексты проекта (DA_Story) или nullptr, если ассета нет. */
	static const UObshagaStoryConfig* Get();

	/** Заполнить ассет текстами из кода (кнопка в редакторе; затирает ручные правки). */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Story")
	void FillFromDefaults();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Story")
	TArray<FResidentInfo> Residents;

	/** По одному набору на пару «ситуация, манера»: сначала все манеры первой ситуации, потом второй и так далее. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Story")
	TArray<FKomendantLineSet> KomendantLines;

	/** По две завязки на манеру коменданта: строгий, рассеянный, мстительный. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Story")
	TArray<FStoryInfo> Stories;

	/**
	 * Что говорят жильцы-боты, по ситуациям (по порядку EBotLine: увидел Палыча, поймали соседа, хвастается ТВ,
	 * разбудила пожарная тревога, подтверждает алиби). Подпись набора — для того, кто правит тексты.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Story")
	TArray<FKomendantLineSet> BotLines;

	/** Пять ночей недели, с понедельника по пятницу. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Story")
	TArray<FWeekNightInfo> WeekNights;

	/**
	 * Титулы недели в пятничной газете: «легенда» (больше всех очков), «постоянный клиент» (больше всех страйков),
	 * «тень» (ни разу не пойман). {0} — имя (у «тени» — список имён), {1} — число очков или страйков.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Story")
	TArray<FText> WeekTitles;
};
