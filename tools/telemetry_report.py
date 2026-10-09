# Отчёт по балансу из телеметрии раундов (Saved/Telemetry/*.json). Нужен для M8: правим числа по данным, а не на глаз.
# Запуск (обычный Python 3, подойдёт и тот, что идёт с движком):
#   python tools/telemetry_report.py                — все файлы из Saved/Telemetry
#   python tools/telemetry_report.py <папка> [мин_игроков]  — другая папка; раунды с меньшим числом игроков пропустить
import glob
import json
import os
import sys
from collections import defaultdict

# Пары заданий, которые мешают друг другу (CLAUDE.md, раздел 9). Если таблица DT_Tasks изменится — поправить и здесь.
CONFLICTS = [
    ("steal_tv", "protect_tv"),
    ("frame_roommate", "investigate_theft"),
    ("hide_contraband", "tip_off"),
    ("sabotage_kitchen", "fix_kitchen"),
    ("start_rumor", "expose_liar"),
]

# События, которые считаем «драматическими моментами» раунда.
DRAMA = {
    "PlayerCaught": "поимка",
    "PlayerFramed": "подстава",
    "PlayerFoundHiding": "нашли в укрытии",
    "PlayerEvicted": "выселение",
    "InterrogationLieSucceeded": "враньё прошло",
    "InterrogationLieFailed": "враньё не прошло",
    "AlibiConfirmed": "алиби",
    "ContrabandConfiscated": "изъята запрещёнка",
    "TipSucceeded": "стук сработал",
    "Accusation": "обвинение",
}

# Цели из CLAUDE.md, раздел 12.
STRIKES_TARGET = (0.5, 1.5)
CONFLICT_TARGET = 0.30
DRAMA_TARGET = 2
DURATION_TARGET = (600, 840)


def conflict_share(task_ids):
    """Доля заданий раунда, у которых в этом же раунде есть задание-противник."""
    present = set(task_ids)
    clashing = set()
    for first, second in CONFLICTS:
        if first in present and second in present:
            clashing.update((first, second))
    return sum(1 for task in task_ids if task in clashing) / len(task_ids) if task_ids else 0.0


def verdict(value, low, high):
    if value < low:
        return "ниже цели"
    if value > high:
        return "выше цели"
    return "в цели"


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    folder = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, "Saved", "Telemetry")
    min_players = int(sys.argv[2]) if len(sys.argv) > 2 else 1

    rounds = []
    for path in sorted(glob.glob(os.path.join(folder, "*.json"))):
        try:
            with open(path, "r", encoding="utf-8-sig") as handle:
                data = json.load(handle)
        except (OSError, ValueError) as error:
            print("пропущен %s: %s" % (os.path.basename(path), error))
            continue
        if data.get("numPlayers", 0) >= min_players:
            rounds.append(data)

    if not rounds:
        print("Нет раундов в %s" % folder)
        return

    durations = [r.get("durationSeconds", 0) for r in rounds]
    players = [p for r in rounds for p in r.get("players", [])]
    strikes = [p.get("strikes", 0) for p in players]
    evicted = sum(1 for p in players if p.get("evicted"))
    shares = [conflict_share([t["id"] for p in r.get("players", []) for t in p.get("tasks", [])]) for r in rounds]
    drama = [sum(1 for e in r.get("events", []) if e.get("type") in DRAMA) for r in rounds]

    print("Раундов: %d, игроков в них: %d (в среднем %.1f на раунд)" % (len(rounds), len(players), len(players) / len(rounds)))
    average_duration = sum(durations) / len(durations)
    print("Длительность: в среднем %.0f с (от %.0f до %.0f) — %s (цель %d–%d с)" % (
        average_duration, min(durations), max(durations), verdict(average_duration, *DURATION_TARGET), DURATION_TARGET[0], DURATION_TARGET[1]))
    average_strikes = sum(strikes) / len(strikes) if strikes else 0.0
    print("Страйки на игрока за раунд: %.2f — %s (цель %.1f–%.1f); выселено: %d" % (
        average_strikes, verdict(average_strikes, *STRIKES_TARGET), STRIKES_TARGET[0], STRIKES_TARGET[1], evicted))
    average_share = sum(shares) / len(shares)
    print("Конфликтующие задания: в среднем %.0f%% — %s (цель от %.0f%%); раундов ниже цели: %d" % (
        average_share * 100, "в цели" if average_share >= CONFLICT_TARGET else "ниже цели", CONFLICT_TARGET * 100,
        sum(1 for share in shares if share < CONFLICT_TARGET)))
    average_drama = sum(drama) / len(drama)
    print("Драматические моменты: в среднем %.1f на раунд — %s (цель от %d); раундов без них: %d" % (
        average_drama, "в цели" if average_drama >= DRAMA_TARGET else "ниже цели", DRAMA_TARGET, sum(1 for count in drama if count == 0)))

    kinds = defaultdict(int)
    for r in rounds:
        for event in r.get("events", []):
            if event.get("type") in DRAMA:
                kinds[DRAMA[event["type"]]] += 1
    if kinds:
        print("  из них: " + ", ".join("%s — %d" % pair for pair in sorted(kinds.items(), key=lambda pair: -pair[1])))

    print()
    print("Задания (выдано / выполнено / доля / награда):")
    given = defaultdict(int)
    done = defaultdict(int)
    reward = {}
    for p in players:
        for task in p.get("tasks", []):
            given[task["id"]] += 1
            done[task["id"]] += 1 if task.get("completed") else 0
            reward[task["id"]] = task.get("reward", 0)
    for task_id in sorted(given, key=lambda key: (done[key] / given[key], key)):
        rate = done[task_id] / given[task_id]
        note = "  <- ни разу не выполнено" if done[task_id] == 0 and given[task_id] >= 3 else ("  <- выполняется почти всегда" if rate >= 0.9 and given[task_id] >= 3 else "")
        print("  %-20s %3d / %3d / %3.0f%% / +%d%s" % (task_id, given[task_id], done[task_id], rate * 100, reward[task_id], note))

    print()
    print("Роли (игроков / средние очки):")
    by_role = defaultdict(list)
    for p in players:
        by_role[p.get("role", "?")].append(p.get("score", 0))
    for role, scores in sorted(by_role.items()):
        print("  %-10s %3d / %.1f" % (role, len(scores), sum(scores) / len(scores)))


if __name__ == "__main__":
    main()
