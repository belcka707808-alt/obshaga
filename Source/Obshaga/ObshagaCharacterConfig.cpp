#include "ObshagaCharacterConfig.h"

#define LOCTEXT_NAMESPACE "ObshagaCharacterConfig"

UObshagaCharacterConfig::UObshagaCharacterConfig()
{
	// Фразы по умолчанию; в ассете DA_CharacterConfig их можно заменить.
	Emotes = {
		LOCTEXT("Emote1", "Атас, комендант!"),
		LOCTEXT("Emote2", "Это не я!"),
		LOCTEXT("Emote3", "Тихо ты!"),
		LOCTEXT("Emote4", "За мной"),
		LOCTEXT("Emote5", "Прикрой меня"),
		LOCTEXT("Emote6", "Ты что там прячешь?"),
		LOCTEXT("Emote7", "Ха-ха-ха"),
		LOCTEXT("Emote8", "Я всё видел...")
	};
}

#undef LOCTEXT_NAMESPACE
