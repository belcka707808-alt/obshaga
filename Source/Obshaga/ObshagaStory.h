#pragma once

#include "CoreMinimal.h"

/** Когда комендант что-то говорит. */
enum class EKomendantLine : uint8
{
	Patrol,
	Heard,
	Bluff,
	Chase,
	Lost,
	Caught,
	LieOk,
	LieFail,
	Confess,
	Silent,
	/** Утренняя проверка комнат. */
	Inspect,
	/** Осмотр тайника вечером или ночью (случайный или по стуку). */
	Search,
	Count
};

/**
 * Тексты «сюжета»: кто живёт в общаге, что говорит комендант, чем особенен сегодняшний вечер и что напишет
 * стенгазета. Только слова — на правила игры они не влияют. Лежат в коде, как и шаблоны хроники.
 */
namespace ObshagaStory
{
	/** Сколько жильцов с именами (по числу моделей). */
	constexpr int32 NumResidents = 10;
	/** Имя с прозвищем для модели жильца: «Миша „Чайник“». */
	OBSHAGA_API FString ResidentName(int32 LookSlot);
	/** Одна строка характера жильца. */
	OBSHAGA_API FText ResidentTrait(int32 LookSlot);

	/** Сколько вариантов реплики есть для ситуации. */
	constexpr int32 LinesPerSituation = 3;
	/** Реплика коменданта. Style: 0 — строгий, 1 — рассеянный, 2 — мстительный. */
	OBSHAGA_API FText KomendantLine(EKomendantLine Situation, int32 Style, int32 Variant);
	/** Стиль речи по имени ассета личности коменданта. */
	OBSHAGA_API int32 StyleFromPersonalityName(const FString& AssetName);

	/** Сколько завязок вечера на каждый стиль коменданта. */
	constexpr int32 StoriesPerStyle = 2;
	constexpr int32 NumStories = 3 * StoriesPerStyle;
	/** Завязка вечера: заголовок и пара строк. Index — от 0 до NumStories - 1 (Style * StoriesPerStyle + вариант). */
	OBSHAGA_API FText StoryTitle(int32 Index);
	OBSHAGA_API FText StoryText(int32 Index);

	/** Заголовок стенгазеты по итогам раунда. */
	OBSHAGA_API FText Headline(const FString& EvictedName, const FString& CaughtName, int32 TotalStrikes);
	/** Подзаголовок: кто победил. */
	OBSHAGA_API FText Subhead(const FString& WinnerName, int32 WinnerScore);
}
