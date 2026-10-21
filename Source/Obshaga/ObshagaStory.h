#pragma once

#include "CoreMinimal.h"
#include "ObshagaStoryConfig.h"

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
 * стенгазета. Только слова — на правила игры они не влияют. Берутся из ассета DA_Story (UObshagaStoryConfig),
 * а если его нет — из кода. Заголовки стенгазеты и шаблоны хроники пока только в коде.
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

	// Те же тексты, зашитые в код: берутся, когда ассета DA_Story нет, и ими же ассет заполняется.
	OBSHAGA_API FString DefaultResidentName(int32 LookSlot);
	OBSHAGA_API FText DefaultResidentTrait(int32 LookSlot);
	OBSHAGA_API FText DefaultKomendantLine(EKomendantLine Situation, int32 Style, int32 Variant);
	OBSHAGA_API FText DefaultStoryTitle(int32 Index);
	OBSHAGA_API FText DefaultStoryText(int32 Index);

	/** Реплики жильцов-ботов. */
	enum class EBotLine : uint8
	{
		SpotKomendant,
		NeighbourCaught,
		BragTV,
		FireAlarm,
		Alibi,
		CallKomendant,
		/** Называет Палычу вора телевизора. */
		AccuseThief,
		/** Называет Палычу автора записок-слухов. */
		AccuseRumor,
		/** Стучит Палычу на чужую комнату. */
		TipRoom,
		/** Только что «нечаянно» уронил вещь, чтобы Палыч пошёл на шум. */
		Lure,
		Count
	};
	constexpr int32 BotLinesPerSituation = 4;
	OBSHAGA_API FText BotLine(EBotLine Situation, int32 Variant);
	OBSHAGA_API FText DefaultBotLine(EBotLine Situation, int32 Variant);

	/** «Неделя в общаге №7»: пять ночей, с понедельника (0) по пятницу (4). */
	constexpr int32 NumWeekNights = 5;
	/** «Понедельник»… и короткое «Пн»… */
	OBSHAGA_API FText WeekdayName(int32 Night);
	OBSHAGA_API FText WeekdayShort(int32 Night);
	/** Ночь недели из DA_Story (или из кода, если в ассете её нет). */
	OBSHAGA_API FWeekNightInfo WeekNight(int32 Night);
	OBSHAGA_API FWeekNightInfo DefaultWeekNight(int32 Night);
	/** Шаблон титула недели: 0 — легенда, 1 — постоянный клиент, 2 — тень. */
	constexpr int32 NumWeekTitles = 3;
	OBSHAGA_API FText WeekTitleFormat(int32 Index);
	OBSHAGA_API FText DefaultWeekTitleFormat(int32 Index);

	/** Заголовок стенгазеты по итогам раунда. */
	OBSHAGA_API FText Headline(const FString& EvictedName, const FString& CaughtName, int32 TotalStrikes);
	/** Подзаголовок: кто победил. */
	OBSHAGA_API FText Subhead(const FString& WinnerName, int32 WinnerScore);
}
