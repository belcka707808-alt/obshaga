# Соглашения по именам

## Ассеты (префикс + название в PascalCase)

| Префикс | Тип | Пример |
|---|---|---|
| `L_` | Карта (Level) | `L_Obshaga` |
| `BP_` | Blueprint-класс | `BP_Item_TV` |
| `WBP_` | Виджет UI | `WBP_Phone` |
| `DA_` | DataAsset | `DA_RoundConfig`, `DA_KomendantPersonality_Strict` |
| `DT_` | DataTable | `DT_Tasks` |
| `BT_` / `BB_` | Behavior Tree / Blackboard | `BT_Komendant`, `BB_Komendant` |
| `SM_` / `SK_` | Static Mesh / Skeletal Mesh | `SM_Wardrobe`, `SK_Student` |
| `M_` / `MI_` | Материал / его инстанс | `M_Greybox`, `MI_Greybox_Floor` |
| `T_` | Текстура | `T_Floor_D` |
| `ABP_` / `AM_` | Animation Blueprint / Montage | `ABP_Student`, `AM_Throw` |
| `S_` / `SC_` | Звук (Wave) / Sound Cue | `S_DoorCreak`, `SC_Footsteps` |
| `NS_` | Niagara-эффект | `NS_Dust` |
| `IA_` / `IMC_` | Input Action / Input Mapping Context | `IA_Interact`, `IMC_Default` |

Ассеты лежат в `Content/Obshaga/<Раздел>/`: `Characters`, `Items`, `Map`, `UI`, `Audio`, `Data`, `AI`.

## C++

- Префиксы Unreal: `A` — Actor, `U` — UObject и компоненты, `F` — структуры, `E` — enum, `I` — интерфейсы.
- Имена классов проекта начинаются с `Obshaga`, если класс — часть «каркаса» игры: `AObshagaGameMode`, `AObshagaGameState`, `AObshagaPlayerState`, `AObshagaCharacter`. Остальные — по смыслу: `AItemActor`, `UCarryComponent`, `AKomendantCharacter`.
- Серверные RPC — `Server` + действие: `ServerPickUp`, `ServerHide`. Обработчики репликации — `OnRep_` + поле: `OnRep_CarriedItem`.
- Один класс — пара файлов `.h`/`.cpp` с именем класса без префикса: `ItemActor.h`, `CarryComponent.h`.
- Баланс-числа (время, скорость, очки, радиусы) — в DataAsset или DataTable, не в коде.

## Git

- Коммиты маленькие: `feat: переноска предметов`, `fix: рассинхрон двери`, `docs: обновил PROGRESS`.
- `.uasset`, `.umap`, текстуры, звук и модели хранятся в Git LFS (см. `.gitattributes`).
