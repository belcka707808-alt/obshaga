#include "ObshagaDecorConfig.h"

#include "Obshaga.h"
#include "UObject/StrongObjectPtr.h"

const UObshagaDecorConfig* UObshagaDecorConfig::Get()
{
	// Как и остальные настройки: держим сильной ссылкой и пробуем загрузить один раз.
	static const TCHAR* AssetPath = TEXT("/Game/Obshaga/Map/Furniture/DA_Decor.DA_Decor");
	static TStrongObjectPtr<const UObshagaDecorConfig> Cached;
	static bool bTried = false;
	if (!bTried)
	{
		bTried = true;
		Cached.Reset(LoadObject<UObshagaDecorConfig>(nullptr, AssetPath, nullptr, LOAD_NoWarn));
		if (!Cached.IsValid())
		{
			UE_LOG(LogObshaga, Warning, TEXT("Decor config %s not found: rooms stay plain"), AssetPath);
		}
	}
	return Cached.IsValid() ? Cached.Get() : GetDefault<UObshagaDecorConfig>();
}

const FRoomStyle* UObshagaDecorConfig::FindStyle(ERoomType RoomType) const
{
	return Styles.FindByPredicate([RoomType](const FRoomStyle& Style) { return Style.RoomType == RoomType; });
}
