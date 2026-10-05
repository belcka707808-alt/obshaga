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

  На Windows ещё не проверено: в облаке нет Unreal Engine.

## В работе — M0, часть 2 (на Windows)
1. Запустить `setup.bat`. Он поставит недостающее, соберёт проект и откроет редактор.
2. Подключить временного персонажа: Content Drawer → **Add** → **Add Feature or Content Pack** → вкладка **Blueprint** → **Third Person** → **Add to Project**.
3. Создать карту: **File → New Level → Basic** → сохранить как `Content/Obshaga/Map/L_Obshaga`.
4. **Edit → Project Settings → Maps & Modes**: Editor Startup Map и Game Default Map = `L_Obshaga`, Default GameMode = GameMode из пака Third Person (`BP_ThirdPersonGameMode`).
5. Проверка: рядом с кнопкой Play → **Number of Players = 2**, **Net Mode = Play As Listen Server** → Play.
6. Коммит: карта и ассеты уйдут в Git LFS.

Шаги 2–5 может сделать Claude Code на вашем компьютере через unreal-mcp: откройте `claude` в папке проекта и напишите «Продолжаем M0».

✔ Готово, когда два окна PIE видят друг друга как стоящих персонажей.

## Дальше
- **M1 — персонаж и серый макет.** Перед кодом — план шагов и архитектура простыми словами, ждать «ок».

## Баги
- Пока нет.

## Решения
- **Сеть:** Listen Server (хост — один из игроков), как в ТЗ.
- **Движок:** последняя UE 5 из Epic Games Launcher. `setup.bat` сам прописывает установленную версию в `Obshaga.uproject`.
- **Графика:** Lumen и виртуальные тени выключены в `Config/DefaultEngine.ini` — цель 60 FPS на GTX 1660.
- **Плагины ModelContextProtocol и AllToolsets** включены как необязательные. Если в вашей версии UE их нет, проект всё равно откроется, просто без управления редактором из Claude.
- **Временный персонаж** — из пака Third Person. На M1 заменяем на C++ `AObshagaCharacter`.
- **Код модуля:** на M0 только пустой модуль `Obshaga`. Зависимости (`EnhancedInput`, `AIModule`, `GameplayTags`…) добавляем на тех этапах, где они нужны.
