#include "ObshagaStory.h"

#include "ObshagaStoryConfig.h"
#include "UObject/StrongObjectPtr.h"

#define LOCTEXT_NAMESPACE "ObshagaStory"

namespace
{
	struct FResident
	{
		const TCHAR* Name;
		FText Trait;
	};

	const FResident& Resident(int32 LookSlot)
	{
		static const FResident Residents[ObshagaStory::NumResidents] = {
			{ TEXT("Миша «Чайник»"), LOCTEXT("Trait0", "Кипятит воду в три часа ночи и искренне не понимает, чем все недовольны.") },
			{ TEXT("Лёха «Доширак»"), LOCTEXT("Trait1", "Питается только лапшой. Знает, у кого в общаге что лежит в холодильнике.") },
			{ TEXT("Тимур «Сессия»"), LOCTEXT("Trait2", "Учит всё в последнюю ночь. Сегодня как раз последняя.") },
			{ TEXT("Саня «Колонка»"), LOCTEXT("Trait3", "Считает, что тихая музыка — это не музыка.") },
			{ TEXT("Вика «Зачётка»"), LOCTEXT("Trait4", "Староста этажа. Официально ничего не видела.") },
			{ TEXT("Даша «Кактус»"), LOCTEXT("Trait5", "Разводит растения на подоконнике. Некоторые из них лучше не показывать коменданту.") },
			{ TEXT("Гоша «Роутер»"), LOCTEXT("Trait6", "Раздаёт вайфай всему этажу. Пароль меняет, когда обижается.") },
			{ TEXT("Катя «Будильник»"), LOCTEXT("Trait7", "Встаёт в шесть утра и будит всех. Не специально. Наверное.") },
			{ TEXT("Артём «Должник»"), LOCTEXT("Trait8", "Должен всем понемногу. Конверт с деньгами — больная тема.") },
			{ TEXT("Ника «Сова»"), LOCTEXT("Trait9", "Днём её никто не видел. Ночью её видят все, кроме коменданта.") },
		};
		return Residents[((LookSlot % ObshagaStory::NumResidents) + ObshagaStory::NumResidents) % ObshagaStory::NumResidents];
	}
}

FString ObshagaStory::DefaultResidentName(int32 LookSlot)
{
	return Resident(LookSlot).Name;
}

FText ObshagaStory::DefaultResidentTrait(int32 LookSlot)
{
	return Resident(LookSlot).Trait;
}

int32 ObshagaStory::StyleFromPersonalityName(const FString& AssetName)
{
	if (AssetName.Contains(TEXT("Strict")))
	{
		return 0;
	}
	return AssetName.Contains(TEXT("Vindictive")) ? 2 : 1;
}

FText ObshagaStory::DefaultKomendantLine(EKomendantLine Situation, int32 Style, int32 Variant)
{
	// [ситуация][стиль: строгий, рассеянный, мстительный][вариант]
	static const FText Lines[static_cast<int32>(EKomendantLine::Count)][3][LinesPerSituation] = {
		// Patrol — обход
		{
			{ LOCTEXT("P00", "Отбой никто не отменял."), LOCTEXT("P01", "Тридцать лет на вахте. Меня не проведёшь."), LOCTEXT("P02", "Порядок должен быть. И он будет.") },
			{ LOCTEXT("P10", "Так… а зачем я сюда шёл?"), LOCTEXT("P11", "Где-то тут были мои очки."), LOCTEXT("P12", "Чай остыл. Опять.") },
			{ LOCTEXT("P20", "Я всё помню. Всех помню."), LOCTEXT("P21", "В прошлый раз было так же тихо. И чем кончилось?"), LOCTEXT("P22", "Списочек-то у меня при себе.") },
		},
		// Heard — услышал шум
		{
			{ LOCTEXT("H00", "Это что за грохот?!"), LOCTEXT("H01", "Кто шумит после отбоя?"), LOCTEXT("H02", "Так. Иду разбираться.") },
			{ LOCTEXT("H10", "А? Что? Где упало?"), LOCTEXT("H11", "Кошка, что ли… У нас нет кошки."), LOCTEXT("H12", "Вроде стукнуло. Или показалось?") },
			{ LOCTEXT("H20", "Ага! Попались, голубчики."), LOCTEXT("H21", "Шумим? Ну-ну. Запишем."), LOCTEXT("H22", "Я этот звук знаю. Это звук выселения.") },
		},
		// Bluff — делает вид, что что-то заметил
		{
			{ LOCTEXT("B00", "Я всё вижу. Выходи по-хорошему."), LOCTEXT("B01", "Считаю до трёх. Раз…"), LOCTEXT("B02", "Думаешь, я не заметил?") },
			{ LOCTEXT("B10", "Тут кто-то есть? Нет? Ну ладно."), LOCTEXT("B11", "Стой… а, это вешалка."), LOCTEXT("B12", "Мне кажется, или тут пахнет жареным?") },
			{ LOCTEXT("B20", "Я знаю, что ты здесь. И знаю, что ты сделал."), LOCTEXT("B21", "Прячешься? Как в прошлый раз?"), LOCTEXT("B22", "Выходи. Хуже уже не будет. Будет.") },
		},
		// Chase — погоня
		{
			{ LOCTEXT("C00", "Стоять! Комната, фамилия!"), LOCTEXT("C01", "А ну стой! Это общежитие, а не стадион!"), LOCTEXT("C02", "Куда?! Назад!") },
			{ LOCTEXT("C10", "Эй! Подожди! У меня колено!"), LOCTEXT("C11", "Стой… как тебя… ну стой же!"), LOCTEXT("C12", "Не беги, я всё равно забыл, зачем гонюсь!") },
			{ LOCTEXT("C20", "От меня ещё никто не уходил!"), LOCTEXT("C21", "Беги, беги. Я знаю, где ты живёшь."), LOCTEXT("C22", "Опять ты! Я так и знал!") },
		},
		// Lost — упустил
		{
			{ LOCTEXT("L00", "Ушёл. Ничего, утром проверка."), LOCTEXT("L01", "Я запомнил эту спину."), LOCTEXT("L02", "Бегать вы умеете. Учились бы так.") },
			{ LOCTEXT("L10", "Куда он делся? И кто это был?"), LOCTEXT("L11", "Ладно. Пойду чай допью."), LOCTEXT("L12", "Фух. Больше не бегаю.") },
			{ LOCTEXT("L20", "Убежал. Тем хуже для него."), LOCTEXT("L21", "Запишу в должники."), LOCTEXT("L22", "Мы ещё встретимся. Общага маленькая.") },
		},
		// Caught — поймал, начинает допрос
		{
			{ LOCTEXT("K00", "Так. И что мы тут делаем?"), LOCTEXT("K01", "Объяснительную. Сейчас. Устно."), LOCTEXT("K02", "Попался. Рассказывай.") },
			{ LOCTEXT("K10", "Ты кто? А, жилец. А что у тебя в руках?"), LOCTEXT("K11", "Ой. А ты чего не спишь?"), LOCTEXT("K12", "Погоди, я тебя, кажется, ловлю.") },
			{ LOCTEXT("K20", "Ну вот и свиделись."), LOCTEXT("K21", "Я ждал этого момента."), LOCTEXT("K22", "Рассказывай. А я сравню с прошлым разом.") },
		},
		// LieOk — поверил вранью
		{
			{ LOCTEXT("O00", "Хм. Ладно. Но я слежу."), LOCTEXT("O01", "Звучит складно. Свободен."), LOCTEXT("O02", "На первый раз поверю.") },
			{ LOCTEXT("O10", "А, ну раз так… Иди, иди."), LOCTEXT("O11", "Логично. Наверное."), LOCTEXT("O12", "Точно, я же сам так делал. Иди.") },
			{ LOCTEXT("O20", "Поверю. Но запомню."), LOCTEXT("O21", "Красиво врёшь. Иди, пока я добрый."), LOCTEXT("O22", "Ладно. Один-ноль в твою пользу.") },
		},
		// LieFail — не поверил
		{
			{ LOCTEXT("F00", "Не верю. Замечание!"), LOCTEXT("F01", "Врать сначала научись."), LOCTEXT("F02", "Сказки будешь на экзамене рассказывать.") },
			{ LOCTEXT("F10", "Что-то не сходится… Точно не сходится!"), LOCTEXT("F11", "Я рассеянный, но не настолько."), LOCTEXT("F12", "Погоди, ты же это говорил вчера. Замечание.") },
			{ LOCTEXT("F20", "Вот это я и хотел услышать. Враньё."), LOCTEXT("F21", "Ха! Попался дважды."), LOCTEXT("F22", "Записываю: врал. Жирным шрифтом.") },
		},
		// Confess — сознался
		{
			{ LOCTEXT("S00", "Честность — это хорошо. Но замечание."), LOCTEXT("S01", "Признание смягчает. Немного."), LOCTEXT("S02", "Вот так бы сразу.") },
			{ LOCTEXT("S10", "Сам сознался? Надо же. Запишу… где ручка?"), LOCTEXT("S11", "Молодец. А в чём сознался-то?"), LOCTEXT("S12", "Ну раз сам — тогда ползамечания. Шучу.") },
			{ LOCTEXT("S20", "Сознался. Жаль, я только разогнался."), LOCTEXT("S21", "Скучно. Но в список — попал."), LOCTEXT("S22", "Чистосердечное. В третий раз.") },
		},
		// Silent — промолчал
		{
			{ LOCTEXT("M00", "Молчим? Молчание — знак согласия."), LOCTEXT("M01", "Не хочешь говорить — напишешь."), LOCTEXT("M02", "Язык проглотил? Замечание.") },
			{ LOCTEXT("M10", "Алло? Ты меня слышишь? Ну ладно."), LOCTEXT("M11", "Молчит. Я тоже так умею. Нет, не умею."), LOCTEXT("M12", "Стесняется. Замечание за стеснение.") },
			{ LOCTEXT("M20", "Молчишь? Я подожду. У меня вся ночь."), LOCTEXT("M21", "Партизан. Уважаю. Замечание."), LOCTEXT("M22", "Ничего, другие расскажут.") },
		},
		// Inspect — утренняя проверка
		{
			{ LOCTEXT("I00", "Утренняя проверка! Шкафы открыть!"), LOCTEXT("I01", "Посмотрим, что вы тут прячете."), LOCTEXT("I02", "Проверка комнат. Без стука.") },
			{ LOCTEXT("I10", "Проверка… какой комнаты? Ладно, всех."), LOCTEXT("I11", "Доброе утро. Я по делу. По какому — вспомню."), LOCTEXT("I12", "Так, тумбочки. Или шкафы. Начну с тумбочек.") },
			{ LOCTEXT("I20", "Моё любимое время суток."), LOCTEXT("I21", "Ну-с, с кого начнём? Я знаю, с кого."), LOCTEXT("I22", "Что найду — то моё. В смысле, конфискую.") },
		},
		// Search — осмотр тайника не утром
		{
			{ LOCTEXT("Q00", "А что у нас в шкафу?"), LOCTEXT("Q01", "Загляну-ка. Для порядка."), LOCTEXT("Q02", "Внеплановый осмотр. Имею право.") },
			{ LOCTEXT("Q10", "Я сюда уже смотрел? Посмотрю ещё."), LOCTEXT("Q11", "Тут что-то лежало. Или не тут."), LOCTEXT("Q12", "Так, тумбочка… а зачем она мне?") },
			{ LOCTEXT("Q20", "Сдаётся мне, тут кое-что припрятано."), LOCTEXT("Q21", "Мне шепнули, где искать."), LOCTEXT("Q22", "Проверю. У меня хорошая память на тайники.") },
		},
	};

	const int32 SituationIndex = FMath::Clamp(static_cast<int32>(Situation), 0, static_cast<int32>(EKomendantLine::Count) - 1);
	return Lines[SituationIndex][FMath::Clamp(Style, 0, 2)][FMath::Clamp(Variant, 0, LinesPerSituation - 1)];
}

FText ObshagaStory::DefaultStoryTitle(int32 Index)
{
	static const FText Titles[NumStories] = {
		LOCTEXT("T0", "Комиссия из деканата"), LOCTEXT("T1", "Палыч не в духе"),
		LOCTEXT("T2", "Финал сериала"), LOCTEXT("T3", "Вторая смена подряд"),
		LOCTEXT("T4", "Кто-то разбил его кружку"), LOCTEXT("T5", "Он всё помнит"),
	};
	return Titles[FMath::Clamp(Index, 0, NumStories - 1)];
}

FText ObshagaStory::DefaultStoryText(int32 Index)
{
	static const FText Texts[NumStories] = {
		LOCTEXT("X0", "Завтра в общагу едет проверка. Комендант Палыч обходит этажи вдвое внимательнее: за каждую найденную мелочь влетит ему самому."),
		LOCTEXT("X1", "С утра у Палыча сломался телевизор на вахте. Теперь его единственное развлечение — вы."),
		LOCTEXT("X2", "Сегодня последняя серия, и Палыч то и дело возвращается к экрану. Слышит он по-прежнему хорошо, а вот смотрит вполглаза."),
		LOCTEXT("X3", "Сменщик заболел, Палыч на ногах вторые сутки. Он зевает, путает этажи и забывает, зачем пришёл. Но спать не идёт."),
		LOCTEXT("X4", "Любимая кружка Палыча найдена разбитой. Виновный не найден. Палыч уверен, что это кто-то из вас, и намерен выяснить, кто."),
		LOCTEXT("X5", "Палыч завёл тетрадку. В ней — все, кто попадался раньше. Сегодня он начнёт обход с них."),
	};
	return Texts[FMath::Clamp(Index, 0, NumStories - 1)];
}

FText ObshagaStory::Headline(const FString& EvictedName, const FString& CaughtName, int32 TotalStrikes)
{
	if (!EvictedName.IsEmpty())
	{
		return FText::Format(LOCTEXT("HeadEvicted", "{0} ВЫСЕЛЕН! ЧЕМОДАН, ВОКЗАЛ, РОДИТЕЛИ"), FText::FromString(EvictedName.ToUpper()));
	}
	if (TotalStrikes >= 2)
	{
		return FText::Format(LOCTEXT("HeadRaid", "ОБЛАВА В ОБЩАГЕ №7: ПАЛЫЧ ЛОВИЛ {0} РАЗА ЗА НОЧЬ"), FText::AsNumber(TotalStrikes));
	}
	if (TotalStrikes == 1 && !CaughtName.IsEmpty())
	{
		return FText::Format(LOCTEXT("HeadCaught", "{0} ПОПАЛСЯ. ОСТАЛЬНЫЕ ДЕЛАЮТ ВИД, ЧТО НЕ ЗНАКОМЫ"), FText::FromString(CaughtName.ToUpper()));
	}
	return LOCTEXT("HeadQuiet", "ТИХАЯ НОЧЬ. ПАЛЫЧ ЧТО-ТО ПОДОЗРЕВАЕТ, НО ДОКАЗАТЬ НЕ МОЖЕТ");
}

FText ObshagaStory::Subhead(const FString& WinnerName, int32 WinnerScore)
{
	return WinnerScore > 0 && !WinnerName.IsEmpty()
		? FText::Format(LOCTEXT("SubWinner", "Герой ночи — {0}: {1} очков и ни капли совести"), FText::FromString(WinnerName), FText::AsNumber(WinnerScore))
		: LOCTEXT("SubNobody", "Никто ничего не добился. Зато все выспались — кроме Палыча");
}

// --- Тексты из ассета DA_Story; если его нет или список пуст — зашитые в код выше ---

const UObshagaStoryConfig* UObshagaStoryConfig::Get()
{
	static const TCHAR* AssetPath = TEXT("/Game/Obshaga/Data/DA_Story.DA_Story");
	static TStrongObjectPtr<const UObshagaStoryConfig> Cached;
	static bool bTried = false;
	if (!bTried)
	{
		bTried = true;
		Cached.Reset(LoadObject<UObshagaStoryConfig>(nullptr, AssetPath, nullptr, LOAD_NoWarn));
	}
	return Cached.Get();
}

void UObshagaStoryConfig::FillFromDefaults()
{
	Residents.Reset();
	for (int32 Slot = 0; Slot < ObshagaStory::NumResidents; ++Slot)
	{
		Residents.Add({ ObshagaStory::DefaultResidentName(Slot), ObshagaStory::DefaultResidentTrait(Slot) });
	}

	static const TCHAR* Situations[] = { TEXT("Обход"), TEXT("Услышал шум"), TEXT("Блеф"), TEXT("Погоня"), TEXT("Упустил"), TEXT("Поймал"),
		TEXT("Поверил вранью"), TEXT("Не поверил"), TEXT("Сознался"), TEXT("Промолчал"), TEXT("Утренняя проверка"), TEXT("Осмотр тайника не утром") };
	static const TCHAR* Styles[] = { TEXT("строгий"), TEXT("рассеянный"), TEXT("мстительный") };
	KomendantLines.Reset();
	for (int32 Situation = 0; Situation < static_cast<int32>(EKomendantLine::Count); ++Situation)
	{
		for (int32 Style = 0; Style < 3; ++Style)
		{
			FKomendantLineSet& Set = KomendantLines.AddDefaulted_GetRef();
			Set.Label = FString::Printf(TEXT("%s — %s"), Situations[FMath::Min(Situation, static_cast<int32>(UE_ARRAY_COUNT(Situations)) - 1)], Styles[Style]);
			for (int32 Variant = 0; Variant < ObshagaStory::LinesPerSituation; ++Variant)
			{
				Set.Lines.Add(ObshagaStory::DefaultKomendantLine(static_cast<EKomendantLine>(Situation), Style, Variant));
			}
		}
	}

	Stories.Reset();
	for (int32 Index = 0; Index < ObshagaStory::NumStories; ++Index)
	{
		Stories.Add({ ObshagaStory::DefaultStoryTitle(Index), ObshagaStory::DefaultStoryText(Index) });
	}

	WeekNights.Reset();
	for (int32 Night = 0; Night < ObshagaStory::NumWeekNights; ++Night)
	{
		WeekNights.Add(ObshagaStory::DefaultWeekNight(Night));
	}

	static const TCHAR* BotSituations[] = { TEXT("Увидел Палыча"), TEXT("Поймали соседа"), TEXT("Хвастается телевизором"), TEXT("Разбудила пожарная тревога"), TEXT("Подтверждает алиби"), TEXT("Зовёт Палыча: уносят телевизор"),
		TEXT("Называет вора телевизора"), TEXT("Называет автора слухов"), TEXT("Стучит на чужую комнату"), TEXT("Уронил вещь, чтобы выманить Палыча") };
	static_assert(static_cast<int32>(UE_ARRAY_COUNT(BotSituations)) == static_cast<int32>(ObshagaStory::EBotLine::Count), "BotSituations needs a label for every EBotLine");
	BotLines.Reset();
	for (int32 Situation = 0; Situation < static_cast<int32>(ObshagaStory::EBotLine::Count); ++Situation)
	{
		FKomendantLineSet& Set = BotLines.AddDefaulted_GetRef();
		Set.Label = BotSituations[Situation];
		for (int32 Variant = 0; Variant < ObshagaStory::BotLinesPerSituation; ++Variant)
		{
			Set.Lines.Add(ObshagaStory::DefaultBotLine(static_cast<ObshagaStory::EBotLine>(Situation), Variant));
		}
	}

	WeekTitles.Reset();
	for (int32 Index = 0; Index < ObshagaStory::NumWeekTitles; ++Index)
	{
		WeekTitles.Add(ObshagaStory::DefaultWeekTitleFormat(Index));
	}
	MarkPackageDirty();
}

FString ObshagaStory::ResidentName(int32 LookSlot)
{
	const UObshagaStoryConfig* Config = UObshagaStoryConfig::Get();
	if (Config && !Config->Residents.IsEmpty())
	{
		const FString& Name = Config->Residents[FMath::Abs(LookSlot) % Config->Residents.Num()].Name;
		if (!Name.IsEmpty())
		{
			return Name;
		}
	}
	return DefaultResidentName(LookSlot);
}

FText ObshagaStory::ResidentTrait(int32 LookSlot)
{
	const UObshagaStoryConfig* Config = UObshagaStoryConfig::Get();
	return Config && !Config->Residents.IsEmpty() ? Config->Residents[FMath::Abs(LookSlot) % Config->Residents.Num()].Trait : DefaultResidentTrait(LookSlot);
}

FText ObshagaStory::KomendantLine(EKomendantLine Situation, int32 Style, int32 Variant)
{
	// Variant — любое число: из набора берётся строка с номером «Variant по модулю числа строк», так что
	// в ассете у ситуации может быть сколько угодно реплик.
	const UObshagaStoryConfig* Config = UObshagaStoryConfig::Get();
	const int32 SetIndex = static_cast<int32>(Situation) * 3 + FMath::Clamp(Style, 0, 2);
	if (Config && Config->KomendantLines.IsValidIndex(SetIndex) && !Config->KomendantLines[SetIndex].Lines.IsEmpty())
	{
		const TArray<FText>& Lines = Config->KomendantLines[SetIndex].Lines;
		return Lines[FMath::Abs(Variant) % Lines.Num()];
	}
	return DefaultKomendantLine(Situation, Style, FMath::Abs(Variant) % LinesPerSituation);
}

FText ObshagaStory::StoryTitle(int32 Index)
{
	const UObshagaStoryConfig* Config = UObshagaStoryConfig::Get();
	return Config && Config->Stories.IsValidIndex(Index) ? Config->Stories[Index].Title : DefaultStoryTitle(Index);
}

FText ObshagaStory::StoryText(int32 Index)
{
	const UObshagaStoryConfig* Config = UObshagaStoryConfig::Get();
	return Config && Config->Stories.IsValidIndex(Index) ? Config->Stories[Index].Text : DefaultStoryText(Index);
}

FText ObshagaStory::WeekdayName(int32 Night)
{
	static const FText Names[NumWeekNights] = { LOCTEXT("Mon", "Понедельник"), LOCTEXT("Tue", "Вторник"), LOCTEXT("Wed", "Среда"), LOCTEXT("Thu", "Четверг"), LOCTEXT("Fri", "Пятница") };
	return Names[FMath::Clamp(Night, 0, NumWeekNights - 1)];
}

FText ObshagaStory::WeekdayShort(int32 Night)
{
	static const FText Names[NumWeekNights] = { LOCTEXT("MonShort", "Пн"), LOCTEXT("TueShort", "Вт"), LOCTEXT("WedShort", "Ср"), LOCTEXT("ThuShort", "Чт"), LOCTEXT("FriShort", "Пт") };
	return Names[FMath::Clamp(Night, 0, NumWeekNights - 1)];
}

FWeekNightInfo ObshagaStory::DefaultWeekNight(int32 Night)
{
	FWeekNightInfo Info;
	switch (FMath::Clamp(Night, 0, NumWeekNights - 1))
	{
	case 0:
		Info.Title = LOCTEXT("WeekTitle0", "Заселение");
		Info.Text = LOCTEXT("WeekText0", "Новая неделя, новые соседи. Палыч сегодня добрый и рассеянный.\nСамое время осмотреться и завести полезные знакомства.");
		Info.NightEvent = 0;
		Info.KomendantStyle = 1;
		break;
	case 1:
		Info.Title = LOCTEXT("WeekTitle1", "Пробки");
		Info.Text = LOCTEXT("WeekText1", "Электрик обещал прийти ещё в понедельник. Не пришёл.\nНочью выбьет пробки — в темноте Палыч видит только перед собой.");
		Info.NightEvent = 1;
		break;
	case 2:
		Info.Title = LOCTEXT("WeekTitle2", "День рождения в 203");
		Info.Text = LOCTEXT("WeekText2", "В 203-й празднуют, и об этом знает вся общага.\nНа шум оттуда Палыч не идёт, но обязательно заглянет поздравить.");
		Info.NightEvent = 4;
		break;
	case 3:
		Info.Title = LOCTEXT("WeekTitle3", "Учения");
		Info.Text = LOCTEXT("WeekText3", "Пожарная часть устраивает учения. Когда завоет сирена — все в коридор.\nКто останется в комнате, тот под подозрением.");
		Info.NightEvent = 2;
		break;
	default:
		Info.Title = LOCTEXT("WeekTitle4", "Деканат");
		Info.Text = LOCTEXT("WeekText4", "Утром приедет проверка из деканата. Палыч помнит всё, что было за неделю,\nи начнёт с того, кто попадался чаще всех.");
		Info.NightEvent = 3;
		Info.KomendantStyle = 2;
		break;
	}
	return Info;
}

FText ObshagaStory::DefaultBotLine(EBotLine Situation, int32 Variant)
{
	static const FText Lines[static_cast<int32>(EBotLine::Count)][BotLinesPerSituation] = {
		{ LOCTEXT("BotSpot0", "Шухер, Палыч!"), LOCTEXT("BotSpot1", "Палыч идёт, разбегаемся!"), LOCTEXT("BotSpot2", "Я тут просто стою…"), LOCTEXT("BotSpot3", "Атас!") },
		{ LOCTEXT("BotCaught0", "Ну всё, попался…"), LOCTEXT("BotCaught1", "Держись, бро!"), LOCTEXT("BotCaught2", "Я его не знаю, честно"), LOCTEXT("BotCaught3", "Вот это поворот") },
		{ LOCTEXT("BotTv0", "Смотрите, что у меня есть!"), LOCTEXT("BotTv1", "Сегодня у нас кино"), LOCTEXT("BotTv2", "Тяжёлый, зараза…"), LOCTEXT("BotTv3", "Это не то, что вы думаете") },
		{ LOCTEXT("BotFire0", "Три часа ночи, какая тревога?!"), LOCTEXT("BotFire1", "Дайте поспать…"), LOCTEXT("BotFire2", "Опять учения?"), LOCTEXT("BotFire3", "Я в тапках вообще-то") },
		{ LOCTEXT("BotAlibi0", "Он был со мной всю ночь!"), LOCTEXT("BotAlibi1", "Это не он, Палыч"), LOCTEXT("BotAlibi2", "Мы чай пили, честно"), LOCTEXT("BotAlibi3", "Я свидетель!") },
		{ LOCTEXT("BotCall0", "Палыч! Телевизор уносят!"), LOCTEXT("BotCall1", "Держи вора!"), LOCTEXT("BotCall2", "Э, положи где взял!"), LOCTEXT("BotCall3", "Палыч, сюда! Тут кража!") },
		{ LOCTEXT("BotAccuse0", "Палыч! Это он телевизор упёр!"), LOCTEXT("BotAccuse1", "Я всё видел, это он!"), LOCTEXT("BotAccuse2", "Вот он, наш киноман!"), LOCTEXT("BotAccuse3", "Палыч, проверьте вон того!") },
		{ LOCTEXT("BotAccuseRumor0", "Палыч, это она записки разбрасывает!"), LOCTEXT("BotAccuseRumor1", "Вот кто тут сплетни пишет!"), LOCTEXT("BotAccuseRumor2", "Я видел, кто подкинул записку!"), LOCTEXT("BotAccuseRumor3", "Автор слухов — вот, стоит!") },
		{ LOCTEXT("BotTipRoom0", "Палыч, загляни-ка сюда…"), LOCTEXT("BotTipRoom1", "А тут что-то интересное в шкафу…"), LOCTEXT("BotTipRoom2", "Я ничего не говорил, но проверьте тут"), LOCTEXT("BotTipRoom3", "Палыч! Тут нечисто!") },
		{ LOCTEXT("BotLure0", "Ой!"), LOCTEXT("BotLure1", "Упс…"), LOCTEXT("BotLure2", "Оно само!"), LOCTEXT("BotLure3", "*свистит*") },
	};
	return Lines[FMath::Clamp(static_cast<int32>(Situation), 0, static_cast<int32>(EBotLine::Count) - 1)][FMath::Clamp(Variant, 0, BotLinesPerSituation - 1)];
}

FText ObshagaStory::BotLine(EBotLine Situation, int32 Variant)
{
	const UObshagaStoryConfig* Config = UObshagaStoryConfig::Get();
	const int32 Index = static_cast<int32>(Situation);
	if (Config && Config->BotLines.IsValidIndex(Index) && !Config->BotLines[Index].Lines.IsEmpty())
	{
		const TArray<FText>& Lines = Config->BotLines[Index].Lines;
		return Lines[FMath::Abs(Variant) % Lines.Num()];
	}
	return DefaultBotLine(Situation, Variant);
}

FText ObshagaStory::DefaultWeekTitleFormat(int32 Index)
{
	static const FText Formats[NumWeekTitles] = {
		LOCTEXT("WeekLegend", "Легенда общаги №7 — {0}: {1} очков за неделю"),
		LOCTEXT("WeekRegular", "Постоянный клиент Палыча — {0}: попадался {1} раз(а) за неделю"),
		LOCTEXT("WeekShadow", "Тень — {0}: ни разу не попался за всю неделю") };
	return Formats[FMath::Clamp(Index, 0, NumWeekTitles - 1)];
}

FText ObshagaStory::WeekTitleFormat(int32 Index)
{
	const UObshagaStoryConfig* Config = UObshagaStoryConfig::Get();
	return Config && Config->WeekTitles.IsValidIndex(Index) && !Config->WeekTitles[Index].IsEmpty() ? Config->WeekTitles[Index] : DefaultWeekTitleFormat(Index);
}

FWeekNightInfo ObshagaStory::WeekNight(int32 Night)
{
	const UObshagaStoryConfig* Config = UObshagaStoryConfig::Get();
	const int32 Index = FMath::Clamp(Night, 0, NumWeekNights - 1);
	return Config && Config->WeekNights.IsValidIndex(Index) ? Config->WeekNights[Index] : DefaultWeekNight(Index);
}

#undef LOCTEXT_NAMESPACE