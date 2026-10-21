#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Автопроверка раундов «Общаги» по журналу игры и телеметрии.

Что делает: читает журнал хоста (строки LogObshaga), делит его на раунды, находит к каждому
файл телеметрии (по строке «Telemetry saved») и по правилам из списка «не проверено»
(PROGRESS.md, «Баги») ставит каждому пункту итог: ПРОШЛО / НЕ ВСТРЕТИЛОСЬ / ОШИБКА — со строкой
журнала, на которой он основан. Несколько журналов (и старые копии Obshaga-backup-*.log)
складываются: пункт, не встретившийся в одной пачке раундов, может встретиться в другой.

Запуск (Windows, из папки проекта; нужен только Python 3, без пакетов):
    python tools\\verify_round.py                         — Saved\\Logs\\Obshaga.log + Saved\\Telemetry
    python tools\\verify_round.py --all-logs              — все Saved\\Logs\\Obshaga*.log, включая старые копии
    python tools\\verify_round.py D:\\logs\\host.log -v     — свой журнал; -v — все случаи, а не первый
    python tools\\verify_round.py --newest 10 --out report.txt
Игру для проверки запускать с -LogCmds="LogObshaga Verbose" (без подробных строк проверять нечего).
Код выхода всегда 0: итог читается из таблицы.
"""

import argparse
import glob
import json
import math
import os
import re
import sys
import traceback
from datetime import datetime

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

PASS, FAIL, SKIP = "pass", "fail", "skip"
STATUS_TEXT = {PASS: "ПРОШЛО", FAIL: "ОШИБКА", SKIP: "НЕ ВСТРЕТИЛОСЬ"}

# Значения по умолчанию из ObshagaRoundConfig.h; настоящие берутся из строки «Round config:» журнала
# (в DA_RoundConfig числа другие, поэтому без этой строки числовые сверки пропускаются).
CONFIG_DEFAULTS = {
    "confess": 10, "silent": 20, "lie": 30, "lieBase": 0.35, "alibiBonus": 0.3, "contrabandLie": 0.25,
    "afterStrike": 40.0, "evictStrikes": 3, "evictPenalty": 50, "interrogation": 8.0, "ratBonus": 15,
    "ratWindow": 60.0, "curfewRate": 15.0, "sms": 3, "grudge": 60.0, "unseen": 10.0, "curfewOutside": 10.0,
    "minNoise": 0.3, "inspect": 2.5, "alibiRadius": 800.0, "immunity": 10.0, "catch": 160.0, "chronicle": 8,
}
# Из кода: KomendantAIController.cpp (StuckSeconds) и ObshagaStory.h (NumWeekNights).
STUCK_SECONDS = 2.5
WEEK_NIGHTS = 5
# «Длинная погоня»: не меньше стольких секунд, метров бега и смен комнат у убегающего.
LONG_CHASE_SECONDS = 8.0
LONG_CHASE_CM = 1200
LONG_CHASE_ROOMS = 1

INTERROGATION_EVENTS = ("InterrogationConfessed", "InterrogationSilent", "InterrogationLieSucceeded", "InterrogationLieFailed")
STRIKE_EVENTS = ("InterrogationConfessed", "InterrogationSilent", "InterrogationLieFailed")

TITLES = {
    "evicted": "Выселен с вещами", "king": "Король общаги", "snitch": "Главный стукач", "framer": "Мастер подстав",
    "saint": "Святой", "favorite": "Любимчик коменданта", "friend": "Настоящий друг", "thief": "Лучший вор",
    "panic": "Паникёр", "quiet": "Тихоня",
}

# Задания: id → как называть в таблице.
TASK_NAMES = [
    ("steal_tv", "Задание «Укради телевизор» (steal_tv)"),
    ("protect_tv", "Задание «Сохрани телевизор» (protect_tv)"),
    ("frame_roommate", "Задание «Подставь соседа» (frame_roommate)"),
    ("investigate_theft", "Задание «Расследуй кражу» (investigate_theft)"),
    ("curfew_breach", "Задание «Ночная вылазка» (curfew_breach)"),
    ("cover_for_friend", "Задание «Прикрой друга» (cover_for_friend)"),
    ("hide_contraband", "Задание «Спрячь запрещёнку» (hide_contraband)"),
    ("tip_off", "Задание «Настучи на комнату» (tip_off)"),
    ("start_rumor", "Задание «Пусти слух» (start_rumor)"),
    ("expose_liar", "Задание «Найди автора слуха» (expose_liar)"),
    ("sabotage_kitchen", "Задание «Сломай на кухне» (sabotage_kitchen)"),
    ("fix_kitchen", "Задание «Почини на кухне» (fix_kitchen)"),
    ("fake_alarm", "Задание «Ложная тревога» (fake_alarm)"),
    ("build_trust", "Задание «Не попадись на глаза» (build_trust)"),
]

NUM = r"-?\d+(?:\.\d+)?"

# Строки журнала: имя вида → регулярное выражение по тексту после «LogObshaga: [Verbose: ]».
# Порядок важен: берётся первое совпадение.
PATTERNS = [
    ("event", r"^Event EGameEventType::(\w+)(?: t=(" + NUM + r"))? by (.*?) item=(\S+)(?: \(([^)]*)\))? room=(\S+) target=(.*)$"),
    ("round_started", r"^Round started: (\d+) s, (\d+) players"),
    ("round_ended", r"^Round ended$"),
    ("telemetry", r"^Telemetry saved: (.+)$"),
    ("config", r"^Round config: (.*)$"),
    ("personality", r"^Komendant personality: (\S+) \(night=(\d)\)(?:, patience=(" + NUM + r"), chase=(" + NUM + r"), sight=(" + NUM + r")/(" + NUM
     + r"), bluff=(" + NUM + r"), inspect=(" + NUM + r"), grudge=(\d)(?:, suspicionMult=(" + NUM + r"))?)?"),
    ("grudge", r"^Komendant grudge: personality (\S+), holds grudge (\d), caught last round (\d+)$"),
    ("grudge_on", r"^Komendant grudge on (.+): suspicion (" + NUM + r"), room (\S+) heat (" + NUM + r")$"),
    ("friday_grudge", r"^Friday grudge: (.+) \((\d+) strikes this week\)$"),
    ("phase", r"^Phase: ERoundPhase::(\w+)"),
    ("week", r"^Week (\d+), night (\d+)"),
    ("lives", r"^(.+) lives in (\S+) \((?:pawn (\S+), )?spawned at"),
    ("reset", r"^Round reset check: items off home (\d+), hidden (\d+), carried (\d+), broken devices (\d+), open doors (\d+), ghosts (\d+), evicted (\d+), strikes (\d+), suspicion (\d+)$"),
    ("room_table", r"^Room table: (\S+) «(.*)»: type ERoomType::(\w+), forbidden after curfew (\d), hiding spots (\d+)$"),
    ("task_assigned", r"^Task (\S+) \((main|side)\) assigned to (.+?), satisfied now (\d)(?:, cond ETaskCondition::(\w+) item (\S+) room (\S+) own (\d) count (\d+) reward (" + NUM + r"))?$"),
    ("task_final", r"^Task (\S+) of (.+): (COMPLETED|FAILED)(?:, reward (" + NUM + r"))?$"),
    ("final_score", r"^Final score (.+): start (" + NUM + r"), tasks \+(" + NUM + r"), rat bonus \+(" + NUM + r"), penalties -(" + NUM + r"), final (" + NUM + r")$"),
    ("result", r"^Result (.+): role (\w+), title «(.*)», score (" + NUM + r"), strikes (\d+), evicted (\d), bot (\d)$"),
    ("chronicle", r"^Chronicle (.*)$"),
    ("headline", r"^Headline (.*)$"),
    ("sms", r"^SMS (\d+) to (.+) \((\w+)\) t=(" + NUM + r"): (.*) \[(.*)\]$"),
    ("tip_accepted", r"^Tip by (.+) on (.+) accepted: room (\S+), window (" + NUM + r") s, t=(" + NUM + r")$"),
    ("tip_refused", r"^Tip by (.+) on (.+) refused: (.*)$"),
    ("rat_pending", r"^Rat bonus pending: (.+) \+(\d+) \(caught (.+) (" + NUM + r") s after the tip\)$"),
    ("rat_expired", r"^Rat tip expired: (.+) on (.+) \((" + NUM + r") s after the tip\)$"),
    ("rat_knows", r"^Rat (.+) knows (.+): (\S+)$"),
    ("bot_tipped", r"^Bot (.+) tipped off on (.+)$"),
    ("bot_runs_hide", r"^Bot (.+) runs to hide in (\S+)"),
    ("bot_hid", r"^Bot (.+) hid in (\S+)$"),
    ("bot_answers", r"^Bot (.+) answers EInterrogationChoice::(\w+)$"),
    ("bot_left", r"^Evicted bot (.+) left the building$"),
    ("interro_start", r"^Interrogation started: (.+) \(contraband=(\d)\)(?:, item=(\S+) \(([^)]*)\), t=(" + NUM + r"))?$"),
    ("interro_res", r"^Interrogation of (.+) resolved: EInterrogationChoice::(\w+), strike=(\d), penalty=(" + NUM + r"), alibi=(\d+)(?:, answered=(\d), chance=("
     + NUM + r"), roll=(" + NUM + r"), contraband=(\d), strikes=(\d+), score=(" + NUM + r"), t=(" + NUM + r"))?$"),
    ("alibi_ok", r"^Alibi for (.+) by (.+) accepted: (" + NUM + r") cm, total (\d+)$"),
    ("alibi_rej", r"^Alibi for (.+) by (.+) rejected: (.*)$"),
    ("ghost", r"^(.+) ghost (\d) \((\S+)\)( on client)?$"),
    ("evicted", r"^(.+) evicted(?:: strikes (\d+), score (" + NUM + r") -> (" + NUM + r"), bot (\d), t=(" + NUM + r"))?$"),
    ("chase_start", r"^Komendant chase start: (.+) \((\S+)\), item=(\S+) \(([^)]*)\), room=(\S+), z=(" + NUM + r"), dist=(" + NUM + r"), suspicion=(" + NUM
     + r"), t=(" + NUM + r")$"),
    ("chase_end", r"^Komendant chase end: (.+) \((\S+)\), reason=(\w+), (" + NUM + r") s, seen (" + NUM + r") s, unseen for (" + NUM + r") s, ran (" + NUM
     + r") cm, rooms (\S+) -> (\S+) \((\d+) changes\), z=(" + NUM + r"), item=(\S+) \(([^)]*)\), spot=(\S+) \((\S+)\), exit=(\d), suspicion=(" + NUM
     + r"), stuck=(\d+), t=(" + NUM + r")$"),
    ("state", r"^Komendant state: EKomendantState::(\w+)$"),
    ("searched", r"^Komendant searched (\S+): contraband=(\S+) player=(.+?)(?:, room=(\S+), phase=(\S+), queued=(\d), t=(" + NUM + r"))?$"),
    ("contraband_found", r"^Contraband (\S+) in (\S+) \(room (\S+)\): hider (.+), residents (.+), blamed (.+), framed (\d)$"),
    ("not_reach", r"^Komendant could not reach (\S+), skipping$"),
    ("stuck", r"^Komendant stuck near .* \(point (\d+) of (\d+)\), blocked by (\S+), snapping to waypoint$"),
    ("snapped", r"^Komendant snapped to (.+), state EKomendantState::(\w+), t=(" + NUM + r")$"),
    ("snap_skipped", r"^Komendant: no free room at (.+), snap skipped$"),
    ("gave_up", r"^Komendant gave up the path to (.+) after (\d+) tries, state EKomendantState::(\w+)$"),
    ("test_stuck", r"^Komendant test stuck: .*, t=(" + NUM + r")$"),
    ("test_chase", r"^Debug: test chase of (.+) \(item (\S+)\) from (" + NUM + r") cm, t=(" + NUM + r")$"),
    ("test_catch", r"^Debug: test catch of (.+), t=(" + NUM + r")$"),
    ("sees", r"^Komendant sees (.+) suspicious in (\S+): carry=(\w+) curfew=(\d) rate=(" + NUM + r") suspicion=(" + NUM + r"), t=(" + NUM + r")$"),
    ("heard", r"^Komendant heard (.+): loudness (" + NUM + r") at .*, (" + NUM + r") cm away, investigating (\d) \((.*)\), t=(" + NUM + r")$"),
    ("sortie", r"^Sortie of (.+) \((\S+)\): outside (" + NUM + r") s of (" + NUM + r"), counted (\d)$"),
    ("end_item", r"^End item (\S+) \(([^)]*)\): state EItemState::(\w+) room (\S+) home (\S+) hiddenBy (.+?) holder (.+?) spot (\S+)(?: contraband (\d) readable (\d))?$"),
    ("end_device", r"^End device (\S+) room (\S+): broken (\d) brokenBy (.+?) repairedBy (.+?) brokenAt (" + NUM + r")$"),
    ("room_tip", r"^Room tip by (.+): room (\S+), t=(" + NUM + r")$"),
    ("accuse", r"^Accuse by (.+) of (.+): (right|wrong) \("),
    ("hid_item", r"^(\S+) hid (\S+) in (\S+): player (.+), room (\S+), id=(\S+), contraband=(\d)$"),
    ("took_out", r"^(\S+) took (\S+) out of (\S+)$"),
    ("notice_bot", r"^Notice for (.+) \(no screen\): (.*)$"),
    ("notice_shown", r"^Notice shown to (.+): (.*)$"),
    ("layout", r"^Results layout: variant (-?\d+) \(two columns (\d), captions (\d), line (" + NUM + r")\), height (" + NUM + r") of (" + NUM
     + r"), fits (\d), players (\d+), chronicle (\d+), canvas (" + NUM + r")x(" + NUM + r")$"),
]
PATTERNS = [(kind, re.compile(rx)) for kind, rx in PATTERNS]

PREFIX = re.compile(r"^(?:\[(\d{4}\.\d\d\.\d\d-\d\d\.\d\d\.\d\d:\d{3})\])?(?:\[\s*\d+\])?LogObshaga: (?:(Verbose|VeryVerbose|Display|Warning|Error|Fatal): )?(.*)$")
CRASH = re.compile(r"(=== Critical error|Assertion failed|Fatal error!|Unhandled Exception|appError called|LogObshaga: Fatal:)")
ENSURE = re.compile(r"Ensure condition failed")


def fnum(text, default=None):
    try:
        return float(text)
    except (TypeError, ValueError):
        return default


def clip(text, size=150):
    text = text.strip()
    return text if len(text) <= size else text[: size - 1] + "…"


class Line(object):
    __slots__ = ("file", "no", "ts", "level", "text", "kind", "m", "round")

    def __init__(self, file, no, ts, level, text):
        self.file, self.no, self.ts, self.level, self.text = file, no, ts, level, text
        self.kind, self.m, self.round = None, None, None

    def g(self, index):
        return self.m.group(index)

    def short(self):
        stamp = self.ts[11:19] if self.ts else "стр. %d" % self.no
        return "[%s] %s" % (stamp, clip(self.text))


class Result(object):
    def __init__(self, status, line=None, note="", rnd=None):
        self.status, self.line, self.note, self.rnd = status, line, note, rnd


class Round(object):
    def __init__(self, file, index):
        self.file, self.index = file, index
        self.gindex = 0
        self.pre, self.lines, self.post = [], [], []
        self.finished = False
        self.telemetry_name = None
        self.telemetry = None
        self.telemetry_line = None
        self.homes = {}
        self.pawns = {}
        self.prev = None
        self.next = None
        self.config = dict(CONFIG_DEFAULTS)
        self.config_known = False
        self.room_table = {}

    def label(self):
        return "%s, раунд %d" % (os.path.basename(self.file), self.index)

    def of(self, kind, lines=None):
        return [line for line in (self.lines if lines is None else lines) if line.kind == kind]

    def events(self, upto_telemetry=False):
        out = []
        for line in self.lines:
            if upto_telemetry and line is self.telemetry_line:
                break
            if line.kind == "event":
                out.append(line)
        return out

    def t_of(self, line):
        """Секунды от начала раунда: из «t=…» в строке или по метке времени журнала."""
        if line.kind in ("event",) and line.g(2) is not None:
            return float(line.g(2))
        match = re.search(r"t=(" + NUM + r")\s*$", line.text) or re.search(r", t=(" + NUM + r")", line.text)
        if match:
            return float(match.group(1))
        start = stamp_seconds(self.lines[0].ts) if self.lines else None
        here = stamp_seconds(line.ts)
        return here - start if start is not None and here is not None else None

    def phase_at(self, line):
        phase = "Evening"
        for other in self.lines:
            if other is line:
                break
            if other.kind == "phase":
                phase = other.g(1)
        return phase

    def personality_at(self, line=None):
        found = None
        for other in self.lines:
            if other is line:
                break
            if other.kind == "personality":
                found = other
        return found


def stamp_seconds(ts):
    if not ts:
        return None
    try:
        moment = datetime.strptime(ts, "%Y.%m.%d-%H.%M.%S:%f")
    except ValueError:
        return None
    return (moment - datetime(2000, 1, 1)).total_seconds()


def classify(line):
    for kind, rx in PATTERNS:
        match = rx.match(line.text)
        if match:
            line.kind, line.m = kind, match
            return


class LogFile(object):
    def __init__(self, path):
        self.path = path
        self.rounds = []
        self.crashes = []
        self.ensures = []
        self.errors = []
        self.obshaga_lines = 0
        self.verbose_lines = 0
        self.auto_lines = []

    def parse(self, telemetry_dir):
        with open(self.path, "r", encoding="utf-8-sig", errors="replace") as handle:
            raw = handle.read().splitlines()
        homes, pawns, room_table = {}, {}, {}
        between = []
        current = None
        for number, text in enumerate(raw, 1):
            if CRASH.search(text):
                self.crashes.append(Line(self.path, number, None, "Fatal", text))
            elif ENSURE.search(text):
                self.ensures.append(Line(self.path, number, None, "Error", text))
            match = PREFIX.match(text.rstrip())
            if not match:
                continue
            line = Line(self.path, number, match.group(1), match.group(2) or "Log", match.group(3))
            self.obshaga_lines += 1
            if line.level == "Verbose":
                self.verbose_lines += 1
            if line.level == "Error":
                self.errors.append(line)
            classify(line)
            if line.text.startswith("Auto rounds:"):
                self.auto_lines.append(line)
            if line.kind == "lives":
                homes[line.g(1)] = line.g(2)
                if line.g(3):
                    pawns[line.g(3)] = line.g(1)
            elif line.kind == "room_table":
                room_table[line.g(1)] = (line.g(3), line.g(4) == "1", int(line.g(5)))

            if line.kind == "event" and line.g(1) == "RoundStarted":
                if current is not None and not current.finished:
                    self._close(current, homes, pawns, room_table, telemetry_dir)
                current = Round(self.path, len(self.rounds) + 1)
                current.pre = between
                if self.rounds:
                    current.prev = self.rounds[-1]
                    self.rounds[-1].next = current
                    self.rounds[-1].post = between
                self.rounds.append(current)
                between = []
            if current is not None and not current.finished:
                line.round = current
                current.lines.append(line)
                if line.kind == "round_ended":
                    current.finished = True
                    self._close(current, homes, pawns, room_table, telemetry_dir)
                    between = []
                elif line.kind == "telemetry":
                    current.telemetry_line = line
                    current.telemetry_name = line.g(1).strip()
            else:
                between.append(line)
        if current is not None and not current.finished:
            self._close(current, homes, pawns, room_table, telemetry_dir)
        if self.rounds and self.rounds[-1].finished:
            self.rounds[-1].post = between

    def _close(self, rnd, homes, pawns, room_table, telemetry_dir):
        rnd.homes = dict(homes)
        rnd.pawns = dict(pawns)
        rnd.room_table = dict(room_table)
        for line in rnd.lines:
            if line.kind == "config":
                rnd.config_known = True
                for key, value in re.findall(r"(\w+)=(" + NUM + r")", line.g(1)):
                    rnd.config[key] = float(value)
        if rnd.telemetry_name:
            rnd.telemetry = load_telemetry(rnd.telemetry_name, telemetry_dir)
            if rnd.telemetry:
                for player in rnd.telemetry.get("players", []):
                    if player.get("homeRoom"):
                        rnd.homes[player["name"]] = player["homeRoom"]


def load_telemetry(name, telemetry_dir):
    candidates = [os.path.join(telemetry_dir, os.path.basename(name.replace("\\", "/"))), name]
    for path in candidates:
        if os.path.isfile(path):
            try:
                with open(path, "r", encoding="utf-8-sig") as handle:
                    data = json.load(handle)
                data["_path"] = path
                return data
            except (OSError, ValueError):
                return None
    return None


# ---------------------------------------------------------------- помощники для проверок

def event_fields(line):
    """(тип, t, кто, актёр предмета, ItemId, комната, цель) из строки события."""
    return (line.g(1), fnum(line.g(2)), line.g(3), line.g(4), line.g(5) or "-", line.g(6), line.g(7))


def ev_list(rnd, upto_telemetry=False):
    """События раунда. exact=False — журнал старой сборки без «t=»: время взято по меткам журнала (точность ~1 с)."""
    out = []
    for line in rnd.events(upto_telemetry):
        kind, t, by, actor, item_id, room, target = event_fields(line)
        exact = t is not None
        if t is None:
            t = rnd.t_of(line)
        out.append({"type": kind, "t": t, "exact": exact, "by": by, "actor": actor, "id": item_id, "room": room, "target": target, "line": line})
    return out


def item_flags(rnd):
    """ItemId → (запрещёнка, записка): из строк «End item» и «hid … contraband=»."""
    flags = {}
    for line in rnd.of("end_item"):
        if line.g(9) is not None:
            flags[line.g(2)] = (line.g(9) == "1", line.g(10) == "1")
    for line in rnd.of("hid_item"):
        old = flags.get(line.g(6), (False, False))
        flags[line.g(6)] = (old[0] or line.g(7) == "1", old[1])
    for line in rnd.of("interro_start"):
        if line.g(2) == "1" and line.g(4) and line.g(4) != "-":
            old = flags.get(line.g(4), (False, False))
            flags[line.g(4)] = (True, old[1])
    flags.setdefault("Note", (False, True))
    return flags


def next_after(rnd, line, kinds, stop_kinds=(), predicate=None):
    """Первая строка нужного вида после line (до строки из stop_kinds)."""
    seen = False
    for other in rnd.lines:
        if other is line:
            seen = True
            continue
        if not seen:
            continue
        if other.kind in stop_kinds:
            return None
        if other.kind in kinds and (predicate is None or predicate(other)):
            return other
    return None


def prev_before(rnd, line, kinds, predicate=None):
    found = None
    for other in rnd.lines:
        if other is line:
            break
        if other.kind in kinds and (predicate is None or predicate(other)):
            found = other
    return found


def close(a, b, eps):
    return a is not None and b is not None and abs(a - b) <= eps


# ---------------------------------------------------------------- пересчёт заданий

def recompute_task(rnd, owner, task_line):
    """True/False — исход задания по журналу; None — посчитать нельзя (нет данных или граничный случай)."""
    if task_line is None or task_line.g(5) is None:
        return None, "строка выдачи без условия (старая сборка)"
    cond, item_param, room_param, own, count = task_line.g(5), task_line.g(6), task_line.g(7), task_line.g(8) == "1", int(task_line.g(9))
    events = ev_list(rnd, upto_telemetry=True)
    if not events:
        return None, "нет строк событий"
    flags = item_flags(rnd)
    window = rnd.config["unseen"] if rnd.config_known else None
    home = rnd.homes.get(owner)
    end_items = rnd.of("end_item")
    end_devices = rnd.of("end_device")

    def own_events(kind):
        return [e for e in events if e["type"] == kind and e["by"] == owner]

    def seen_between(start, stop):
        hits = [e for e in events if e["type"] in ("PlayerSpotted", "PlayerCaught") and e["by"] == owner]
        if any(start + 0.15 < e["t"] < stop - 0.15 for e in hits):
            return True
        if any(abs(e["t"] - start) <= 0.15 or abs(e["t"] - stop) <= 0.15 for e in hits):
            return None
        return False

    if cond in ("ItemInRoomAtEnd", "ItemNotMovedFromZone"):
        if not end_items:
            return None, "нет строк «End item»"
        target = home if own else room_param
        if not target or target == "None":
            return False, "комната цели неизвестна"
        hit = [l for l in end_items if l.g(2) == item_param and l.g(4) == target]
        return bool(hit), "%s в %s: %s" % (item_param, target, "да" if hit else "нет")

    if cond == "ContrabandSurvivedInspection":
        if not end_items:
            return None, "нет строк «End item»"
        hit = []
        for l in end_items:
            matches = flags.get(l.g(2), (False, False))[0] if item_param == "None" else l.g(2) == item_param
            if matches and l.g(3) == "Hidden" and l.g(6) == owner:
                hit.append(l)
        return bool(hit), ("спрятана в %s" % hit[0].g(8)) if hit else "в конце не лежит в тайнике от его имени"

    if cond == "AlibiConfirmedSuccessfully":
        return bool(own_events("AlibiConfirmed")), "событий AlibiConfirmed: %d" % len(own_events("AlibiConfirmed"))

    if cond == "NeverSpottedWholeRound":
        bad = own_events("PlayerSpottedSuspicious") + own_events("PlayerCaught")
        return not bad, "замечен/пойман %d раз" % len(bad)

    if cond == "NoiseLuredKomendantAndUnseen":
        lures = own_events("KomendantAlerted")
        if not lures:
            return False, "Палыч на его шум не ходил"
        last = lures[-1]["t"]
        caught = [e["t"] for e in own_events("PlayerCaught")]
        if any(abs(t - last) <= 0.15 for t in caught):
            return None, "поимка в ту же секунду, что и приманка"
        after = [t for t in caught if t > last]
        return not after, "приманка t=%.1f, поимок после: %d" % (last, len(after))

    if cond in ("ItemPlantedInRoomOfPlayer", "PlayerCaughtWithItem"):
        if own_events("PlayerFramed"):
            return True, "PlayerFramed"
        for plant in events:
            if plant["type"] not in ("ItemDropped", "ItemHidden") or plant["by"] != owner or plant["actor"] == "None":
                continue
            if plant["room"] == "None" or plant["room"] == home:
                continue
            is_contraband = flags.get(plant["id"], (False, False))[0]
            if (item_param == "None" and not is_contraband) or (item_param != "None" and plant["id"] != item_param):
                continue
            for caught in events:
                if caught["type"] == "PlayerCaught" and caught["actor"] == plant["actor"] and caught["t"] > plant["t"] \
                        and caught["by"] != owner and rnd.homes.get(caught["by"]) == plant["room"]:
                    return True, "%s оставил %s в %s, %s пойман с ним" % (owner, plant["id"], plant["room"], caught["by"])
        return False, "подстава не сложилась"

    if cond == "LeftBuildingAndReturnedUnseen":
        if window is None:
            return None, "нет строки Round config"
        left = None
        uncertain = False
        for e in events:
            if e["by"] != owner:
                continue
            if e["type"] == "LeftBuilding":
                left = e["t"]
            elif e["type"] == "ReturnedToBuilding" and left is not None:
                seen = seen_between(left - window, e["t"] + window)
                if seen is None:
                    uncertain = True
                elif not seen:
                    return True, "вышел t=%.1f, вернулся t=%.1f незамеченным" % (left, e["t"])
        return (None, "граница окна") if uncertain else (False, "вылазки без свидетелей нет")

    if cond == "InspectionFoundContrabandInRoom":
        for tip in own_events("RoomTipOff"):
            found = [e for e in events if e["type"] == "ContrabandConfiscated" and e["room"] == tip["room"] and e["t"] > tip["t"]]
            if found:
                return True, "стук на %s, изъято t=%.1f" % (tip["room"], found[0]["t"])
        return False, "после стука в той комнате ничего не изъяли"

    if cond == "NoteReadByDistinctPlayers":
        for plant in events:
            if plant["type"] not in ("ItemDropped", "ItemThrown", "ItemHidden") or plant["by"] != owner or plant["actor"] == "None":
                continue
            if not flags.get(plant["id"], (False, False))[1]:
                continue
            readers = set(e["by"] for e in events if e["type"] == "NoteRead" and e["actor"] == plant["actor"] and e["t"] > plant["t"]
                          and e["by"] not in ("-", owner))
            if len(readers) >= max(count, 1):
                return True, "записку прочитали: %s" % ", ".join(sorted(readers))
        return False, "читателей меньше %d" % max(count, 1)

    if cond in ("DeviceBrokenAndNotSeen", "DeviceRepaired"):
        if not end_devices:
            return None, "нет строк «End device»"
        uncertain = False
        for l in end_devices:
            if room_param != "None" and l.g(2) != room_param:
                continue
            broken, broken_by, repaired_by, broken_at = l.g(3) == "1", l.g(4), l.g(5), float(l.g(6))
            if cond == "DeviceRepaired":
                if not broken and repaired_by == owner and broken_by != owner:
                    return True, "%s починен" % l.g(1)
            elif broken and broken_by == owner:
                if window is None:
                    return None, "нет строки Round config"
                seen = seen_between(broken_at - window, broken_at + window)
                if seen is None:
                    uncertain = True
                elif not seen:
                    return True, "%s сломан t=%.1f, свидетелей нет" % (l.g(1), broken_at)
        return (None, "граница окна") if uncertain else (False, "условие не выполнено")

    if cond == "CorrectAccusation":
        acc = own_events("Accusation")
        if not acc:
            return False, "не обвинял"
        return acc[0]["actor"] != "None", "обвинение: %s" % ("верное" if acc[0]["actor"] != "None" else "мимо")

    return None, "условие %s не разобрано" % cond


def task_outcomes(rnd):
    """[(id задания, игрок, строка итога, выполнено ли, пересчёт, пояснение)] по раунду."""
    assigned = {}
    for line in rnd.of("task_assigned"):
        assigned[(line.g(1), line.g(3))] = line
    out = []
    for line in rnd.of("task_final"):
        task_id, owner, done = line.g(1), line.g(2), line.g(3) == "COMPLETED"
        verdict, note = recompute_task(rnd, owner, assigned.get((task_id, owner)))
        out.append((task_id, owner, line, done, verdict, note))
    return out


# ---------------------------------------------------------------- проверки

def check_no_crash(ctx):
    out = []
    for log in ctx.logs:
        if log.crashes:
            out.append(Result(FAIL, log.crashes[0], "падение в %s" % os.path.basename(log.path)))
        else:
            note = "%s: %d строк Obshaga" % (os.path.basename(log.path), log.obshaga_lines)
            if log.ensures:
                note += ", ensure: %d (первое: %s)" % (len(log.ensures), clip(log.ensures[0].text, 80))
            if log.errors:
                note += ", Error: %d" % len(log.errors)
            out.append(Result(PASS, None, note))
    return out


def check_rounds_complete(ctx):
    out = []
    for rnd in ctx.rounds:
        if not rnd.finished:
            out.append(Result(FAIL, rnd.lines[-1] if rnd.lines else None, "журнал оборвался посреди раунда (вылет, закрыли окно или остановили PIE)", rnd))
            continue
        started = rnd.of("round_started")
        players = started[0].g(2) if started else "?"
        tele = os.path.basename(rnd.telemetry["_path"]) if rnd.telemetry else ("файл %s не найден" % rnd.telemetry_name if rnd.telemetry_name else "нет строки Telemetry saved")
        out.append(Result(PASS, rnd.of("round_ended")[0], "%s жильцов, телеметрия: %s" % (players, tele), rnd))
    return out


def check_events_match(ctx):
    out = []
    for rnd in ctx.rounds:
        if not rnd.telemetry:
            continue
        log_events = ev_list(rnd, upto_telemetry=True)
        json_events = rnd.telemetry.get("events", [])
        if not log_events:
            continue
        if len(log_events) != len(json_events):
            out.append(Result(FAIL, rnd.telemetry_line, "событий в журнале %d, в телеметрии %d" % (len(log_events), len(json_events)), rnd))
            continue
        bad = None
        for index, (a, b) in enumerate(zip(log_events, json_events)):
            same = (a["type"] == b.get("type") and (a["by"] == "-" and "by" not in b or a["by"] == b.get("by"))
                    and (a["target"] == "-" and "target" not in b or a["target"] == b.get("target"))
                    and (a["room"] == "None" and "room" not in b or a["room"] == b.get("room")))
            if same and a["id"] != "-" and "item" in b:
                same = a["id"] == b["item"]
            if same and a["t"] is not None and isinstance(b.get("t"), (int, float)):
                same = abs(a["t"] - b["t"]) <= 0.11
            if not same:
                bad = (index, a, b)
                break
        if bad:
            index, a, b = bad
            out.append(Result(FAIL, a["line"], "событие №%d расходится с телеметрией: %s" % (index + 1, json.dumps(b, ensure_ascii=False)), rnd))
        else:
            out.append(Result(PASS, rnd.telemetry_line, "%d событий совпали по порядку, типу, людям и комнатам" % len(log_events), rnd))
    return out


def check_task_verdicts(ctx):
    out = []
    for rnd in ctx.rounds:
        finals = rnd.of("task_final")
        if not rnd.telemetry or not finals:
            continue
        tasks = {}
        for player in rnd.telemetry.get("players", []):
            for task in player.get("tasks", []):
                tasks[(task.get("id"), player.get("name"))] = task
        bad = None
        for line in finals:
            task = tasks.get((line.g(1), line.g(2)))
            done = line.g(3) == "COMPLETED"
            if task is None or bool(task.get("completed")) != done:
                bad = (line, "в телеметрии: %s" % (json.dumps(task, ensure_ascii=False) if task else "нет такого задания"))
                break
            if done and line.g(4) is not None and fnum(line.g(4)) != fnum(task.get("reward")):
                bad = (line, "награда в телеметрии %s" % task.get("reward"))
                break
        if bad:
            out.append(Result(FAIL, bad[0], bad[1], rnd))
        else:
            out.append(Result(PASS, finals[0], "%d итогов заданий совпали с телеметрией" % len(finals), rnd))
    return out


def check_task_recompute(ctx):
    out = []
    for rnd in ctx.rounds:
        if not rnd.finished:
            continue
        outcomes = task_outcomes(rnd)
        if not outcomes:
            continue
        wrong = [o for o in outcomes if o[4] is not None and o[4] != o[3]]
        known = [o for o in outcomes if o[4] is not None]
        if wrong:
            task_id, owner, line, done, verdict, note = wrong[0]
            out.append(Result(FAIL, line, "по журналу выходит %s: %s" % ("выполнено" if verdict else "провалено", note), rnd))
        elif known:
            out.append(Result(PASS, known[0][2], "пересчитано %d из %d заданий, все совпали" % (len(known), len(outcomes)), rnd))
    return out


def check_score_math(ctx):
    out = []
    for rnd in ctx.rounds:
        finals = rnd.of("final_score")
        if not finals:
            continue
        rewards, penalties, rat = {}, {}, {}
        for line in rnd.of("task_final"):
            if line.g(3) == "COMPLETED" and line.g(4) is not None:
                rewards[line.g(2)] = rewards.get(line.g(2), 0) + fnum(line.g(4))
        for line in rnd.of("interro_res"):
            penalties[line.g(1)] = penalties.get(line.g(1), 0) + fnum(line.g(4))
        for line in rnd.of("evicted"):
            if line.g(3) is not None:
                penalties[line.g(1)] = penalties.get(line.g(1), 0) + fnum(line.g(3)) - fnum(line.g(4))
        for line in rnd.of("rat_pending"):
            rat[line.g(1)] = rat.get(line.g(1), 0) + fnum(line.g(2))
        results = dict((line.g(1), line) for line in rnd.of("result"))
        json_players = dict((p.get("name"), p) for p in (rnd.telemetry or {}).get("players", []))
        bad = None
        for line in finals:
            name = line.g(1)
            start, tasks, bonus, pen, final = [fnum(line.g(i)) for i in range(2, 7)]
            problems = []
            if abs(start + tasks + bonus - pen - final) > 0.5:
                problems.append("start + tasks + bonus − penalties ≠ final")
            if any(l.g(4) is not None for l in rnd.of("task_final")) and abs(rewards.get(name, 0) - tasks) > 0.5:
                problems.append("награды заданий по строкам Task = %g" % rewards.get(name, 0))
            if abs(penalties.get(name, 0) - pen) > 0.5:
                problems.append("штрафы по допросам и выселению = %g" % penalties.get(name, 0))
            if abs(rat.get(name, 0) - bonus) > 0.5:
                problems.append("бонус Крысы по журналу = %g" % rat.get(name, 0))
            if name in results and abs(fnum(results[name].g(4)) - final) > 0.5:
                problems.append("на экране итогов %s" % results[name].g(4))
            player = json_players.get(name)
            if player is not None:
                if abs(fnum(player.get("score"), 0) - final) > 0.5:
                    problems.append("в телеметрии score %s" % player.get("score"))
                if "scoreBefore" in player and abs(fnum(player.get("scoreBefore"), 0) - start) > 0.5:
                    problems.append("в телеметрии scoreBefore %s" % player.get("scoreBefore"))
                if "ratBonus" in player and abs(fnum(player.get("ratBonus"), 0) - bonus) > 0.5:
                    problems.append("в телеметрии ratBonus %s" % player.get("ratBonus"))
            if problems:
                bad = (line, "; ".join(problems))
                break
        if bad:
            out.append(Result(FAIL, bad[0], bad[1], rnd))
        else:
            out.append(Result(PASS, finals[0], "%d игроков: очки сходятся с заданиями, штрафами, бонусом и телеметрией" % len(finals), rnd))
    return out


def personality_numbers(rnd, line):
    found = rnd.personality_at(line)
    if found is None or found.g(3) is None:
        return None
    return {"name": found.g(1), "patience": fnum(found.g(3)), "grudge": found.g(9) == "1", "mult": fnum(found.g(10), 1.0)}


def check_escape(ctx):
    out = []
    for rnd in ctx.rounds:
        for line in rnd.of("chase_end"):
            if line.g(3) != "lost_sight":
                continue
            who, unseen, suspicion = line.g(1), fnum(line.g(6)), fnum(line.g(17))
            pers = personality_numbers(rnd, line)
            problems = []
            if rnd.config_known and abs(suspicion - rnd.config["afterStrike"]) > 0.5:
                problems.append("подозрение после побега %g, ждали %g" % (suspicion, rnd.config["afterStrike"]))
            if pers and unseen < pers["patience"] - 0.25:
                problems.append("бросил погоню через %.1f с без цели, терпение %.1f" % (unseen, pers["patience"]))
            state = next_after(rnd, line, ("state",))
            if state is not None and state.g(1) != "Investigate":
                problems.append("дальше состояние %s, а не Investigate" % state.g(1))
            note = "%s: погоня %s с, пробежал %.0f м, %s → %s, не видел %.1f с" % (who, line.g(4), fnum(line.g(7)) / 100, line.g(8), line.g(9), unseen)
            out.append(Result(FAIL if problems else PASS, line, "; ".join(problems) or note, rnd))
    return out


def hide_results(ctx, exit_flag):
    out = []
    for rnd in ctx.rounds:
        for line in rnd.of("chase_end"):
            if line.g(3) not in ("hid_seen", "hid_unseen") or line.g(16) != exit_flag:
                continue
            who, spot = line.g(1), line.g(14)
            if line.g(3) == "hid_unseen":
                problems = []
                if rnd.config_known and abs(fnum(line.g(17)) - rnd.config["afterStrike"]) > 0.5:
                    problems.append("подозрение %s, ждали %g" % (line.g(17), rnd.config["afterStrike"]))
                state = next_after(rnd, line, ("state",))
                if state is not None and state.g(1) != "Investigate":
                    problems.append("дальше %s, а не Investigate" % state.g(1))
                out.append(Result(FAIL if problems else PASS, line,
                                  "; ".join(problems) or "%s спрятался в %s незаметно, Палыч ищет вокруг" % (who, spot), rnd))
                continue
            follow = next_after(rnd, line, ("searched", "not_reach"), stop_kinds=("chase_start", "round_ended"),
                                predicate=lambda other: other.g(1) == spot)
            if follow is None:
                out.append(Result(SKIP, line, "раунд кончился раньше обыска", rnd))
            elif follow.kind == "not_reach":
                out.append(Result(FAIL, follow, "видел, как %s залез в %s, но не дошёл до укрытия" % (who, spot), rnd))
            else:
                found = follow.g(3)
                what = "вытащил %s" % found if found not in ("None", "") else "внутри уже пусто (%s вышел)" % who
                out.append(Result(PASS, follow, "видел, как %s залез в %s, открыл: %s" % (who, spot, what), rnd))
    return out


def check_wardrobe(ctx):
    out = hide_results(ctx, "0")
    if not any(r.status != SKIP for r in out):
        runs = sum(len(r.of("bot_runs_hide")) for r in ctx.rounds)
        hid = sum(len(r.of("bot_hid")) for r in ctx.rounds)
        out.append(Result(SKIP, None, "в погоне не прятались; вне погони боты бежали прятаться %d раз, спрятались %d" % (runs, hid)))
    return out


def check_night_exit(ctx):
    return hide_results(ctx, "1")


def check_morning_contraband(ctx):
    out = []
    for rnd in ctx.rounds:
        contents = {}
        for line in rnd.lines:
            if line.kind == "hid_item":
                contents[line.g(3)] = (line.g(2), line.g(6), line.g(7) == "1", line.g(4))
            elif line.kind == "took_out":
                if contents.get(line.g(3), (None,))[0] == line.g(2):
                    contents.pop(line.g(3), None)
            elif line.kind == "searched":
                spot, found = line.g(1), line.g(2)
                held = contents.get(spot)
                phase = (line.g(5) or "").replace("ERoundPhase::", "") or rnd.phase_at(line)
                if found not in ("None", ""):
                    contents.pop(spot, None)
                    if phase != "Morning":
                        continue
                    confiscated = next_after(rnd, line, ("event",), stop_kinds=("searched",),
                                             predicate=lambda e: e.g(1) == "ContrabandConfiscated" and e.g(4) == found)
                    detail = next_after(rnd, line, ("contraband_found",), stop_kinds=("searched",))
                    if confiscated is None:
                        out.append(Result(FAIL, line, "нашёл %s, но события ContrabandConfiscated нет" % found, rnd))
                    else:
                        blame = (" — записали на %s%s" % (detail.g(6), " (подстава)" if detail.g(7) == "1" else "")) if detail else ""
                        out.append(Result(PASS, line, "утренний обыск %s (комната %s) нашёл %s%s" % (spot, line.g(4) or "?", found, blame), rnd))
                elif held and held[2] and phase == "Morning":
                    out.append(Result(FAIL, line, "обыскал %s, где лежит запрещёнка %s (прятал %s), и не нашёл" % (spot, held[0], held[3]), rnd))
    return out


def interrogation_cases(ctx):
    """Каждый итог допроса: (раунд, строка, вид, список нарушений, пояснение)."""
    cases = []
    for rnd in ctx.rounds:
        strikes = {}
        for line in rnd.of("interro_res"):
            who, choice, strike, penalty, alibi = line.g(1), line.g(2), line.g(3) == "1", fnum(line.g(4)), int(line.g(5))
            answered = line.g(6)
            problems = []
            cfg = rnd.config
            if choice == "Confess":
                kind = "confess"
                if rnd.config_known and (not strike or penalty != cfg["confess"]):
                    problems.append("ждали страйк и −%g" % cfg["confess"])
            elif choice == "Lie":
                kind = "lie_ok" if not strike else "lie_fail"
                if answered is not None:
                    chance, roll, contraband = fnum(line.g(7)), fnum(line.g(8)), line.g(9) == "1"
                    if rnd.config_known:
                        expected = cfg["lieBase"] + cfg["alibiBonus"] * alibi - (cfg["contrabandLie"] if contraband else 0)
                        expected = min(max(expected, 0.05), 0.95)
                        if abs(expected - chance) > 0.011:
                            problems.append("шанс %.2f, по правилам %.2f" % (chance, expected))
                    if abs(roll - chance) > 0.006 and (roll < chance) == strike:
                        problems.append("бросок %.2f при шансе %.2f, а страйк=%d" % (roll, chance, strike))
                if rnd.config_known:
                    if strike and penalty != cfg["lie"]:
                        problems.append("штраф за неудачную ложь %g, ждали %g" % (penalty, cfg["lie"]))
                    if not strike and penalty != 0:
                        problems.append("удачная ложь со штрафом %g" % penalty)
            else:
                kind = "timeout" if answered == "0" else "silent"
                if rnd.config_known and (not strike or penalty != cfg["silent"]):
                    problems.append("ждали страйк и −%g" % cfg["silent"])
                if kind == "timeout":
                    start = prev_before(rnd, line, ("interro_start",), lambda other: other.g(1) == who)
                    t0, t1 = (rnd.t_of(start) if start else None), rnd.t_of(line)
                    if rnd.config_known and t0 is not None and t1 is not None and abs((t1 - t0) - cfg["interrogation"]) > 1.0:
                        problems.append("допрос длился %.1f с, а окно %g с" % (t1 - t0, cfg["interrogation"]))
            strikes[who] = strikes.get(who, 0) + (1 if strike else 0)
            if line.g(10) is not None and int(line.g(10)) != strikes[who]:
                problems.append("страйков %s, по журналу раунда %d" % (line.g(10), strikes[who]))
            expected_event = {"confess": "InterrogationConfessed", "lie_ok": "InterrogationLieSucceeded", "lie_fail": "InterrogationLieFailed"}.get(kind, "InterrogationSilent")
            event = next_after(rnd, line, ("event",), predicate=lambda e: e.g(3) == who and e.g(1) in INTERROGATION_EVENTS)
            if event is not None and event.g(1) != expected_event:
                problems.append("событие %s вместо %s" % (event.g(1), expected_event))
            note = "%s: %s, штраф %g, страйков %s, алиби %d" % (who, choice, penalty, line.g(10) or "?", alibi)
            if choice == "Lie" and answered is not None:
                note += ", шанс %s, бросок %s" % (line.g(7), line.g(8))
            cases.append((rnd, line, kind, problems, note))
    return cases


def interrogation_check(kind):
    def run(ctx):
        out = []
        for rnd, line, case_kind, problems, note in ctx.cache("interro", interrogation_cases):
            if case_kind == kind:
                out.append(Result(FAIL if problems else PASS, line, "; ".join(problems) or note, rnd))
        if kind == "lie_ok":
            out.extend(lie_statistics(ctx))
        return out
    return run


def lie_statistics(ctx):
    lies = [(rnd, line) for rnd, line, kind, _, _ in ctx.cache("interro", interrogation_cases)
            if kind in ("lie_ok", "lie_fail") and line.g(7) is not None]
    if len(lies) < 2:
        return []
    wins = sum(1 for rnd, line in lies if line.g(3) == "0")
    chances = [fnum(line.g(7)) for rnd, line in lies]
    mean = sum(chances)
    spread = math.sqrt(sum(p * (1 - p) for p in chances)) or 1.0
    z = (wins - mean) / spread
    note = "ложь удалась %d из %d, ожидалось ≈%.1f (отклонение %.1f σ)" % (wins, len(lies), mean, z)
    if len(lies) >= 10 and abs(z) > 3.5:
        return [Result(FAIL, None, note + " — шанс в игре не совпадает с настройкой")]
    return [Result(SKIP, None, note)]


def check_alibi(ctx):
    out = []
    for rnd in ctx.rounds:
        for line in rnd.of("alibi_ok"):
            suspect, helper, distance, total = line.g(1), line.g(2), fnum(line.g(3)), int(line.g(4))
            problems = []
            if rnd.config_known and distance > rnd.config["alibiRadius"] + 0.5:
                problems.append("принято с %.0f см при радиусе %g" % (distance, rnd.config["alibiRadius"]))
            res = next_after(rnd, line, ("interro_res",), predicate=lambda other: other.g(1) == suspect)
            if res is None:
                out.append(Result(SKIP, line, "допрос не закончился до конца журнала", rnd))
                continue
            if int(res.g(5)) < total:
                problems.append("в итоге допроса alibi=%s, а принято уже %d" % (res.g(5), total))
            confirmed = next_after(rnd, res, ("event",), stop_kinds=("interro_start",),
                                   predicate=lambda e: e.g(1) == "AlibiConfirmed" and e.g(3) == helper and e.g(7) == suspect)
            lie_ok = res.g(2) == "Lie" and res.g(3) == "0"
            if lie_ok and confirmed is None:
                problems.append("ложь удалась, а AlibiConfirmed для %s нет" % helper)
            if not lie_ok and confirmed is not None:
                problems.append("ложь не удалась, а AlibiConfirmed записан")
            note = "%s подтвердил алиби %s с %.0f см; %s" % (helper, suspect, distance,
                                                           "ложь удалась, алиби засчитано" if lie_ok else "%s — алиби не засчитано, как и должно" % res.g(2))
            out.append(Result(FAIL if problems else PASS, line, "; ".join(problems) or note, rnd))
    rejected = sum(len(r.of("alibi_rej")) for r in ctx.rounds)
    if rejected and not out:
        out.append(Result(SKIP, None, "попыток алиби отклонено: %d (далеко/повтор), принятых нет" % rejected))
    return out


def telemetry_invariants(data, config):
    problems = []
    events = data.get("events", [])
    duration = fnum(data.get("durationSeconds"), 0)
    window = (config or {}).get("interrogation", 10.0) + 1.5
    if events and (events[0].get("type") != "RoundStarted" or fnum(events[0].get("t"), 0) != 0):
        problems.append("первое событие не RoundStarted в t=0")
    times = [fnum(e.get("t"), 0) for e in events]
    if any(b < a for a, b in zip(times, times[1:])):
        problems.append("время событий идёт назад")
    if sum(1 for e in events if e.get("type") == "PlayerCaught") != data.get("numCaught", 0):
        problems.append("numCaught не равен числу PlayerCaught")
    for player in data.get("players", []):
        name = player.get("name")
        mine = [e for e in events if e.get("by") == name]
        pending = None
        for e in mine:
            if e.get("type") == "PlayerCaught":
                if pending is not None:
                    problems.append("%s пойман дважды без итога допроса (t=%s)" % (name, e.get("t")))
                pending = e
            elif e.get("type") in INTERROGATION_EVENTS:
                if pending is None:
                    problems.append("%s: итог допроса без поимки (t=%s)" % (name, e.get("t")))
                pending = None
        if pending is not None and duration - fnum(pending.get("t"), 0) > window:
            problems.append("%s: поимка t=%s без итога допроса" % (name, pending.get("t")))
        strikes = sum(1 for e in mine if e.get("type") in STRIKE_EVENTS)
        if strikes != player.get("strikes"):
            problems.append("%s: страйков %s, а по событиям %d" % (name, player.get("strikes"), strikes))
        evicted_events = sum(1 for e in mine if e.get("type") == "PlayerEvicted")
        if bool(player.get("evicted")) != (evicted_events > 0):
            problems.append("%s: evicted=%s, событий выселения %d" % (name, player.get("evicted"), evicted_events))
        if config and bool(player.get("evicted")) != (strikes >= config["evictStrikes"]):
            problems.append("%s: страйков %d, выселен=%s" % (name, strikes, player.get("evicted")))
    for e in events:
        if e.get("type") == "AlibiConfirmed":
            target, t = e.get("target"), fnum(e.get("t"), 0)
            if not any(o.get("type") == "InterrogationLieSucceeded" and o.get("by") == target and abs(fnum(o.get("t"), 0) - t) <= 0.25 for o in events):
                problems.append("AlibiConfirmed для %s (t=%s) без удачной лжи" % (target, e.get("t")))
        if e.get("type") == "TipSucceeded":
            target, t = e.get("target"), fnum(e.get("t"), 0)
            if not any(o.get("type") == "PlayerCaught" and o.get("by") == target and abs(fnum(o.get("t"), 0) - t) <= 0.25 for o in events):
                problems.append("TipSucceeded на %s без поимки" % target)
            if not any(o.get("type") == "TipOff" and o.get("by") == e.get("by") and o.get("target") == target and fnum(o.get("t"), 0) <= t for o in events):
                problems.append("TipSucceeded без стука TipOff")
    return problems


def check_telemetry_invariants(ctx):
    out = []
    seen = set()
    for rnd in ctx.rounds:
        if not rnd.telemetry:
            continue
        seen.add(os.path.abspath(rnd.telemetry["_path"]))
        problems = telemetry_invariants(rnd.telemetry, rnd.config if rnd.config_known else None)
        caught = rnd.telemetry.get("numCaught", 0)
        out.append(Result(FAIL if problems else PASS, rnd.telemetry_line,
                          "; ".join(problems[:3]) or "%s: поимок %d, у каждой ровно один итог, страйки и выселения сходятся" % (os.path.basename(rnd.telemetry["_path"]), caught), rnd))
    for path in ctx.extra_telemetry:
        if os.path.abspath(path) in seen:
            continue
        try:
            with open(path, "r", encoding="utf-8-sig") as handle:
                data = json.load(handle)
        except (OSError, ValueError) as error:
            out.append(Result(FAIL, None, "%s не читается: %s" % (os.path.basename(path), error)))
            continue
        problems = telemetry_invariants(data, None)
        out.append(Result(FAIL if problems else PASS, None, "%s: %s" % (os.path.basename(path), "; ".join(problems[:3]) or "поимки, допросы, страйки и выселения сходятся")))
    return out


def check_vindictive(ctx):
    out = []
    for rnd in ctx.rounds:
        grudge_lines = rnd.of("grudge")
        on_lines = rnd.of("grudge_on")
        first = next((l for l in rnd.of("personality") if l.g(2) == "0"), None)
        holds = grudge_lines[0].g(2) == "1" if grudge_lines else (first is not None and first.g(9) == "1")
        if not holds:
            if on_lines:
                out.append(Result(FAIL, on_lines[0], "Палыч не мстительный, а подозрение с прошлого раунда выставил", rnd))
            continue
        caught_before = int(grudge_lines[0].g(3)) if grudge_lines else None
        if not caught_before:
            out.append(Result(SKIP, grudge_lines[0] if grudge_lines else first, "мстительный, но в прошлом раунде никого не ловил", rnd))
            continue
        allowed = set()
        if rnd.prev is not None:
            allowed |= set(e.g(3) for e in rnd.prev.of("event") if e.g(1) == "PlayerCaught")
        for line in rnd.pre + rnd.lines:
            if line.kind == "friday_grudge":
                allowed.add(line.g(1))
        problems = []
        if len(on_lines) != caught_before:
            problems.append("помнит %d, а строк «grudge on» %d" % (caught_before, len(on_lines)))
        for line in on_lines:
            if rnd.config_known and abs(fnum(line.g(2)) - rnd.config["grudge"]) > 0.5:
                problems.append("%s: подозрение %s, ждали %g" % (line.g(1), line.g(2), rnd.config["grudge"]))
            if (rnd.prev is not None or allowed) and line.g(1) not in allowed:
                problems.append("%s не попадался в прошлом раунде" % line.g(1))
        if rnd.telemetry and "grudge" in rnd.telemetry:
            if sorted(rnd.telemetry["grudge"]) != sorted(l.g(1) for l in on_lines):
                problems.append("в телеметрии grudge=%s" % rnd.telemetry["grudge"])
        names = ", ".join("%s (%s)" % (l.g(1), l.g(2)) for l in on_lines)
        out.append(Result(FAIL if problems else PASS, on_lines[0] if on_lines else grudge_lines[0],
                          "; ".join(problems) or "мстительный помнит: %s" % names, rnd))
    return out


def check_stuck(ctx):
    out = []
    for rnd in ctx.rounds:
        stucks = rnd.of("stuck")
        for line in stucks:
            follow = next((l for l in rnd.lines[rnd.lines.index(line) + 1: rnd.lines.index(line) + 40] if l.kind in ("snapped", "snap_skipped")), None)
            if follow is None:
                out.append(Result(FAIL, line, "после «stuck» нет ни переноса, ни отказа", rnd))
            elif follow.kind == "snapped":
                out.append(Result(PASS, follow, "упёрся и перенесён на точку (состояние %s)" % follow.g(2), rnd))
            else:
                out.append(Result(SKIP, follow, "упёрся, у точки нет места — ждёт", rnd))
        if rnd.telemetry and "komendantStuck" in rnd.telemetry and rnd.finished:
            if int(rnd.telemetry["komendantStuck"]) != len(stucks):
                out.append(Result(FAIL, rnd.telemetry_line, "komendantStuck=%s, а строк «stuck» %d" % (rnd.telemetry["komendantStuck"], len(stucks)), rnd))
        for line in rnd.of("test_stuck"):
            t0 = fnum(line.g(1))
            later = [l for l in stucks if rnd.t_of(l) is not None and t0 <= rnd.t_of(l) <= t0 + STUCK_SECONDS + 3.0]
            state = next_after(rnd, line, ("state",))
            if later:
                continue
            if state is not None and rnd.t_of(state) is not None and rnd.t_of(state) <= t0 + STUCK_SECONDS + 1.0:
                out.append(Result(SKIP, line, "проверочный упор прерван сменой состояния (%s)" % state.g(1), rnd))
            else:
                out.append(Result(FAIL, line, "проверочный упор не вызвал переноса за %.1f с" % (STUCK_SECONDS + 3.0), rnd))
    return out


def check_long_chase(ctx):
    out = []
    longest = None
    for rnd in ctx.rounds:
        for start in rnd.of("chase_start"):
            if start.g(3) == "None":
                continue
            who = start.g(1)
            end = next_after(rnd, start, ("chase_end",), predicate=lambda other: other.g(1) == who)
            if end is None:
                continue
            duration, ran, rooms = fnum(end.g(4)), fnum(end.g(7)), int(end.g(10))
            forced = prev_before(rnd, start, ("test_chase",), lambda other: other.g(1) == who and abs(fnum(other.g(4)) - fnum(start.g(9))) < 0.5)
            if longest is None or duration > longest[0]:
                longest = (duration, end, rnd)
            if end.g(3) == "caught":
                interro = next_after(rnd, end, ("interro_start",), predicate=lambda other: other.g(1) == who)
                if interro is not None and interro.g(3) is not None and interro.g(3) != end.g(12):
                    out.append(Result(FAIL, interro, "в погоне нёс %s, а на допросе предмет %s" % (end.g(12), interro.g(3)), rnd))
                    continue
            if duration >= LONG_CHASE_SECONDS and ran >= LONG_CHASE_CM and rooms >= LONG_CHASE_ROOMS:
                note = "%s с %s (%s): %.1f с, %.0f м, %s → %s (%d смен), итог %s, переносов %s%s" % (
                    who, start.g(4), start.g(3), duration, ran / 100, end.g(8), end.g(9), rooms, end.g(3), end.g(18),
                    ", погоня вызвана -TestChaseAt" if forced else "")
                out.append(Result(PASS, end, note, rnd))
    if not any(r.status == PASS for r in out) and longest is not None:
        duration, end, rnd = longest
        out.append(Result(SKIP, end, "погони с вещью были, самая длинная %.1f с / %.0f м — короче порога %g с, %g м" % (
            duration, fnum(end.g(7)) / 100, LONG_CHASE_SECONDS, LONG_CHASE_CM / 100), rnd))
    return out


def eviction_cases(ctx, bot):
    out = []
    for rnd in ctx.rounds:
        for line in rnd.of("evicted"):
            if line.g(2) is None or (line.g(5) == "1") != bot:
                continue
            who, strikes, before, after, t = line.g(1), int(line.g(2)), fnum(line.g(3)), fnum(line.g(4)), fnum(line.g(6))
            problems = []
            if rnd.config_known:
                if strikes < rnd.config["evictStrikes"]:
                    problems.append("выселен при %d страйках" % strikes)
                if abs(before - after - rnd.config["evictPenalty"]) > 0.5:
                    problems.append("штраф %g, ждали %g" % (before - after, rnd.config["evictPenalty"]))
            later = [e for e in rnd.of("event") if e.g(3) == who and e.g(1) in ("PlayerCaught", "PlayerSpottedSuspicious") and fnum(e.g(2), 0) > t + 0.15]
            if later:
                problems.append("после выселения Палыч его ещё ловил/замечал (t=%s)" % later[0].g(2))
            if bot:
                left = next_after(rnd, line, ("bot_left",), predicate=lambda other: other.g(1) == who) or \
                    next((l for l in rnd.post if l.kind == "bot_left" and l.g(1) == who), None)
                end_t = rnd.t_of(rnd.lines[-1])
                if left is None and not problems:
                    if end_t is not None and end_t - t < 40:
                        out.append(Result(SKIP, line, "раунд кончился через %.0f с — до выхода бот не дошёл" % (end_t - t), rnd))
                    else:
                        out.append(Result(FAIL, line, "выселенный бот так и не ушёл через ночной выход", rnd))
                    continue
                note = "%s: %d страйка, %g → %g, ушёл через ночной выход" % (who, strikes, before, after)
            else:
                ghost = prev_before(rnd, line, ("ghost",), lambda other: other.g(1) == who and other.g(2) == "1" and not other.g(4))
                if ghost is None:
                    problems.append("нет строки «ghost 1» — не стал наблюдателем")
                note = "%s: %d страйка, %g → %g, стал призраком" % (who, strikes, before, after)
                if rnd.next is not None:
                    reset = next((l for l in rnd.next.pre if l.kind == "reset"), None)
                    if reset is not None and (reset.g(6) != "0" or reset.g(7) != "0"):
                        problems.append("в следующем раунде призраков %s, выселенных %s" % (reset.g(6), reset.g(7)))
                    elif reset is not None:
                        note += ", в реванше снова жилец"
            out.append(Result(FAIL if problems else PASS, line, "; ".join(problems) or note, rnd))
    return out


def check_evict_bot(ctx):
    return eviction_cases(ctx, True)


def check_evict_human(ctx):
    return eviction_cases(ctx, False)


def check_rat_bonus(ctx):
    out = []
    bot_tips, refusals = 0, {}
    for rnd in ctx.rounds:
        bot_tips += len(rnd.of("bot_tipped"))
        for line in rnd.of("tip_refused"):
            refusals[line.g(3)] = refusals.get(line.g(3), 0) + 1
        for line in rnd.of("tip_accepted"):
            rat, victim, t0 = line.g(1), line.g(2), fnum(line.g(5))
            window = fnum(line.g(4))
            catches = [e for e in rnd.of("event") if e.g(1) == "PlayerCaught" and e.g(3) == victim and fnum(e.g(2), -1) >= t0 - 0.15]
            in_window = [e for e in catches if fnum(e.g(2)) <= t0 + window + 0.15]
            pending = next_after(rnd, line, ("rat_pending",), predicate=lambda other: other.g(1) == rat)
            if not in_window:
                if pending is not None:
                    out.append(Result(FAIL, pending, "бонус без поимки в окне %g с" % window, rnd))
                else:
                    out.append(Result(SKIP, line, "стук не сработал: %s не поймали за %g с" % (victim, window), rnd))
                continue
            problems = []
            if pending is None:
                problems.append("поймали %s в окне, а бонуса нет" % victim)
            else:
                if rnd.config_known and fnum(pending.g(2)) != rnd.config["ratBonus"]:
                    problems.append("бонус %s, ждали %g" % (pending.g(2), rnd.config["ratBonus"]))
                succeeded = next_after(rnd, pending, ("event",), predicate=lambda e: e.g(1) == "TipSucceeded" and e.g(3) == rat and e.g(7) == victim)
                if succeeded is None:
                    problems.append("нет события TipSucceeded")
                final = next((l for l in rnd.of("final_score") if l.g(1) == rat), None)
                if final is not None and abs(fnum(final.g(4)) - fnum(pending.g(2))) > 0.5:
                    problems.append("в итоговых очках бонус %s" % final.g(4))
                player = next((p for p in (rnd.telemetry or {}).get("players", []) if p.get("name") == rat), None)
                if player is not None and "ratBonus" in player and abs(fnum(player["ratBonus"], 0) - fnum(pending.g(2))) > 0.5:
                    problems.append("в телеметрии ratBonus %s" % player["ratBonus"])
            note = "%s стукнул на %s, поймали через %.0f с, бонус +%s" % (rat, victim, fnum(in_window[0].g(2)) - t0, pending.g(2) if pending else "?")
            out.append(Result(FAIL if problems else PASS, pending or line, "; ".join(problems) or note, rnd))
    if not any(r.status == PASS for r in out) and (bot_tips or refusals):
        text = ", ".join("%s: %d" % (k, v) for k, v in refusals.items())
        out.append(Result(SKIP, None, "боты стучали %d раз; отказы сервера — %s" % (bot_tips, text or "нет")))
    return out


def sms_cases(ctx):
    cases = []
    for rnd in ctx.rounds:
        roles = dict((p.get("name"), p.get("role")) for p in (rnd.telemetry or {}).get("players", []))
        for line in rnd.of("sms"):
            index, reader, role, t, source = int(line.g(1)), line.g(2), line.g(3), fnum(line.g(4)), line.g(6)
            problems = []
            if roles.get(reader) and roles[reader] != role:
                problems.append("роль в СМС %s, в телеметрии %s" % (role, roles[reader]))
            if rnd.config_known and index > rnd.config["sms"]:
                problems.append("СМС №%d при лимите %g" % (index, rnd.config["sms"]))
            kind = "quiet"
            match = re.match(r"^event (\w+) by (.+) room (\S+) t=(" + NUM + r")$", source)
            if match:
                kind = "true"
                if role == "Paranoid":
                    problems.append("Параноику пришла правда")
                if match.group(2) == reader:
                    problems.append("слух про самого читателя")
                found = [e for e in ev_list(rnd) if e["type"] == match.group(1) and e["by"] == match.group(2) and e["room"] == match.group(3)
                         and close(e["t"], fnum(match.group(4)), 0.11 if e["exact"] else 1.5)]
                if not found:
                    problems.append("события %s у %s в %s t=%s в журнале нет" % match.groups())
                elif fnum(match.group(4)) > t + 0.11:
                    problems.append("слух о событии из будущего")
            elif source.startswith("lie"):
                kind = "lie"
                if role != "Paranoid":
                    problems.append("выдумка пришла не Параноику (%s)" % role)
            cases.append((rnd, line, kind, problems))
    return cases


def check_sms_true(ctx):
    out = []
    for rnd, line, kind, problems in ctx.cache("sms", sms_cases):
        if kind == "true" or (problems and kind != "lie"):
            out.append(Result(FAIL if problems else PASS, line, "; ".join(problems) or "слух по настоящему событию: %s" % line.g(6), rnd))
    if not out:
        quiet = sum(1 for _, _, kind, _ in ctx.cache("sms", sms_cases) if kind == "quiet")
        if quiet:
            out.append(Result(SKIP, None, "все %d СМС — «подозрительно тихо»: событий с другими игроками не было" % quiet))
    return out


def check_sms_paranoid(ctx):
    out = []
    for rnd, line, kind, problems in ctx.cache("sms", sms_cases):
        if line.g(3) == "Paranoid" or kind == "lie":
            if kind == "quiet" and not problems:
                out.append(Result(SKIP, line, "Параноику пришло «тихо» (врать было не о ком)", rnd))
            else:
                out.append(Result(FAIL if problems else PASS, line, "; ".join(problems) or "Параноику выдумка: %s" % line.g(6), rnd))
    return out


def check_curfew(ctx):
    out = []
    for rnd in ctx.rounds:
        for line in rnd.of("sees"):
            if line.g(4) != "1":
                continue
            who, room, carry, rate = line.g(1), line.g(2), line.g(3), fnum(line.g(5))
            phase = rnd.phase_at(line)
            problems = []
            if phase != "Night":
                problems.append("отбой засчитан в фазе %s" % phase)
            if rnd.room_table and room in rnd.room_table and not rnd.room_table[room][1]:
                problems.append("комната %s не запретна после отбоя" % room)
            pers = personality_numbers(rnd, line)
            if carry == "none" and rnd.config_known and pers is not None:
                expected = rnd.config["curfewRate"] * pers["mult"]
                if abs(expected - rate) > 0.06:
                    problems.append("скорость %.1f/с, ждали %.1f" % (rate, expected))
            out.append(Result(FAIL if problems else PASS, line, "; ".join(problems) or "%s ночью в %s: подозрение +%.1f/с, сейчас %s" % (who, room, rate, line.g(6)), rnd))
    return out


def check_four_players(ctx):
    out = []
    for rnd in ctx.rounds:
        started = rnd.of("round_started")
        if not started or not rnd.finished:
            continue
        players = int(started[0].g(2))
        bots = sum(1 for l in rnd.of("result") if l.g(7) == "1")
        if players >= 4:
            out.append(Result(PASS, started[0], "%d жильцов, из них ботов %d (живые люди так не проверяются)" % (players, bots), rnd))
    return out


def check_tv_scenario(ctx):
    out = []
    for rnd in ctx.rounds:
        tv_ids = set(l.g(6) for l in rnd.of("task_assigned") if l.g(1) in ("steal_tv", "protect_tv") and l.g(6) and l.g(6) != "None")
        events = ev_list(rnd)
        if not tv_ids:
            tv_ids = set(e["id"] for e in events if e["id"].lower() in ("tv", "television"))
        for pick in events:
            if pick["type"] != "ItemPickedUp" or pick["id"] not in tv_ids:
                continue
            who, actor = pick["by"], pick["actor"]
            home = rnd.homes.get(who)
            hidden = next((e for e in events if e["type"] == "ItemHidden" and e["by"] == who and e["actor"] == actor and e["t"] > pick["t"]), None)
            if hidden and home and hidden["room"] == home:
                end = next((l for l in rnd.of("end_item") if l.g(1) == actor), None)
                tail = (", в конце: %s в %s" % (end.g(3), end.g(4))) if end else ""
                out.append(Result(PASS, hidden["line"], "%s унёс ТВ из %s и спрятал у себя в %s за %.0f с%s" % (
                    who, pick["room"], home, hidden["t"] - pick["t"], tail), rnd))
                break
    return out


def task_row(task_id):
    def run(ctx):
        out = []
        failed_only = 0
        for rnd in ctx.rounds:
            if not rnd.finished:
                continue
            for tid, owner, line, done, verdict, note in task_outcomes(rnd):
                if tid != task_id:
                    continue
                if verdict is not None and verdict != done:
                    out.append(Result(FAIL, line, "сервер: %s, по журналу: %s (%s)" % ("выполнено" if done else "провалено",
                                                                                     "выполнено" if verdict else "провалено", note), rnd))
                elif done:
                    reward = line.g(4) or "?"
                    out.append(Result(PASS, line, "%s выполнил, +%s (%s)%s" % (owner, reward, note, "" if verdict is not None else ", пересчитать нельзя"), rnd))
                else:
                    failed_only += 1
        out.extend(task_extra(ctx, task_id))
        if not any(r.status in (PASS, FAIL) for r in out) and failed_only:
            out.append(Result(SKIP, None, "выдавалось %d раз, ни разу не выполнено (пересчёт согласен)" % failed_only))
        return out
    return run


def task_extra(ctx, task_id):
    out = []
    for rnd in ctx.rounds:
        if task_id == "curfew_breach":
            for line in rnd.of("sortie"):
                outside, needed, counted = fnum(line.g(3)), fnum(line.g(4)), line.g(5) == "1"
                if (outside >= needed) != counted:
                    out.append(Result(FAIL, line, "гулял %.1f с из %.1f, засчитано=%d" % (outside, needed, counted), rnd))
                elif counted and next_after(rnd, line, ("event",), predicate=lambda e: e.g(1) == "ReturnedToBuilding" and e.g(3) == line.g(1)) is None:
                    out.append(Result(FAIL, line, "вылазка засчитана, а события ReturnedToBuilding нет", rnd))
        elif task_id == "fake_alarm" and rnd.of("heard"):
            for e in rnd.of("event"):
                if e.g(1) != "KomendantAlerted":
                    continue
                heard = prev_before(rnd, e, ("heard",), lambda other: other.g(1) == e.g(3) and other.g(4) == "1")
                if heard is None or not close(fnum(heard.g(6)), fnum(e.g(2)), 0.15):
                    out.append(Result(FAIL, e, "KomendantAlerted без строки «heard … investigating 1»", rnd))
    return out


def check_rematch(ctx):
    out = []
    names = ["вещей не на месте", "спрятано", "в руках", "сломано приборов", "открытых дверей", "призраков", "выселенных", "со страйками", "с подозрением"]
    for rnd in ctx.rounds:
        reset = next((l for l in rnd.pre if l.kind == "reset"), None)
        if reset is None:
            continue
        values = [int(reset.g(i)) for i in range(1, 10)]
        dirty = ["%s %d" % (name, value) for name, value in zip(names, values) if value]
        label = "реванш" if rnd.prev is not None else "первый раунд"
        out.append(Result(FAIL if dirty else PASS, reset, ("%s: " % label) + (", ".join(dirty) if dirty else "мир и жильцы сброшены"), rnd))
        week = next((l for l in rnd.pre if l.kind == "week"), None)
        prev_week = next((l for l in rnd.prev.pre if l.kind == "week"), None) if rnd.prev is not None else None
        if week is not None and prev_week is not None:
            night, week_id = int(week.g(2)), int(week.g(1))
            p_night, p_week = int(prev_week.g(2)), int(prev_week.g(1))
            expected = (p_night + 1) % WEEK_NIGHTS
            if night != expected or week_id != p_week + (1 if expected == 0 else 0):
                out.append(Result(FAIL, week, "после недели %d ночи %d идёт неделя %d ночь %d" % (p_week, p_night, week_id, night), rnd))
    return out


def expected_titles(data):
    events = data.get("events", [])
    players = data.get("players", [])

    def count(name, kind):
        return sum(1 for e in events if e.get("type") == kind and e.get("by") == name)

    scores = [int(fnum(p.get("score"), 0)) for p in players]
    best = max(scores) if scores else 0
    num_best = scores.count(best)
    out = {}
    for player in players:
        name, score = player.get("name"), int(fnum(player.get("score"), 0))
        caught = count(name, "PlayerCaught")
        if player.get("evicted"):
            title = TITLES["evicted"]
        elif score == best and num_best == 1 and best > 0:
            title = TITLES["king"]
        elif count(name, "TipSucceeded") > 0:
            title = TITLES["snitch"]
        elif count(name, "PlayerFramed") > 0:
            title = TITLES["framer"]
        elif caught == 0 and count(name, "PlayerSpottedSuspicious") == 0:
            title = TITLES["saint"]
        elif caught >= 2:
            title = TITLES["favorite"]
        elif count(name, "AlibiConfirmed") > 0:
            title = TITLES["friend"]
        elif count(name, "ItemPickedUp") >= 3:
            title = TITLES["thief"]
        elif count(name, "KomendantAlerted") >= 2:
            title = TITLES["panic"]
        else:
            title = TITLES["quiet"]
        out[name] = title
    return out


def check_titles(ctx):
    out = []
    for rnd in ctx.rounds:
        data = rnd.telemetry
        if not data or not any("title" in p for p in data.get("players", [])):
            continue
        expected = expected_titles(data)
        shown = dict((l.g(1), l.g(3)) for l in rnd.of("result"))
        bad = []
        for player in data.get("players", []):
            name = player.get("name")
            if player.get("title") != expected[name]:
                bad.append("%s: «%s», по правилам «%s»" % (name, player.get("title"), expected[name]))
            elif name in shown and shown[name] != expected[name]:
                bad.append("%s: на экране «%s»" % (name, shown[name]))
        line = rnd.of("result")[0] if rnd.of("result") else rnd.telemetry_line
        titles = ", ".join("%s — %s" % (n, t) for n, t in expected.items())
        out.append(Result(FAIL if bad else PASS, line, "; ".join(bad) or titles, rnd))
    return out


def check_chronicle(ctx):
    out = []
    for rnd in ctx.rounds:
        if not rnd.finished:
            continue
        lines = [l.g(1) for l in rnd.of("chronicle")]
        if not lines and rnd.telemetry:
            lines = rnd.telemetry.get("chronicle", [])
        if not lines and not rnd.of("headline") and not (rnd.telemetry and "chronicle" in rnd.telemetry):
            continue
        events = ev_list(rnd, upto_telemetry=True)
        worthy = [e for e in events if e["type"] in ("PlayerCaught", "ItemHidden", "TipOff", "ContrabandConfiscated", "PlayerFoundHiding",
                                                     "InterrogationConfessed", "InterrogationSilent")]
        problems = []
        limit = rnd.config["chronicle"] if rnd.config_known else None
        if limit is not None and len(lines) > limit:
            problems.append("строк %d при лимите %g" % (len(lines), limit))
        if worthy and not lines:
            problems.append("хроника пустая, хотя событий для неё %d" % len(worthy))
        stamps = []
        for text in lines:
            match = re.match(r"^(\d\d):(\d\d) — (.+)$", text)
            if not match:
                problems.append("строка без времени: %s" % clip(text, 60))
                continue
            if "{" in text or "}" in text:
                problems.append("неподставленный шаблон: %s" % clip(text, 60))
            seconds = int(match.group(1)) * 60 + int(match.group(2))
            stamps.append(seconds)
            if events and not any(e["t"] is not None and abs(math.floor(e["t"]) - seconds) <= 1 for e in events):
                problems.append("нет события на %s" % text[:5])
        if any(b < a for a, b in zip(stamps, stamps[1:])):
            problems.append("время строк идёт назад")
        headline = rnd.of("headline")
        note = "%d строк, первая: %s" % (len(lines), clip(lines[0], 70)) if lines else "событий для хроники не было"
        if headline:
            note += "; заголовок: %s" % clip(headline[0].g(1), 50)
        out.append(Result(FAIL if problems else PASS, (rnd.of("chronicle") or [rnd.telemetry_line])[0], "; ".join(problems) or note, rnd))
    return out


def check_layout(ctx):
    out = []
    for rnd in ctx.rounds:
        for line in rnd.post + [l for l in rnd.lines if l.kind == "layout"]:
            if line.kind != "layout":
                continue
            fits = line.g(7) == "1"
            note = "раскладка %s, высота %s из %s, игроков %s, хроника %s, окно %sx%s" % (
                line.g(1), line.g(5), line.g(6), line.g(8), line.g(9), line.g(10), line.g(11))
            out.append(Result(PASS if fits else FAIL, line, note if fits else "не влезает над «Реваншем»: " + note, rnd))
    return out


CHECKS = [
    ("Журнал без падений", check_no_crash, "журнала нет"),
    ("Раунды доиграны до итогов", check_rounds_complete, "в журнале нет ни одного раунда (RoundStarted)"),
    ("События журнала = телеметрия", check_events_match, "нет пары журнал + файл телеметрии"),
    ("Итоги заданий = телеметрия", check_task_verdicts, "нет итогов заданий"),
    ("Пересчёт всех заданий по журналу", check_task_recompute, "нет итогов заданий с условиями"),
    ("Арифметика очков", check_score_math, "нет строк «Final score»"),
    ("Побег: Палыч потерял из виду", check_escape, "не было; ключ -TestChaseAt=N даёт погоню"),
    ("Прятки в шкафу во время погони", check_wardrobe, "не было"),
    ("Ночной выход как укрытие от погони", check_night_exit, "не было"),
    ("Утренний обыск нашёл запрещёнку", check_morning_contraband, "утром запрещёнки в тайниках не было"),
    ("Допрос: сознался", interrogation_check("confess"), "никто не сознавался"),
    ("Допрос: неудачная ложь", interrogation_check("lie_fail"), "не было"),
    ("Допрос: удачная ложь", interrogation_check("lie_ok"), "не было"),
    ("Допрос: молчание (ответил)", interrogation_check("silent"), "не было"),
    ("Допрос: не ответил за время → молчание", interrogation_check("timeout"), "ключ -TestCatchHostAt=40 (хост не отвечает)"),
    ("Алиби на допросе", check_alibi, "свидетеля рядом с допросом не было"),
    ("Телеметрия: поимки, допросы, страйки", check_telemetry_invariants, "нет файлов телеметрии"),
    ("Длинная погоня с вещью в руках", check_long_chase, "погонь с вещью не было; ключ -TestChaseAt=N"),
    ("Палыч упёрся → перенос на точку", check_stuck, "не упирался; ключ -TestKomendantStuck=N"),
    ("Выселение бота (уходит через выход)", check_evict_bot, "ботов не выселяли"),
    ("Выселение человека → призрак", check_evict_human, "людей не выселяли; ключ -TestCatchHostAt=40,100,160"),
    ("Бонус Крысе за поимку после стука", check_rat_bonus, "стуков Крысы не было"),
    ("СМС: правдивые слухи", check_sms_true, "СМС не было"),
    ("СМС Параноика — выдумки", check_sms_paranoid, "Параноика не было (наверняка есть при 6+ жильцах: -Bots=5)"),
    ("Ночью в запретной зоне растёт подозрение", check_curfew, "ночью в запретных зонах никого не замечали"),
    ("Мстительный комендант", check_vindictive, "мстительный не выпадал; нужны раунды подряд (-AutoRounds=5, пятница)"),
    ("Раунд на 4+ жильцов", check_four_players, "законченных раундов на 4+ не было"),
    ("ТВ из гостиной в свой шкаф", check_tv_scenario, "никто не донёс ТВ до своей комнаты"),
] + [(name, task_row(task_id), "не выдавалось") for task_id, name in TASK_NAMES] + [
    ("Реванш: мир и счёт сброшены", check_rematch, "нет строк «Round reset check»"),
    ("Экран итогов: титулы", check_titles, "нет телеметрии с титулами"),
    ("Экран итогов: хроника", check_chronicle, "нет строк «Chronicle» и телеметрии с хроникой"),
    ("Экран итогов: всё влезает", check_layout, "нет строк «Results layout» (окно не рисовало итоги)"),
]


class Context(object):
    def __init__(self, logs, extra_telemetry):
        self.logs = logs
        self.rounds = [rnd for log in logs for rnd in log.rounds]
        for index, rnd in enumerate(self.rounds, 1):
            rnd.gindex = index
        self.extra_telemetry = extra_telemetry
        self._cache = {}

    def cache(self, key, builder):
        if key not in self._cache:
            self._cache[key] = builder(self)
        return self._cache[key]


def collect_logs(args):
    paths = []
    patterns = list(args.logs)
    if args.all_logs:
        patterns.append(os.path.join(ROOT, "Saved", "Logs", "Obshaga*.log"))
    if not patterns:
        patterns.append(os.path.join(ROOT, "Saved", "Logs", "Obshaga.log"))
    missing = []
    for pattern in patterns:
        found = glob.glob(pattern)
        if not found:
            missing.append(pattern)
        paths.extend(found)
    unique = []
    for path in sorted(set(os.path.abspath(p) for p in paths), key=lambda p: os.path.getmtime(p)):
        unique.append(path)
    return unique, missing


def render(ctx, results, args, header):
    out = list(header)
    width = max(len(name) for name, _, _ in CHECKS) + 2
    out.append("")
    out.append("%-3s %-*s %-15s %s" % ("№", width, "Пункт", "Итог", "Случаи"))
    out.append("-" * (width + 40))
    totals = {PASS: 0, FAIL: 0, SKIP: 0}
    for number, (name, items, hint) in enumerate(results, 1):
        fails = [r for r in items if r.status == FAIL]
        passes = [r for r in items if r.status == PASS]
        status = FAIL if fails else (PASS if passes else SKIP)
        totals[status] += 1
        counts = "ок %d, ошибок %d" % (len(passes), len(fails)) if (passes or fails) else ""
        out.append("%-3d %-*s %-15s %s" % (number, width, name, STATUS_TEXT[status], counts))
        shown = fails if fails else (passes if passes else items)
        if not args.verbose:
            shown = shown[:1]
        if not shown:
            out.append("      — %s" % hint)
        for result in shown:
            where = (" (%s)" % result.rnd.label()) if result.rnd is not None else ""
            if result.note:
                out.append("      — %s%s" % (result.note, where))
            if result.line is not None:
                out.append("        %s" % result.line.short())
    out.append("-" * (width + 40))
    finished = sum(1 for r in ctx.rounds if r.finished)
    tele = sum(1 for r in ctx.rounds if r.telemetry)
    out.append("Итого: пунктов %d — ПРОШЛО %d, ОШИБКА %d, НЕ ВСТРЕТИЛОСЬ %d. Раундов %d (законченных %d, с телеметрией %d), журналов %d." % (
        len(results), totals[PASS], totals[FAIL], totals[SKIP], len(ctx.rounds), finished, tele, len(ctx.logs)))
    return out


def main(argv=None):
    parser = argparse.ArgumentParser(description="Автопроверка раундов «Общаги» по журналу и телеметрии.")
    parser.add_argument("logs", nargs="*", help="журналы (можно с * и ?); по умолчанию Saved/Logs/Obshaga.log")
    parser.add_argument("--all-logs", action="store_true", help="добавить все Saved/Logs/Obshaga*.log (и старые копии)")
    parser.add_argument("--telemetry", default=os.path.join(ROOT, "Saved", "Telemetry"), help="папка телеметрии (по умолчанию Saved/Telemetry)")
    parser.add_argument("--newest", type=int, default=0, help="ещё N самых новых файлов телеметрии проверить без журнала")
    parser.add_argument("-v", "--verbose", action="store_true", help="показывать все случаи, а не первый")
    parser.add_argument("--out", help="ещё и записать отчёт в файл (UTF-8)")
    args = parser.parse_args(argv)

    paths, missing = collect_logs(args)
    header = ["Проверка раундов «Общаги» — %s" % datetime.now().strftime("%Y-%m-%d %H:%M")]
    for pattern in missing:
        header.append("Журнал не найден: %s" % pattern)
    logs = []
    for path in paths:
        log = LogFile(path)
        try:
            log.parse(args.telemetry)
        except OSError as error:
            header.append("Не читается %s: %s" % (path, error))
            continue
        logs.append(log)
        header.append("Журнал %s: раундов %d, строк Obshaga %d (подробных %d)" % (path, len(log.rounds), log.obshaga_lines, log.verbose_lines))
        if log.obshaga_lines and not log.verbose_lines:
            header.append("  ! подробных строк нет — запускайте игру с -LogCmds=\"LogObshaga Verbose\"")
        if log.rounds and not any(l.kind == "config" for r in log.rounds for l in r.lines):
            header.append("  ! нет строк «Round config» — журнал записан сборкой без автопроверки, числа не сверяются")
    extra = []
    if args.newest > 0 and os.path.isdir(args.telemetry):
        files = sorted(glob.glob(os.path.join(args.telemetry, "round_*.json")), key=os.path.getmtime, reverse=True)
        extra = files[: args.newest]
        header.append("Телеметрия без журнала: %d файлов из %s" % (len(extra), args.telemetry))

    ctx = Context(logs, extra)
    results = []
    for name, check, hint in CHECKS:
        try:
            items = check(ctx) if (logs or check is check_telemetry_invariants) else []
        except Exception:  # одна сломанная проверка не должна прятать остальные
            items = [Result(FAIL, None, "сбой проверки: %s" % traceback.format_exc().strip().splitlines()[-1])]
        results.append((name, items, hint))
    report = render(ctx, results, args, header)
    text = "\n".join(report)
    print(text)
    if args.out:
        with open(args.out, "w", encoding="utf-8-sig") as handle:
            handle.write(text + "\n")
    return 0


if __name__ == "__main__":
    try:
        if hasattr(sys.stdout, "reconfigure"):
            sys.stdout.reconfigure(errors="replace")
        main()
    except Exception:
        print("Сбой скрипта (итог не посчитан):")
        traceback.print_exc(file=sys.stdout)
    sys.exit(0)
