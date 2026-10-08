#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ObshagaAudioConfig.generated.h"

class USoundBase;

/** Что именно прозвучало: по этому клиент выбирает звук. */
UENUM()
enum class ENoiseKind : uint8
{
	/** Предмет упал или ударился. */
	Impact,
	/** Скрипнула дверь. */
	Door,
	/** Сломался прибор. */
	Device
};

/**
 * Все звуки игры в одном месте. Ассет лежит по постоянному адресу Content/Obshaga/Audio/DA_Audio;
 * пустая ячейка значит «этого звука пока нет» — игра просто промолчит.
 */
UCLASS(BlueprintType)
class OBSHAGA_API UObshagaAudioConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Ассет со звуками; если его нет — пустые значения по умолчанию. */
	static const UObshagaAudioConfig* Get();

	/** Играет звук в точке мира на этой машине. Пустой звук и нулевая громкость молча пропускаются. */
	static void PlayAt(const UObject* WorldContextObject, USoundBase* Sound, const FVector& Location, float Volume = 1.f);
	/** Играет звук «в голове» у локального игрока (интерфейс, пульс). */
	static void Play2D(const UObject* WorldContextObject, USoundBase* Sound, float Volume = 1.f);

	// --- Мир ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World")
	TObjectPtr<USoundBase> ItemImpactLight;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World")
	TObjectPtr<USoundBase> ItemImpactHeavy;

	/** С какой громкости шума удар считается тяжёлым. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World", meta = (ClampMin = "0", ClampMax = "1"))
	float HeavyImpactLoudness = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World")
	TObjectPtr<USoundBase> Door;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World")
	TObjectPtr<USoundBase> DeviceBreak;

	/** Шорох: кто-то прячет вещь в тайник или обыскивает его. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World")
	TObjectPtr<USoundBase> HidingSpotRustle;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World")
	TObjectPtr<USoundBase> Footstep;

	/** Через сколько сантиметров пути звучит шаг. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World", meta = (ClampMin = "10", Units = "cm"))
	float FootstepStride = 150.f;

	/** Громкость шага при ходьбе, беге и вприсядку. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World", meta = (ClampMin = "0"))
	float FootstepWalkVolume = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World", meta = (ClampMin = "0"))
	float FootstepRunVolume = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World", meta = (ClampMin = "0"))
	float FootstepCrouchVolume = 0.1f;

	/** Комендант начал погоню. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World")
	TObjectPtr<USoundBase> ChaseStart;

	// --- Игрок ---

	/** Один удар «тук-тук»; частоту задаёт чувство опасности. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player")
	TObjectPtr<USoundBase> Heartbeat;

	/** Новое сообщение на экране (СМС, задания). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player")
	TObjectPtr<USoundBase> Notice;

	/** Тебя поймали. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player")
	TObjectPtr<USoundBase> Caught;

	// --- Музыка (зацикленные дорожки) ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music")
	TObjectPtr<USoundBase> MusicCalm;

	/** Звучит тем громче, чем ближе комендант. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music")
	TObjectPtr<USoundBase> MusicTense;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music")
	TObjectPtr<USoundBase> MusicResults;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music", meta = (ClampMin = "0", ClampMax = "1"))
	float MusicVolume = 0.35f;

	/** Насколько спокойная музыка стихает на пике опасности (0 — не стихает, 1 — замолкает). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music", meta = (ClampMin = "0", ClampMax = "1"))
	float CalmDuckAtDanger = 0.8f;
};
