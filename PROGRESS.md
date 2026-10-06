# PROGRESS — ОБЩАГА

Журнал проекта. Claude читает его в начале каждой сессии вместе с `CLAUDE.md`.

## Сделано
- **2026-10-05 — M0, часть 1 (в облаке).** Каркас проекта:
  - `Obshaga.uproject`, C++-модуль `Obshaga` (`Source/`), конфиги (`Config/`);
  - структура `Content/Obshaga/{Characters,Items,Map,UI,Audio,Data,AI}`;
  - `.gitignore`, `.gitattributes` (Git LFS для ассетов, текстур, звука, моделей);
  - `setup.bat` — ставит Git, Claude Code, Visual Studio, Epic Launcher, собирает проект и открывает редактор;
  - Claude Code: плагин Unreal Engine Skills (`.claude/settings.json`), подключение к редактору (`.mcp.json`), скилы `replication-check` и `milestone-done`;
  - соглашения по именам — `docs/CONVENTIONS.md`.
- **2026-10-06 — M0, часть 2 (на Windows). M0 ГОТОВ.**
  - UE 5.8.3, проект собирается и открывается, unreal-mcp подключён;
  - пак Third Person добавлен (`Content/ThirdPerson`, `Content/Characters`, `Content/Input`, `Content/LevelPrototyping`);
  - карта `Content/Obshaga/Map/L_Obshaga`, она же Editor Startup Map и Game Default Map; GameMode — `BP_ThirdPersonGameMode`;
  - Claude проверил сам: PIE, 2 игрока, Listen Server → `Join succeeded`, оба окна видят друг друга; персонаж хоста пошёл вперёд — во втором окне он тоже ушёл; персонаж клиента пошёл вперёд — у хоста он тоже сдвинулся. Ошибок в логе нет;
  - повторная проверка с цифрами: позиции персонажей на сервере и на клиенте совпали до и после движения (хост `X 0 → 1224`, клиент `Y 0 → 831`, в обоих мирах одинаково). Позиции читаются через unreal-mcp `ActorTools.get_actor_transform` из миров `UEDPIE_0` (сервер) и `UEDPIE_1` (клиент);
  - `tools/pie-check.ps1` — скрипт для самопроверки (скриншот окна PIE, нажатие клавиш).

## В работе
- Ничего. Ждём «ок» на план M1.

## Дальше
- **M1 — персонаж и серый макет.** Перед кодом — план шагов и архитектура простыми словами, ждать «ок».

## Баги
- Пока нет.

## Решения
- **Сеть:** Listen Server (хост — один из игроков), как в ТЗ.
- **Движок:** UE 5.8 (`EngineAssociation` в `Obshaga.uproject`).
- **Графика:** Lumen и виртуальные тени выключены в `Config/DefaultEngine.ini` — цель 60 FPS на GTX 1660.
- **Плагины ModelContextProtocol и AllToolsets** включены как необязательные. Если в вашей версии UE их нет, проект всё равно откроется, просто без управления редактором из Claude.
- **Временный персонаж** — из пака Third Person. На M1 заменяем на C++ `AObshagaCharacter`.
- **Код модуля:** на M0 только пустой модуль `Obshaga`. Зависимости (`EnhancedInput`, `AIModule`, `GameplayTags`…) добавляем на тех этапах, где они нужны.
- **Самопроверка:** Claude сам запускает PIE через unreal-mcp (`StartPIE`, 2 игрока, Listen Server), двигает персонажей и смотрит скриншоты через `tools/pie-check.ps1`. Пользователя зовёт, только если что-то сломалось.
