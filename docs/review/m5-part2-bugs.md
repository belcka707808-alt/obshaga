# Ошибки в коде M5, часть 2 (ревью коммита 0d6cae9)

Проверка устроена так же, как для части 1 (`m5-part1-bugs.md`): по ревьюеру на область (задания, сеть, комендант, надёжность, наблюдения из PIE-теста), каждую находку потом перепроверил скептик. Ниже только подтверждённое, без дублей. Номера строк даны по 0d6cae9.

Начни с части 1: исправления #1, #5 и #6 оттуда закрывают сразу несколько пунктов ниже.

---

## 0. [Серьёзно] Ошибка №1 из части 1 теперь случается в любой фазе

`InspectRoom` (стук на комнату по B) кладёт тайники в `InspectionQueue` вечером и ночью. Сброс очереди в `OnPhaseChanged` убран. Поэтому обыск сквозь стены по устаревшему `QueuedSpot` больше не ограничен утром. Так засчитываются tip_off, frame_roommate и проваливается hide_contraband.

Исправление то же, что в части 1, но с поправкой. Если за 4 секунды не дошёл до тайника, **выкидывай его** и переходи в `SetState(Patrol)`. Не возвращай его в очередь. Иначе `Pop()` тут же достанет его снова, и недостижимый тайник будет перебираться бесконечно.

---

## 1. [Средне] Все игроки появляются в одной комнате

**Где:** `ObshagaGameMode`. Нет переопределения `ChoosePlayerStart`.

**Что происходит.** Домашняя комната — это та, где заспавнился игрок (`AssignHomeRoom`), а спавн выбирается случайно из 8 точек. Вот с какой вероятностью кто-то окажется в одной комнате:

| Игроков | Шанс общей комнаты |
|---|---|
| 2 | 1 из 7 |
| 3 | ~43% |
| 4 | ~77% |

Соседи не могут подставить друг друга и стучать на свою комнату, поэтому frame_roommate и tip_off невыполнимы. При реванше `StartSpot` переиспользуется, так что пары сохраняются. В PIE с «Play from Here» все вообще спавнятся в одной точке.

**Как чинить:** каждому новому игроку выдавай наименее заселённую спальню.

```cpp
// .h
virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;

// .cpp  (#include "GameFramework/PlayerStart.h", "Engine/PlayerStartPIE.h")
AActor* AObshagaGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
    TSet<const AActor*> Taken; TMap<FName, int32> Residents;
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        const APlayerController* Other = It->Get();
        const AActor* Spot = (Other && Other != Player) ? Other->StartSpot.Get() : nullptr;
        if (!Spot) continue;
        Taken.Add(Spot);
        if (const ARoomVolume* R = ARoomVolume::FindRoomAt(this, Spot->GetActorLocation())) { ++Residents.FindOrAdd(R->RoomId); }
    }
    TArray<APlayerStart*> Best; int32 BestCount = MAX_int32;
    for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
    {
        if (It->IsA<APlayerStartPIE>() || Taken.Contains(*It)) continue;
        const ARoomVolume* R = ARoomVolume::FindRoomAt(this, It->GetActorLocation());
        if (!R || R->RoomType != ERoomType::Bedroom) continue;
        const int32 Count = Residents.FindRef(R->RoomId);
        if (Count < BestCount) { BestCount = Count; Best.Reset(); }
        if (Count == BestCount) { Best.Add(*It); }
    }
    return Best.Num() ? Best[FMath::RandRange(0, Best.Num() - 1)] : Super::ChoosePlayerStart_Implementation(Player);
}
```

Чтобы при реванше комнаты перетасовывались, ставь `Controller->StartSpot = nullptr` перед `RestartPlayer` в `ResetWorldForRematch`. Сверь имена `FindRoomAt`, `RoomType` и `ERoomType` с реальным кодом.

---

## 2. [Средне] «Незаметно» не учитывает, что комендант видел во время допроса

**Где:** `KomendantAIController.cpp`, `UpdateVision` (~387–409). `TaskComponent.cpp`, `WasSeenBetween` (~167).

**Что происходит.** sabotage_kitchen и curfew_breach считают игрока незамеченным, если в журнале нет `PlayerSpotted`. Но `UpdateVision` выходит сразу в состоянии Interrogate. Ещё он пропускает замороженных и тех, у кого иммунитет после допроса, и всё это до публикации `PlayerSpotted`. Пример: идёт допрос, длинный вариант «Соврать» тянется 8 секунд. В это время можно сломать плиту прямо на глазах у коменданта, и задание засчитается.

**Как чинить:** публикуй увиденное в самом начале `UpdateVision`, до раннего выхода для Interrogate. Проходи по всем `SeenPlayers` и делай `PublishFrom(Player, PlayerSpotted)` не чаще раза в 5 секунд на игрока. Проверки frozen и immune оставь только для роста подозрения и `StartChase`.

---

## 3. [Средне] «Найди вора»: вором считается любой, кто брал ТВ в гостиной

**Где:** `TaskComponent.cpp`, `HasStolen` (~62–76). `ObshagaGameMode.cpp`, `Accuse` (~501–511).

**Что происходит.** Защитник ТВ (protect_tv) прячет его в тумбу в той же гостиной. Это разумная защита. Но его запоминают как вора: R на нём даёт «Комендант поверил», +40 подозрения невиновному и выполненное investigate_theft.

**Как чинить:**
- Считай кражей подбор в `Row.RoomId`, только если потом этот же игрок уронил, бросил, спрятал предмет или был пойман с ним в другой комнате. Второй вариант: предмет сейчас у него в руках вне гостиной.
- Решай это один раз в `Accuse`. Клади украденный предмет в событие Accusation (или `nullptr`, если обвинение неверное).
- `CorrectAccusation` должен читать результат из события, а не пересчитывать.

---

## 4. [Средне] Контрабанду выселенного вешают на его соседа

**Где:** `KomendantAIController.cpp`, `SearchSpot` (~737–773).

**Что происходит.** `Residents` собирается без выселенных. Пусть A прятал запрещёнку в своей же комнате, а потом его выселили. Утром её находят и обвиняют соседа B. A получает «Подстава удалась» и очки за frame_roommate. Похожий случай: A подбросил запрещёнку в шкаф B и сам спрятался в том же шкафу. Комендант находит обоих, но всё равно обвиняет B.

**Как чинить:**

```cpp
const bool bHiderLivesHere = HiderState && HiderState->GetHomeRoomId() == RoomId;
const bool bCaughtInside  = HiderState && FoundPlayer && FoundPlayer->GetPlayerState() == HiderState;
if (Residents.IsEmpty() || bHiderLivesHere || bCaughtInside) { /* виноват прятавший, без PlayerFramed */ }
else { /* обвинить жильцов + PlayerFramed */ }
```

---

## 5. [Средне] Плиту можно сломать в лобби (усугубляет №5 из части 1)

**Где:** `DeviceActor.cpp`, `Interact` / `SecondaryInteract` (~118–132).

**Что происходит.** Плита, сломанная до старта, остаётся сломанной в первом раунде. Тогда sabotage_kitchen засчитывается даром. Или fix_kitchen становится невыполнимым: игрок сам её сломал, а его ремонт не считается.

**Как чинить:**
- Сделай исправление №5 из части 1: `ResetWorldForRematch()` в начале `StartRound`.
- Для надёжности в `Interact`, `SecondaryInteract` и подсказках плиты выходи, если раунд не InProgress. Ночной выход так уже делает.

---

## 6. [Мелко] frame_roommate засчитывается за любую вещь в своей же комнате

**Где:** `TaskComponent.cpp`, второй путь подставы (~236–252).

**Что происходит.** Второй путь не проверяет, что комната чужая. Он не проверяет и тип вещи: годится любая, хоть ключ соседа на его же полу.

**Как чинить:**
- Пропускай подбросы с `Plant.RoomId == OwnerState->GetHomeRoomId()`.
- Требуй контрабанду или `Row.ItemId`.
- В `TaskDirector::IsFeasible` для этого задания требуй другого игрока с другой домашней комнатой.

---

## 7. [Мелко] Стук, не отработанный до утра, утром проверяется последним

**Где:** `KomendantAIController.cpp`, `OnPhaseChanged` (~191–201).

**Что происходит.** Утром тайники спален добавляются в конец через `AddUnique`, а `Pop()` берёт с конца. Поэтому тайники застучанной комнаты разбираются последними, и до них можно вообще не дойти.

**Как чинить:** вместо `AddUnique` используй `if (!InspectionQueue.Contains(Spot)) InspectionQueue.Insert(Spot, 0);`.

---

## 8. [Мелко] Поздно зашедшим выдаются невыполнимые задания

**Где:** `ObshagaGameMode.cpp`, `HandleStartingNewPlayer` (~110–117). `TaskDirector.cpp` (~75–83).

**Что происходит.**
- `FTaskRow::Phase` нигде не читается. Зашедший утром может получить curfew_breach, а ночь уже прошла.
- Таймер самополомки плиты решается только в `StartRound`. Поэтому fix_kitchen у позднего игрока может остаться без поломки.

**Как чинить:**
- Передавай текущую фазу в `AssignTasksTo`. Пропускай задания, чья фаза уже прошла (LeftBuildingAndReturnedUnseen после Night).
- Вынеси проверку «нужно ли завести самополомку» в общий метод и вызывай её и после позднего входа.

---

## 9. [Мелко] Подсказка «[B] Настучать» видна из укрытия, а сервер молча отказывает

**Где:** `ObshagaHUD.cpp` (~228). `ObshagaPlayerController.cpp`, `OnTipOffRoom` (~345). `ObshagaGameMode.cpp`, `TipOffRoom` (~524).

**Как чинить:**
- В HUD и в `OnTipOffRoom` добавь проверки `!IsHiding() && !IsFrozen()` и что раунд InProgress.
- В `TipOffRoom` при отказе пиши причину, например «Сначала вылези из укрытия».
- В `SetTaskAbilities` вызывай `ForceNetUpdate()`.

---

## 10. [Мелко] Обвинение R тратится, когда коменданту некогда

**Где:** `ObshagaGameMode.cpp`, `Accuse` (~501–511).

**Что происходит.** Та же беда, что №2 в части 1. Если комендант в погоне или на допросе, `InvestigateTip` ничего не делает. При этом игроку пишется «Комендант поверил и пошёл разбираться».

**Как чинить:** добавь в контроллер `bool CanTakeTip() const`: граф готов, раунд идёт, состояние не Chase и не Interrogate. Проверяй его в `Accuse` до траты попытки. Если комендант занят, пиши «Комендант занят — скажи позже» и выходи. Проверка не зависит от того, виновен ли подозреваемый, поэтому ничего не выдаёт.

---

## 11. [Мелко] Игрок вышел из игры, пока был на улице, и ночной выход занят

**Где:** `NightExitDoor.cpp` (~76–88).

**Что происходит.** У двери `HiddenPlayer` остаётся указывать на удалённую пешку. Остальные получают «На улице уже кто-то есть».

**Как чинить:** исправление №6 из части 1 (`EndPlay` → `ForgetHiddenPlayer`) закрывает и это, потому что дверь — наследник `AHidingSpot`. Для надёжности добавь в начало `Interact` строку `if (HiddenPlayer && !IsValid(HiddenPlayer)) HiddenPlayer = nullptr;`.

---

## 12. [Мелко] Улица не закрывается утром

**Где:** `NightExitDoor.cpp` (~76). `ObshagaGameMode.cpp`, `BeginPhase`.

**Что происходит.** Можно выйти в 4:59 ночи, просидеть на улице всё утро и вернуться за секунду до конца. curfew_breach засчитывается почти без риска. Если игрок не вернулся, на итогах он висит «в укрытии».

**Как чинить:** добавь `ANightExitDoor::ForceReturn()`, который вызывает `EjectHiddenPlayer()`. Вызывай его для всех дверей в `BeginPhase(Morning)`.

---

## 13. [Мелко] На улице камера внутри двери и надпись «Ты в укрытии»

**Где:** `ObshagaCharacter.cpp`, `EnterHidingSpot` (~151). `ObshagaHUD.cpp` (~201).

**Как чинить:**
- В `AHidingSpot` заведи `virtual FVector GetHiddenPlayerLocation(float HalfHeight) const`. По умолчанию он возвращает текущую точку.
- В `ANightExitDoor` переопредели его так, чтобы точка была за дверью со стороны улицы: `GetActorLocation() - GetActorForwardVector() * (BoxSize.X * 0.5f + 60.f) + FVector(0,0,HalfHeight+2)`.
- Используй его в `EnterHidingSpot`.
- В HUD для `ANightExitDoor` пиши «Ты на улице».

---

## 14. [Удобство] Трудно поднять предмет у самых ног

Это не ошибка кода: трассировка персонажа не задевает. Но луч из центра экрана упирается в пол далеко впереди. Предмет ближе ~60 см к ногам ловится, только если смотреть почти вертикально вниз. Предмет за спиной не ловится вовсе. Так и не получилось поднять запрещёнку с пола в тесте.

**Как чинить:**
- В `FindFocusedActor`, если ничего не найдено и игрок смотрит вниз (`ViewRotation.Vector().Z < -0.4`), сделай ещё один `OverlapMultiByChannel`. Сфера ~80 см у ног, со сдвигом на 40 см вперёд по направлению взгляда. Учитывай только `AItemActor`.
- Сервер и так перепроверяет дальность и видимость.
- Добавь точку-прицел в HUD.
