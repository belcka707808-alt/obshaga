#pragma once

#include "CoreMinimal.h"

/**
 * Настройки игрока: графика, окно и громкость. Хранятся на его компьютере (GameUserSettings.ini) и на игру
 * других не влияют. Экран настроек открывается из главного меню и по Esc в игре; выбор — цифрами.
 */
namespace ObshagaSettings
{
	/** Сколько строк на экране настроек. */
	constexpr int32 NumOptions = 5;

	/** Строка экрана настроек: «[1] Качество графики: среднее». */
	OBSHAGA_API FText Describe(int32 Option);
	/** Переключает настройку на следующее значение, применяет и сохраняет. */
	OBSHAGA_API void Step(int32 Option);

	/** Выбрано ли низкое качество графики: тогда комнаты обходятся без окон, плакатов и мелких украшений. */
	OBSHAGA_API bool IsLowQuality();

	/** Громкость музыки и звуков по выбору игрока, 0–1. */
	OBSHAGA_API float MusicVolume();
	OBSHAGA_API float SoundVolume();
}
