#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""
Автопроверка раундов «Общаги» по журналу игры и телеметрии.

Что делает: читает журнал хоста (строки LogObshaga), делит его на раунды, находит к каждому
файл телеметрии (по строке «Telemetry saved») и по правилам из списка «не проверено»
(PROGRESS.md, «Баги») ставит каждому пункту итог: ПРОШЛО / НЕ ВСТРЕТИЛОСЬ / ОШИБКА — со строкой
журнала (время и текст), на которой он основан. Несколько журналов (и старые копии
Obshaga-backup-*.log) складываются: пункт, не встретившийся в одной пачке раундов, может
встретиться в другой.

Игра (упакованная сборка Development; в Shipping проверочных ключей и журнала нет):
    Obshaga.exe L_Obshaga?listen -windowed -ResX=1280 -ResY=720 -log -LogCmds="LogObshaga Verbose" <ключи пачки>
    Журнал — Windows\Obshaga\Saved\Logs\Obshaga.log, телеметрия — Windows\Obshaga\Saved\Telemetry.
    Пачки (6 жильцов, значит Параноик есть всегда):
    A, «как в жизни»:   -Bots=5 -AutoRounds=3                      — СМС, поломка на кухне, ночь, утренний обыск (~40 мин)
    B, «принудительно»: -Bots=5 -AutoRounds=2 -WeekNight=3 -TestCatchHostAt=40,100,160,220
                        -TestCatchBotAt=60,130,200,270 -TestChaseAt=300,420 -TestKomendantStuck=90
                        — выселение человека и бота, молчание по таймауту, длинная погоня, упор Палыча, пятничная месть (~25 мин)
    C, «экран итогов»:  -Bots=7 -AutoRounds=1 -TestPhaseSeconds=40 -ResX=960 -ResY=540  — 8 жильцов в маленьком окне (~3 мин)

Скрипт (нужен только Python 3, без пакетов; код выхода всегда 0 — это отчёт, итог читается из таблицы):
    python -X utf8 tools\verify_round.py "<сборка>\Windows\Obshaga\Saved\Logs\Obshaga*.log" --out report.txt
        — все журналы сборки (и старые копии); телеметрия берётся из Saved\Telemetry рядом с Logs
    python -X utf8 tools\verify_round.py --all-logs      — то же для Saved\Logs проекта (запуск из редактора)
    -v — все случаи по пункту, а не первый; --telemetry DIR — своя папка телеметрии;
    --newest N — ещё N свежих файлов телеметрии без журнала. «-X utf8» — чтобы русский текст не испортился
    при выводе в файл или в другую программу; report.txt пишется в UTF-8.
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
    "botConfess": 1.0, "botLie": 2.0, "botSilent": 1.0, "botAlibi": 0.35,
}
# Из кода: KomendantAIController.cpp (StuckSeconds), ObshagaStory.h (NumWeekNights), ObshagaBotController.cpp (думает 1,5–4 с).
STUCK_SECONDS = 2.5
WEEK_NIGHTS = 5
BOT_ANSWER = (1.4, 4.7)
# «Длинная погоня»: не меньше стольких секунд, секунд на глазах у Палыча, сантиметров бега и смен комнат (или этажа).
LONG_CHASE_SECONDS = 8.0
LONG_CHASE_SEEN = 3.0
LONG_CHASE_CM = 1500
LONG_CHASE_ROOMS = 2
FLOOR_CHANGE_CM = 200
# Побег засчитывается, только если Палыч видел цель хоть полсекунды (иначе это «погоня» вслепую из -TestChaseAt).
ESCAPE_MIN_SEEN = 0.5

INTERROGATION_EVENTS = ("InterrogationConfessed", "InterrogationSilent", "InterrogationLieSucceeded", "InterrogationLieFailed")
STRIKE_EVENTS = ("InterrogationConfessed", "InterrogationSilent", "InterrogationLieFailed")
SEEN_EVENTS = ("PlayerSpotted", "PlayerSpottedSuspicious", "PlayerCaught")
# Что выселенный уже не может делать (ходить, открывать двери и шуметь шагами по дороге к выходу — может).
EVICTED_FORBIDDEN = ("ItemPickedUp", "ItemDropped", "ItemThrown", "ItemHidden", "ItemFound", "NoteRead", "DeviceBroken", "DeviceRepaired",
                     "LeftBuilding", "ReturnedToBuilding", "PlayerSpotted", "PlayerSpottedSuspicious", "PlayerCaught", "Accusation",
                     "PlayerHid", "AlibiConfirmed", "TipOff", "RoomTipOff", "KomendantAlerted")

# EKomendantLine (ObshagaStory.h): номер в строке «Komendant says».
SAY_HEARD, SAY_LOST = 1, 4
SAY_NAMES = ["обход", "услышал", "блеф", "погоня", "ушёл", "поймал", "поверил", "не поверил", "сознался", "молчит", "утренняя проверка", "осмотр"]

TITLES = {
    "evicted": "Выселен с вещами", "king": "Король общаги", "snitch": "Главный стукач", "framer": "Мастер подстав",
    "saint": "Святой", "favorite": "Любимчик коменданта", "friend": "Настоящий друг", "thief": "Лучший вор",
    "panic": "Паникёр", "quiet": "Тихоня",
}
ROLE_TEXT = {"Rat": "Крыса", "Paranoid": "Параноик", "Resident": "Жилец"}

# СМС (ObshagaGameMode.cpp, MakeSmsFor): глагол → событие, по которому пишется правдивый слух.
SMS_VERBS = ["что-то тащил", "что-то прятал", "шумел", "крутился без дела"]
SMS_EVENT = ["ItemPickedUp", "ItemHidden", "Noise", "RoomEntered"]
SMS_TEXT = re.compile(r"^Кто-то видел, как (.+) (" + "|".join(re.escape(v) for v in SMS_VERBS) + r") \((.*)\)$")
SMS_QUIET = "Сегодня в общаге подозрительно тихо..."
# Комнаты, про которые не пишут «крутился без дела».
NOT_LURK_ROOMS = ("Corridor", "Stairs", "Bedroom")
EVICTED_WORDS = ("выселен", "собирает вещи", "домой к маме")

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
# Порядок важен: берётся первое совпадение. Необязательные хвосты — для журналов старых сборок.
PATTERNS = [
    ("event", r"^Event EGameEventType::(\w+)(?: t=(" + NUM + r"))? by (.*?) item=(\S+)(?: \(([^)]*)\))? room=(\S+) target=(.*)$"),
    ("round_started", r"^Round started: (\d+) s, (\d+) players"),
    ("round_ended", r"^Round ended$"),
    ("telemetry", r"^Telemetry saved: (.+)$"),
    ("config", r"^Round config: (.*)$"),
    ("world_reset", r"^World reset for rematch$"),
    ("personality", r"^Komendant personality: (\S+) \(night=(\d)\)(?:, patience=(" + NUM + r"), chase=(" + NUM + r"), sight=(" + NUM + r")/(" + NUM
     + r"), bluff=(" + NUM + r"), inspect=(" + NUM + r"), grudge=(\d)(?:, suspicionMult=(" + NUM + r"))?)?"),
    ("grudge", r"^Komendant grudge: personality (\S+), holds grudge (\d), caught last round (\d+)$"),
    ("grudge_on", r"^Komendant grudge on (.+): suspicion (" + NUM + r"), room (\S+) heat (" + NUM + r")$"),
    ("friday_grudge", r"^Friday grudge: (.+) \((\d+) strikes this week\)$"),
    ("says", r"^Komendant says (\d+) \(style (-?\d+), forced (\d)\): (.*?)(?:, t=(" + NUM + r"))?$"),
    ("phase", r"^Phase: ERoundPhase::(\w+)"),
    ("week", r"^Week (\d+), night (\d+)(?: \((.*)\))?(?:, planned style (-?\d+), night event (-?\d+))?$"),
    ("reset", r"^Round reset check: items off home (\d+), hidden (\d+), carried (\d+), broken devices (\d+), open doors (\d+), ghosts (\d+), evicted (\d+), strikes (\d+), suspicion (\d+)$"),
    ("room_table", r"^Room table: (\S+) «(.*)»: type ERoomType::(\w+), forbidden after curfew (\d), hiding spots (\d+)$"),
    ("role", r"^Role of (.+): (?:EPlayerRole::)?(\w+)$"),
    ("task_assigned", r"^Task (\S+) \((main|side)\) assigned to (.+?), satisfied now (\d)(?:, cond ETaskCondition::(\w+) item (\S+) room (\S+) own (\d) count (\d+) reward (" + NUM + r"))?$"),
    ("task_final", r"^Task (\S+) of (.+): (COMPLETED|FAILED)(?:, reward (" + NUM + r"))?$"),
    ("final_score", r"^Final score (.+): start (" + NUM + r"), tasks \+(" + NUM + r"), rat bonus \+(" + NUM + r"), penalties -(" + NUM + r"), final (" + NUM + r")$"),
    ("result", r"^Result (.+): role (\w+), title «(.*)», score (" + NUM + r"), strikes (\d+), evicted (\d), bot (\d)$"),
    ("results_row", r"^Results row (.+?): (.*) \|(?: (.*))?$"),
    ("results_trunc", r"^Results truncated: shown (\d+) of (\d+) players$"),
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
    ("bot_confirms", r"^Bot (.+) confirms alibi of (.+)$"),
    ("bot_left", r"^Evicted bot (.+?) left the building(?:, t=(" + NUM + r"))?$"),
    ("bot_stuck", r"^Bot (.+?) stuck near .* snapping to waypoint"),
    ("bot_breaks", r"^Bot (.+) breaks (\S+)$"),
    ("bot_repairs", r"^Bot (.+) repairs (\S+)$"),
    ("bot_goes_out", r"^Bot (.+) goes outside: (\d)$"),
    ("interro_start", r"^Interrogation started: (.+) \(contraband=(\d)\)(?:, item=(\S+) \(([^)]*)\), t=(" + NUM + r"))?$"),
    ("interro_res", r"^Interrogation of (.+) resolved: EInterrogationChoice::(\w+), strike=(\d), penalty=(" + NUM + r"), alibi=(\d+)(?:, answered=(\d), chance=("
     + NUM + r"), roll=(" + NUM + r"), contraband=(\d), strikes=(\d+), score=(" + NUM + r")(?:, suspicion=(" + NUM + r"))?, t=(" + NUM + r"))?$"),
    ("alibi_ok", r"^Alibi for (.+) by (.+) accepted: (" + NUM + r") cm, total (\d+)$"),
    ("alibi_rej", r"^Alibi for (.+) by (.+) rejected: (.*)$"),
    ("ghost", r"^(.+?) ghost (\d) \((\S+)\)( on client)?(?:: hidden (\d), blocks pawns (\d), movement (.+))?$"),
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
    ("inspect_next", r"^Komendant inspection next: (\S+) \(room (\S+)\), (\d+) left$"),
    ("morning_queue", r"^Komendant morning inspection: (\d+) hiding spots$"),
    ("tipped_room", r"^Komendant tipped about room (\S+): (\d+) hiding spots queued$"),
    ("stuck", r"^Komendant stuck near (.+?) on the way to (.+?) \(point (\d+) of (\d+)\), blocked by (\S+), snapping to waypoint(?:, t=(" + NUM + r"))?$"),
    ("snapped", r"^Komendant snapped to (.+), state EKomendantState::(\w+), t=(" + NUM + r")$"),
    ("snap_skipped", r"^Komendant: no free room at (.+), snap skipped$"),
    ("blocked_pawn", r"^Komendant blocked by (\S+) on the way to (.+), waiting \((\d+) of (\d+)\)$"),
    ("gave_up", r"^Komendant gave up the path to (.+) after (\d+) tries, state EKomendantState::(\w+)$"),
    ("test_stuck", r"^Komendant test stuck: .*, t=(" + NUM + r")$"),
    ("patrol_to", r"^Komendant patrol to (\S+) \(room (\S+), heat (" + NUM + r")\), t=(" + NUM + r")$"),
    ("returned", r"^Komendant returned (\S+) to (\S+)$"),
    ("test_chase", r"^Debug: test chase of (.+) \(item (\S+)\) from (" + NUM + r") cm, t=(" + NUM + r")(?:, visible (\d))?$"),
    ("test_catch", r"^Debug: test catch of (.+), t=(" + NUM + r")$"),
    ("test_catch_skip", r"^Debug: test catch skipped \((.*)\)$"),
    ("sees", r"^Komendant sees (.+) suspicious in (\S+): carry=(\w+) curfew=(\d) rate=(" + NUM + r") suspicion=(" + NUM + r"), t=(" + NUM + r")$"),
    ("heard", r"^Komendant heard (.+): loudness (" + NUM + r") at .*, (" + NUM + r") cm away, investigating (\d) \((.*)\), t=(" + NUM + r")$"),
    ("sortie", r"^Sortie of (.+) \((\S+)\): outside (" + NUM + r") s of (" + NUM + r"), counted (\d)$"),
    ("end_item", r"^End item (\S+) \(([^)]*)\): state EItemState::(\w+) room (\S+) home (\S+) hiddenBy (.+?) holder (.+?) spot (\S+)(?: contraband (\d) readable (\d))?$"),
    ("end_device", r"^End device (\S+) room (\S+): broken (\d) brokenBy (.+?) repairedBy (.+?) brokenAt (" + NUM + r")$"),
    ("room_tip_rej", r"^Room tip by (.+) rejected: (.*)$"),
    ("room_tip", r"^Room tip by (.+): room (\S+), t=(" + NUM + r")$"),
    ("accuse", r"^Accuse by (.+) of (.+): (right|wrong) \((?:rumor task: (\d), proof: (\S+), holder (.+), item room (\S+)\))?"),
    ("hid_item", r"^(\S+) hid (\S+) in (\S+): player (.+), room (\S+), id=(\S+), contraband=(\d)$"),
    ("took_out", r"^(\S+) took (\S+) out of (\S+)$"),
    ("hide_interrupted", r"^(.+) (hide|search) of (\S+) in (\S+) interrupted: (.+)$"),
    ("notice_bot", r"^Notice for (.+) \(no screen\): (.*)$"),
    ("notice_shown", r"^Notice shown to (.+): (.*)$"),
    ("lives", r"^(.+) lives in (\S+) \((?:pawn (\S+), )?spawned at"),
    ("layout", r"^Results layout: variant (-?\d+) \(two columns (\d), captions (\d), line (" + NUM + r")\), height (" + NUM + r") of (" + NUM
     + r"), fits (\d), players (\d+), chronicle (\d+), canvas (" + NUM + r")x(" + NUM + r")$"),
]
PATTERNS = [(kind, re.compile(rx)) for kind, rx in PATTERNS]
# Виды, по которым видно, что игрок — бот.
BOT_KINDS = ("bot_answers", "bot_tipped", "bot_hid", "bot_runs_hide", "bot_confirms", "bot_left", "bot_stuck", "bot_breaks", "bot_repairs",
             "bot_goes_out", "notice_bot")

PREFIX = re.compile(r"^(?:\[(\d{4}\.\d\d\.\d\d-\d\d\.\d\d\.\d\d:\d{3})\])?(?:\[\s*\d+\])?LogObshaga: (?:(Verbose|VeryVerbose|Display|Warning|Error|Fatal): )?(.*)$")
CRASH = re.compile(r"(=== Critical error|Assertion failed|Fatal error!|Unhandled Exception|appError called|LogObshaga: Fatal:)")
ENSURE = re.compile(r"Ensure condition failed")
VECTOR = re.compile(r"([XYZ])=(" + NUM + r")")


def fnum(text, default=None):
    try:
        return float(text)
    except (TypeError, ValueError):
        return default


def clip(text, size=150):
    text = text.strip()
    return text if len(text) <= size else text[: size - 1] + "…"


def vec(text):
    """Точка из FVector::ToCompactString (V(X=1.00, Z=3.00) — нулевые оси не пишутся)."""
    out = {"X": 0.0, "Y": 0.0, "Z": 0.0}
    for axis, value in VECTOR.findall(text or ""):
        out[axis] = float(value)
    return out


def dist2d(a, b):
    return math.hypot(a["X"] - b["X"], a["Y"] - b["Y"])


class Line(object):
    __slots__ = ("file", "no", "ts", "level", "text", "kind", "m", "round", "idx")

    def __init__(self, file, no, ts, level, text):
        self.file, self.no, self.ts, self.level, self.text = file, no, ts, level, text
        self.kind, self.m, self.round, self.idx = None, None, None, None

    def g(self, index):
        return self.m.group(index)

    def short(self):
        stamp = self.ts[11:19].replace(".", ":") if self.ts else "стр. %d" % self.no
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
        self.by_kind = {}
        self._events = {}
        self._bots = None

    def label(self):
        return "%s, раунд %d" % (os.path.basename(self.file), self.index)

    def of(self, kind, lines=None):
        if lines is None:
            return self.by_kind.get(kind, [])
        return [line for line in lines if line.kind == kind]

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
        if line.kind == "event" and line.g(2) is not None:
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
        for other in self.of("personality"):
            if line is not None and line.round is self and other.idx > line.idx:
                break
            found = other
        return found

    def players(self):
        """Имена жильцов раунда: по строкам итогов, телеметрии, ролей или «lives in» перед раундом."""
        names = [l.g(1) for l in self.of("result")]
        if not names and self.telemetry:
            names = [p.get("name") for p in self.telemetry.get("players", [])]
        if not names:
            names = [l.g(1) for l in self.of("role")]
        if not names:
            names = [l.g(1) for l in self.pre if l.kind == "lives"]
        return names

    def num_players(self):
        started = self.of("round_started")
        return int(started[0].g(2)) if started else len(self.players())

    def duration(self):
        if self.telemetry and isinstance(self.telemetry.get("durationSeconds"), (int, float)):
            return float(self.telemetry["durationSeconds"])
        ended = self.of("round_ended")
        return self.t_of(ended[0]) if ended else None

    def is_bot(self, name):
        """True — бот, False — человек, None — неизвестно."""
        if self._bots is None:
            flags = {}
            for line in self.of("result"):
                flags[line.g(1)] = line.g(7) == "1"
            for player in (self.telemetry or {}).get("players", []):
                if "bot" in player:
                    flags.setdefault(player.get("name"), bool(player.get("bot")))
            for kind in BOT_KINDS:
                for line in self.of(kind):
                    flags.setdefault(line.g(1), True)
            self._bots = flags
        return self._bots.get(name)

    def room(self, room_id):
        """(тип, запретна ли после отбоя, тайников, название) или None."""
        return self.room_table.get(room_id)


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
                room_table[line.g(1)] = (line.g(3), line.g(4) == "1", int(line.g(5)), line.g(2))

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
        for index, line in enumerate(rnd.lines):
            line.idx = index
            if line.kind:
                rnd.by_kind.setdefault(line.kind, []).append(line)
        for line in rnd.of("config"):
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
    if upto_telemetry not in rnd._events:
        out = []
        for line in rnd.events(upto_telemetry):
            kind, t, by, actor, item_id, room, target = event_fields(line)
            exact = t is not None
            if t is None:
                t = rnd.t_of(line)
            out.append({"type": kind, "t": t, "exact": exact, "by": by, "actor": actor, "id": item_id, "room": room, "target": target, "line": line})
        rnd._events[upto_telemetry] = out
    return rnd._events[upto_telemetry]


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


def _position(rnd, line):
    if line.round is rnd and line.idx is not None:
        return line.idx
    for index, other in enumerate(rnd.lines):
        if other is line:
            return index
    return None


def next_after(rnd, line, kinds, stop_kinds=(), predicate=None, limit=None):
    """Первая строка нужного вида после line (до строки из stop_kinds; limit — не дальше стольких строк)."""
    start = _position(rnd, line)
    if start is None:
        return None
    end = len(rnd.lines) if limit is None else min(len(rnd.lines), start + 1 + limit)
    for other in rnd.lines[start + 1: end]:
        if other.kind in stop_kinds:
            return None
        if other.kind in kinds and (predicate is None or predicate(other)):
            return other
    return None


def prev_before(rnd, line, kinds, predicate=None, limit=None):
    """Последняя строка нужного вида до line (limit — не дальше стольких строк назад)."""
    stop = _position(rnd, line)
    if stop is None:
        return None
    begin = 0 if limit is None else max(0, stop - limit)
    for other in reversed(rnd.lines[begin: stop]):
        if other.kind in kinds and (predicate is None or predicate(other)):
            return other
    return None


def between(rnd, first, second):
    """Строки раунда строго между двумя строками."""
    a, b = _position(rnd, first), _position(rnd, second)
    if a is None or b is None:
        return []
    return rnd.lines[a + 1: b]


def close(a, b, eps):
    return a is not None and b is not None and abs(a - b) <= eps


def tline(rnd, line, group=None):
    """Секунды раунда строки: из нужной группы, иначе из «t=» или метки времени."""
    if group is not None and line.g(group) is not None:
        return fnum(line.g(group))
    return rnd.t_of(line)


def forced_by_test(rnd, who, t, window=3.0):
    """Строка -TestChaseAt/-TestCatch*At для этого игрока не дальше window секунд до t (поимка/погоня «по ключу»)."""
    for line in rnd.of("test_chase") + rnd.of("test_catch"):
        if line.g(1) != who:
            continue
        at = fnum(line.g(4)) if line.kind == "test_chase" else fnum(line.g(2))
        if at is not None and t is not None and t - window - 0.05 <= at <= t + 0.05:
            return line
    return None


def interrogation_windows(rnd):
    """[(кто, начало, конец)] допросов раунда (конец — итог допроса)."""
    out = []
    for start in rnd.of("interro_start"):
        who = start.g(1)
        res = next_after(rnd, start, ("interro_res",), predicate=lambda o: o.g(1) == who)
        out.append((who, tline(rnd, start, 5), tline(rnd, res, 13) if res is not None else None))
    return out


def personality_numbers(rnd, line):
    found = rnd.personality_at(line)
    if found is None or found.g(3) is None:
        return None
    return {"name": found.g(1), "patience": fnum(found.g(3)), "grudge": found.g(9) == "1", "mult": fnum(found.g(10), 1.0),
            "style": personality_style(found.g(1))}


def personality_style(name):
    """Как ObshagaStory::StyleFromPersonalityName: 0 — строгий, 1 — рассеянный, 2 — мстительный."""
    if "Strict" in name:
        return 0
    return 2 if "Vindictive" in name else 1


def notice_before(rnd, line, who, words, limit=4):
    """Сообщение боту (или показанное человеку) с этими словами в нескольких строках до line."""
    return prev_before(rnd, line, ("notice_bot", "notice_shown"), lambda o: o.g(1) == who and any(w in o.g(2) for w in words), limit=limit)


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


def task_final_of(rnd, task_id, owner):
    return next((l for l in rnd.of("task_final") if l.g(1) == task_id and l.g(2) == owner), None)


# ---------------------------------------------------------------- проверки: раунд целиком

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
            # Вылет поймает «Журнал без падений»; закрытое окно или остановленный PIE — не ошибка игры.
            out.append(Result(SKIP, rnd.lines[-1] if rnd.lines else None, "журнал оборвался посреди раунда — раунд не в счёт", rnd))
            continue
        started = rnd.of("round_started")
        players = started[0].g(2) if started else "?"
        tele = os.path.basename(rnd.telemetry["_path"]) if rnd.telemetry else ("файл %s не найден" % rnd.telemetry_name if rnd.telemetry_name else "нет строки Telemetry saved")
        out.append(Result(PASS, rnd.of("round_ended")[0], "%s жильцов, телеметрия: %s" % (players, tele), rnd))
    return out


def check_round_structure(ctx):
    """Порядок главных строк раунда, число итогов и ролей, у каждого задания — один итог, нет ошибок LogObshaga."""
    out = []
    for rnd in ctx.rounds:
        if not rnd.finished:
            continue
        problems = []
        first = {}
        for line in rnd.lines:
            key = line.kind if line.kind != "phase" else "phase_" + line.g(1)
            first.setdefault(key, line)
        # Порядок в коде: событие RoundStarted → «Phase: Evening» → «Round config» → «Round started» → Night → Morning → «Telemetry saved» → «Round ended».
        order = ["phase_Evening", "config", "round_started", "phase_Night", "phase_Morning", "telemetry", "round_ended"]
        present = [key for key in order if key in first]
        for key in ("phase_Evening", "round_started", "phase_Night", "phase_Morning", "round_ended"):
            if key not in first:
                problems.append("нет строки %s" % key.replace("phase_", "Phase: "))
        if "telemetry" not in first:
            problems.append("нет строки «Telemetry saved»")
        positions = [first[key].idx for key in present]
        if positions != sorted(positions):
            problems.append("порядок строк: %s" % " → ".join(sorted(present, key=lambda k: first[k].idx)))
        players = rnd.num_players()
        results = rnd.of("result")
        if results and len(results) != players:
            problems.append("строк Result %d, жильцов %d" % (len(results), players))
        roles = rnd.of("role")
        if roles and len(roles) != players:
            problems.append("строк «Role of» %d, жильцов %d" % (len(roles), players))
        finals = {}
        for line in rnd.of("task_final"):
            finals[(line.g(1), line.g(2))] = finals.get((line.g(1), line.g(2)), 0) + 1
        for line in rnd.of("task_assigned"):
            count = finals.get((line.g(1), line.g(3)), 0)
            if count != 1:
                problems.append("задание %s у %s: итогов %d" % (line.g(1), line.g(3), count))
                break
        errors = [l for l in rnd.lines if l.level == "Error"]
        if errors:
            problems.append("ошибок LogObshaga: %d (первая: %s)" % (len(errors), clip(errors[0].text, 60)))
        note = "%d жильцов, порядок строк верный, у %d заданий по одному итогу" % (players, len(rnd.of("task_assigned")))
        out.append(Result(FAIL if problems else PASS, errors[0] if errors else first.get("round_ended"), "; ".join(problems) or note, rnd))
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
            out.append(Result(FAIL, line, "%s у %s: по журналу выходит %s: %s" % (task_id, owner, "выполнено" if verdict else "провалено", note), rnd))
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
        unknown = set()
        for line in rnd.of("evicted"):
            if line.g(3) is not None:
                penalties[line.g(1)] = penalties.get(line.g(1), 0) + fnum(line.g(3)) - fnum(line.g(4))
            elif rnd.config_known:
                penalties[line.g(1)] = penalties.get(line.g(1), 0) + rnd.config["evictPenalty"]
            else:
                unknown.add(line.g(1))  # журнал старой сборки: штраф за выселение не записан
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
            if name not in unknown and abs(penalties.get(name, 0) - pen) > 0.5:
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
                bad = (line, "%s: %s" % (name, "; ".join(problems)))
                break
        if bad:
            out.append(Result(FAIL, bad[0], bad[1], rnd))
        else:
            out.append(Result(PASS, finals[0], "%d игроков: очки сходятся с заданиями, штрафами, бонусом и телеметрией" % len(finals), rnd))
    return out


# ---------------------------------------------------------------- погони: побег, укрытия

def chase_start_of(rnd, end):
    """Начало погони, которой кончилась строка end (того же игрока)."""
    who = end.g(1)
    return prev_before(rnd, end, ("chase_start",), lambda o: o.g(1) == who)


def forced_chase(rnd, start):
    """Строка -TestChaseAt / -TestCatch*At, вызвавшая эту погоню (то же t), или None."""
    if start is None:
        return None
    t0 = fnum(start.g(9))
    for line in rnd.of("test_chase"):
        if line.g(1) == start.g(1) and close(fnum(line.g(4)), t0, 0.5):
            return line
    for line in rnd.of("test_catch"):
        if line.g(1) == start.g(1) and close(fnum(line.g(2)), t0, 0.5):
            return line
    return None


def escape_cases(ctx):
    """Каждый «оторвался» (reason=lost_sight): (раунд, строка конца, засчитан ли, нарушения, пояснение)."""
    cases = []
    for rnd in ctx.rounds:
        for end in rnd.of("chase_end"):
            if end.g(3) != "lost_sight":
                continue
            who, seen, unseen, suspicion = end.g(1), fnum(end.g(5)), fnum(end.g(6)), fnum(end.g(17))
            start = chase_start_of(rnd, end)
            forced = forced_chase(rnd, start)
            if seen < ESCAPE_MIN_SEEN:
                cases.append((rnd, end, False, [], "%s «оторвался», но Палыч ни разу его не видел (seen %.1f с)%s" % (
                    who, seen, ", погоня по -TestChaseAt" if forced is not None else "")))
                continue
            problems = []
            if start is None:
                problems.append("нет строки начала погони за %s" % who)
            else:
                caught = next_after(rnd, start, ("interro_start",), predicate=lambda o: o.g(1) == who)
                if caught is not None and caught.idx < end.idx:
                    problems.append("между началом и концом погони %s успел попасть на допрос" % who)
            pers = personality_numbers(rnd, end)
            if pers:
                if unseen < pers["patience"] - 0.05:
                    problems.append("бросил погоню через %.1f с без цели, терпение %.1f" % (unseen, pers["patience"]))
                elif unseen > pers["patience"] + 1.5:
                    problems.append("ждал %.1f с при терпении %.1f" % (unseen, pers["patience"]))
            if rnd.config_known and abs(suspicion - rnd.config["afterStrike"]) > 0.5:
                problems.append("подозрение после побега %g, ждали %g" % (suspicion, rnd.config["afterStrike"]))
            state = next_after(rnd, end, ("state",))
            if state is not None and state.g(1) != "Investigate":
                problems.append("дальше состояние %s, а не Investigate" % state.g(1))
            if rnd.is_bot(who) and notice_before(rnd, end, who, ("Оторвался",)) is None:
                problems.append("боту %s не пришло «Оторвался!»" % who)
            note = "%s: погоня %s с, на глазах %.1f с, пробежал %.0f м, %s → %s, не видел %.1f с (терпение %s)%s" % (
                who, end.g(4), seen, fnum(end.g(7)) / 100, end.g(8), end.g(9), unseen, "%.1f" % pers["patience"] if pers else "?",
                ", погоня по ключу -TestChaseAt" if forced is not None else "")
            cases.append((rnd, end, True, problems, note))
    return cases


def check_escape(ctx):
    out = []
    blind = []
    for rnd, end, counted, problems, note in ctx.cache("escape", escape_cases):
        if counted:
            out.append(Result(FAIL if problems else PASS, end, "; ".join(problems) or note, rnd))
        else:
            blind.append((rnd, end, note))
    if blind and not out:
        rnd, end, note = blind[0]
        out.append(Result(SKIP, end, "%s; таких «побегов вслепую» %d — не в счёт" % (note, len(blind)), rnd))
    return out


def check_escape_speech(ctx):
    """После настоящего побега Палыч должен сказать реплику «ушёл» (EKomendantLine::Lost)."""
    out = []
    for rnd, end, counted, _, _ in ctx.cache("escape", escape_cases):
        if not counted:
            continue
        if not any(r.of("says") for r in ctx.rounds):
            out.append(Result(SKIP, end, "нет строк «Komendant says» — журнал старой сборки", rnd))
            break
        t = fnum(end.g(19))
        said = [l for l in rnd.of("says") if l.idx > end.idx and close(tline(rnd, l, 5), t + 1.0, 1.0)]
        if any(int(l.g(1)) == SAY_LOST for l in said):
            line = next(l for l in said if int(l.g(1)) == SAY_LOST)
            out.append(Result(PASS, line, "%s оторвался, Палыч: «%s»" % (end.g(1), clip(line.g(4), 60)), rnd))
        else:
            what = ", ".join("%s «%s»" % (SAY_NAMES[int(l.g(1))] if int(l.g(1)) < len(SAY_NAMES) else l.g(1), clip(l.g(4), 40)) for l in said) or "ничего"
            out.append(Result(FAIL, said[0] if said else end, "%s оторвался, а Палыч сказал: %s — реплики «ушёл» нет "
                              "(после погони в Investigate должна звучать «ушёл»)" % (end.g(1), what), rnd))
    return out


def check_chase_pairs(ctx):
    """У каждой погони есть строка конца до следующей смены состояния Палыча."""
    out = []
    for rnd in ctx.rounds:
        starts = rnd.of("chase_start")
        if not starts:
            continue
        problems = []
        for start in starts:
            who = start.g(1)
            follow = next_after(rnd, start, ("chase_end", "state", "round_ended"))
            if follow is None:
                continue  # журнал оборвался
            if follow.kind == "state":
                problems.append((start, "погоня за %s кончилась без строки «chase end» (дальше %s)" % (who, follow.g(1))))
            elif follow.kind == "chase_end" and follow.g(1) != who and follow.g(3) != "target_gone":
                problems.append((follow, "погоня за %s, а конец про %s" % (who, follow.g(1))))
        if problems:
            out.append(Result(FAIL, problems[0][0], problems[0][1], rnd))
        else:
            reasons = {}
            for end in rnd.of("chase_end"):
                reasons[end.g(3)] = reasons.get(end.g(3), 0) + 1
            out.append(Result(PASS, starts[0], "погонь %d, у всех есть конец: %s" % (len(starts), ", ".join("%s %d" % kv for kv in sorted(reasons.items()))), rnd))
    return out


def hide_results(ctx, exit_flag):
    out = []
    for rnd in ctx.rounds:
        events = ev_list(rnd)
        for end in rnd.of("chase_end"):
            if end.g(3) not in ("hid_seen", "hid_unseen") or end.g(16) != exit_flag:
                continue
            who, spot, t = end.g(1), end.g(14), fnum(end.g(19))
            problems = []
            entered = [e for e in events if e["by"] == who and e["type"] in ("PlayerHid", "LeftBuilding") and t - 0.5 <= e["t"] <= t + 0.05]
            if not entered:
                problems.append("нет события PlayerHid/LeftBuilding у %s за полсекунды до конца погони" % who)
            state = next_after(rnd, end, ("state",))
            if end.g(3) == "hid_unseen":
                if rnd.config_known and abs(fnum(end.g(17)) - rnd.config["afterStrike"]) > 0.5:
                    problems.append("подозрение %s, ждали %g" % (end.g(17), rnd.config["afterStrike"]))
                if state is not None and state.g(1) != "Investigate":
                    problems.append("дальше %s, а не Investigate" % state.g(1))
                if rnd.is_bot(who) and notice_before(rnd, end, who, ("потерял",)) is None:
                    problems.append("боту не пришло «Палыч тебя потерял»")
                # Пока сидит в укрытии, его не видят и не ловят; погоня за ним — только если его нашли обыском.
                for line in rnd.lines[end.idx + 1:]:
                    if line.kind == "event" and line.g(3) == who and line.g(1) == "PlayerLeftHiding":
                        break
                    if line.kind == "event" and line.g(3) == who and line.g(1) in SEEN_EVENTS:
                        problems.append("сидя в укрытии, получил %s (t=%s)" % (line.g(1), line.g(2)))
                        break
                    if line.kind == "chase_start" and line.g(1) == who:
                        search = prev_before(rnd, line, ("searched",), limit=6)
                        if search is None or search.g(3) != who:
                            problems.append("погоня за %s, пока он в укрытии, без обыска" % who)
                        break
                out.append(Result(FAIL if problems else PASS, end, "; ".join(problems) or "%s спрятался в %s незаметно, Палыч ищет вокруг" % (who, spot), rnd))
                continue
            # Видел, как залез: идёт открывать именно это укрытие.
            if state is not None and state.g(1) != "Inspect":
                problems.append("дальше %s, а не Inspect" % state.g(1))
            follow = None
            interrupted = None
            for line in rnd.lines[end.idx + 1:]:
                if line.kind == "state" and line.g(1) != "Inspect":
                    interrupted = line
                    break
                if line.kind in ("searched", "not_reach") and line.g(1) == spot:
                    follow = line
                    break
                if line.kind == "round_ended":
                    break
            if follow is None:
                if interrupted is not None and not problems:
                    out.append(Result(SKIP, interrupted, "видел, как %s залез в %s, но обыск прервало (%s)" % (who, spot, interrupted.g(1)), rnd))
                elif not problems:
                    out.append(Result(SKIP, end, "раунд кончился раньше обыска", rnd))
                else:
                    out.append(Result(FAIL, end, "; ".join(problems), rnd))
                continue
            if follow.kind == "not_reach":
                problems.append("видел, как %s залез в %s, но не дошёл до укрытия" % (who, spot))
            elif follow.g(3) == who:
                chase = next_after(rnd, follow, ("chase_start",), limit=8)
                if chase is None or chase.g(1) != who:
                    problems.append("вытащил %s, но погоня не началась" % who)
                elif fnum(chase.g(8)) < 99.5:
                    problems.append("погоня за вытащенным с подозрением %s, а не 100" % chase.g(8))
            else:
                left = [e for e in events if e["by"] == who and e["type"] == "PlayerLeftHiding" and end.idx < e["line"].idx < follow.idx]
                if not left:
                    problems.append("%s из %s не выходил, а обыск никого не нашёл (player=%s)" % (who, spot, follow.g(3)))
            if follow.kind == "not_reach":
                what = "не дошёл до укрытия"
            else:
                what = "вытащил %s" % who if follow.g(3) == who else "внутри уже пусто (%s вышел сам)" % who
            out.append(Result(FAIL if problems else PASS, follow, "; ".join(problems) or "видел, как %s залез в %s, открыл: %s" % (who, spot, what), rnd))
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


def check_xray(ctx):
    """Палыч не видит сквозь мебель: пока жилец в укрытии (или на улице), событий «заметил/поймал» о нём нет."""
    out = []
    for rnd in ctx.rounds:
        events = ev_list(rnd)
        if not events:
            continue
        inside = {}
        intervals = 0
        bad = None
        for e in events:
            who = e["by"]
            if e["type"] in ("PlayerHid", "LeftBuilding"):
                inside[who] = e["t"]
                intervals += 1
            elif e["type"] == "PlayerLeftHiding":
                inside.pop(who, None)
            elif e["type"] in SEEN_EVENTS and who in inside and e["t"] > inside[who] + 0.05 and bad is None:
                bad = (e, inside[who])
        if bad:
            e, since = bad
            out.append(Result(FAIL, e["line"], "%s в укрытии с t=%.1f, а в t=%.1f — %s" % (e["by"], since, e["t"], e["type"]), rnd))
        elif intervals:
            out.append(Result(PASS, None, "прятались %d раз, ни одного «заметил/поймал» изнутри укрытия" % intervals, rnd))
    return out


# ---------------------------------------------------------------- обыски и запрещёнка

def contraband_finds(rnd):
    """[(строка «searched» с находкой, что лежало по журналу, нарушения)] — все найденные в тайниках запрещёнки раунда."""
    out = []
    contents = {}
    for line in rnd.lines:
        if line.kind == "hid_item":
            contents[line.g(3)] = {"actor": line.g(2), "player": line.g(4), "room": line.g(5), "id": line.g(6), "contraband": line.g(7) == "1", "line": line}
        elif line.kind == "took_out":
            if contents.get(line.g(3), {}).get("actor") == line.g(2):
                contents.pop(line.g(3), None)
        elif line.kind == "searched":
            spot, found = line.g(1), line.g(2)
            held = contents.get(spot)
            if found not in ("None", ""):
                contents.pop(spot, None)
                out.append((line, held, []))
            elif held and held["contraband"]:
                out.append((line, held, ["обыскал %s, где лежит запрещёнка %s (прятал %s), и не нашёл" % (spot, held["actor"], held["player"])]))
    return out


def judge_find(rnd, line, held):
    """Нарушения при находке запрещёнки в тайнике: изъятие, на кого записали, подстава, исход «спрячь запрещёнку»."""
    problems = []
    spot, found, room, finder = line.g(1), line.g(2), line.g(4), line.g(3)
    nxt = next_after(rnd, line, ("searched",))
    stop = nxt.idx if nxt is not None else len(rnd.lines)
    window = rnd.lines[line.idx + 1: stop]
    confiscated = next((l for l in window if l.kind == "event" and l.g(1) == "ContrabandConfiscated" and l.g(4) == found), None)
    detail = next((l for l in window if l.kind == "contraband_found" and l.g(1) == found), None)
    if confiscated is None:
        problems.append("нашёл %s, но события ContrabandConfiscated нет" % found)
    else:
        if room and confiscated.g(6) != room:
            problems.append("изъятие записано в комнату %s, тайник в %s" % (confiscated.g(6), room))
        if confiscated.g(7) not in ("-", ""):
            problems.append("у изъятия из тайника есть цель %s (так пишут только отобранное из рук)" % confiscated.g(7))
    if held is None:
        problems.append("в журнале нет строки, как %s прятали в %s" % (found, spot))
    elif held["actor"] != found:
        problems.append("по журналу в %s лежит %s, а нашёл %s" % (spot, held["actor"], found))
    if detail is None:
        problems.append("нет строки «Contraband %s in %s»" % (found, spot))
        return problems, None
    hider, residents, blamed, framed = detail.g(4), detail.g(5), detail.g(6), detail.g(7) == "1"
    if held is not None and hider != held["player"] and hider != "None":
        problems.append("прятал %s, а записано на прятавшего %s" % (held["player"], hider))
    resident_list = [] if residents == "-" else residents.split("+")
    own = rnd.homes.get(hider) == detail.g(3) or residents == "-" or finder == hider
    if own:
        if framed or blamed != hider:
            problems.append("прятал у себя/в общей комнате, а записано на %s (подстава=%d)" % (blamed, framed))
    else:
        if not framed or blamed != residents:
            problems.append("подброшено в чужую комнату, а записано на %s (подстава=%d, жильцы %s)" % (blamed, framed, residents))
        if hider != "None":
            framed_events = [l for l in window if l.kind == "event" and l.g(1) == "PlayerFramed" and l.g(3) == hider]
            if sorted(l.g(7) for l in framed_events) != sorted(resident_list):
                problems.append("событий PlayerFramed от %s: %s, жильцы %s" % (hider, [l.g(7) for l in framed_events], residents))
        if hider == blamed:
            problems.append("подстава записана на самого прятавшего")
    # В конце раунда изъятая не лежит в тайнике от его имени, и «спрячь запрещёнку» провалено, если другой спрятанной нет.
    later_hide = next((l for l in rnd.lines[line.idx + 1:] if l.kind == "hid_item" and l.g(2) == found), None)
    end = next((l for l in rnd.of("end_item") if l.g(1) == found), None)
    if later_hide is None and end is not None and end.g(3) == "Hidden" and end.g(6) == hider:
        problems.append("изъятая %s в конце раунда снова числится спрятанной у %s" % (found, hider))
    task = task_final_of(rnd, "hide_contraband", hider)
    if task is not None:
        others = [l for l in rnd.of("end_item") if l.g(9) == "1" and l.g(3) == "Hidden" and l.g(6) == hider]
        if not others and task.g(3) != "FAILED":
            problems.append("запрещёнку %s изъяли, другой спрятанной нет, а «спрячь запрещёнку» засчитано" % hider)
    return problems, detail


def check_morning_contraband(ctx):
    out = []
    for rnd in ctx.rounds:
        for line, held, problems in contraband_finds(rnd):
            phase = (line.g(5) or "").replace("ERoundPhase::", "") or rnd.phase_at(line)
            if problems:
                if phase == "Morning":
                    out.append(Result(FAIL, line, problems[0], rnd))
                continue
            found_problems, detail = judge_find(rnd, line, held)
            if found_problems:
                out.append(Result(FAIL, line, "; ".join(found_problems), rnd))
            elif phase == "Morning":
                blame = (" — записали на %s%s" % (detail.g(6), " (подстава)" if detail.g(7) == "1" else "")) if detail else ""
                queued = " (по утренней очереди)" if line.g(6) == "1" else ""
                out.append(Result(PASS, line, "утренний обыск %s (комната %s)%s нашёл %s%s" % (line.g(1), line.g(4) or "?", queued, line.g(2), blame), rnd))
    return out


def check_unreachable_spots(ctx):
    """Тайник из утренней очереди, до которого Палыч не дошёл в двух раундах и больше, — ошибка карты (там всё переживает проверку)."""
    out = []
    misses = {}
    for rnd in ctx.rounds:
        queue = rnd.of("morning_queue")
        if not queue:
            continue
        searched, missed = 0, []
        for line in rnd.lines[queue[0].idx:]:
            if line.kind == "searched" and (line.g(5) or "").endswith("Morning"):
                searched += 1
            elif line.kind == "not_reach":
                missed.append(line)
                misses.setdefault(line.g(1), []).append((rnd, line))
        names = ", ".join(sorted(set(l.g(1) for l in missed)))
        out.append(Result(PASS, queue[0], "утром в очереди %s тайников: обыскано %d, не дошёл %d%s" % (
            queue[0].g(1), searched, len(missed), (" (%s)" % names) if names else ""), rnd))
    for spot, hits in sorted(misses.items()):
        rounds = set(id(rnd) for rnd, _ in hits)
        if len(rounds) >= 2:
            rnd, line = hits[-1]
            out.append(Result(FAIL, line, "до тайника %s Палыч не доходит в %d раундах — запрещёнка там всегда переживает проверку" % (spot, len(rounds)), rnd))
    return out


# ---------------------------------------------------------------- допрос

def interrogation_cases(ctx):
    """Каждый итог допроса: (раунд, строка, вид, список нарушений, пояснение)."""
    cases = []
    for rnd in ctx.rounds:
        cfg = rnd.config
        events = ev_list(rnd)
        strikes = {}
        score = dict((l.g(1), fnum(l.g(2))) for l in rnd.of("final_score"))
        duration = rnd.duration()
        for line in rnd.lines:
            if line.kind == "evicted" and line.g(4) is not None:
                score[line.g(1)] = fnum(line.g(4))
                continue
            if line.kind != "interro_res":
                continue
            who, choice, strike, penalty, alibi = line.g(1), line.g(2), line.g(3) == "1", fnum(line.g(4)), int(line.g(5))
            answered = line.g(6)
            t = tline(rnd, line, 13)
            start = prev_before(rnd, line, ("interro_start",), lambda o: o.g(1) == who)
            t0 = tline(rnd, start, 5) if start is not None else None
            took = (t - t0) if (t is not None and t0 is not None) else None
            bot = rnd.is_bot(who)
            problems = []
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
            # Сколько длился допрос: ответ бота — через 1,5–4 с, ложь и молчание по таймауту — ровно окно допроса.
            if took is not None and rnd.config_known:
                window = cfg["interrogation"]
                if kind == "timeout" and took < window - 0.5 and duration is not None and t >= duration - 1.0:
                    kind = "cut"
                elif kind in ("lie_ok", "lie_fail") and abs(took - window) > 0.3:
                    problems.append("ложь решена через %.1f с, а окно %g с" % (took, window))
                elif kind == "timeout" and abs(took - window) > 0.5:
                    problems.append("молчание по таймауту через %.1f с, а окно %g с" % (took, window))
                elif kind in ("confess", "silent"):
                    if bot and not BOT_ANSWER[0] <= took <= BOT_ANSWER[1]:
                        problems.append("бот ответил через %.1f с (ждали %.1f–%.1f)" % (took, BOT_ANSWER[0], BOT_ANSWER[1]))
                    elif not bot and took > window + 0.3:
                        problems.append("ответ через %.1f с, дольше окна %g с" % (took, window))
            if kind == "timeout":
                if bot:
                    problems.append("бот не ответил за время допроса")
                elif start is not None and forced_by_test(rnd, who, t0, window=30.0) is None and bot is None:
                    problems.append("не ответил, а поимки по ключу -TestCatchHostAt перед допросом нет")
            if answered == "0" and choice != "Silent":
                problems.append("ответа не было, а записан %s" % choice)
            # Страйки по порядку и подозрение после допроса.
            strikes[who] = strikes.get(who, 0) + (1 if strike else 0)
            if line.g(10) is not None and int(line.g(10)) != strikes[who]:
                problems.append("страйков %s, по журналу раунда %d" % (line.g(10), strikes[who]))
            if line.g(12) is not None and rnd.config_known and abs(fnum(line.g(12)) - cfg["afterStrike"]) > 0.5:
                problems.append("подозрение после допроса %s, ждали %g" % (line.g(12), cfg["afterStrike"]))
            # Очки: минус штраф от прежних (удачная ложь — без изменений).
            if line.g(11) is not None:
                now = fnum(line.g(11))
                if who in score and score[who] is not None and abs(score[who] - penalty - now) > 0.5:
                    problems.append("очки %g, ждали %g − %g" % (now, score[who], penalty))
                score[who] = now
            # Событие итога, ответ бота и алиби.
            expected_event = {"confess": "InterrogationConfessed", "lie_ok": "InterrogationLieSucceeded", "lie_fail": "InterrogationLieFailed"}.get(kind, "InterrogationSilent")
            event = next_after(rnd, line, ("event",), predicate=lambda e: e.g(3) == who and e.g(1) in INTERROGATION_EVENTS)
            if event is not None and event.g(1) != expected_event:
                problems.append("событие %s вместо %s" % (event.g(1), expected_event))
            if bot and answered == "1":
                if kind in ("lie_ok", "lie_fail"):
                    answer = prev_before(rnd, line, ("bot_answers",), lambda o: o.g(1) == who)
                    if answer is not None and start is not None and answer.idx < start.idx:
                        answer = None
                else:
                    answer = next_after(rnd, line, ("bot_answers",), stop_kinds=("interro_start",), predicate=lambda o: o.g(1) == who, limit=40)
                if answer is None or answer.g(2) != choice:
                    problems.append("нет строки «Bot %s answers %s»" % (who, choice))
            confirmed = [e for e in events if e["type"] == "AlibiConfirmed" and e["target"] == who and t is not None and abs(e["t"] - t) <= 0.15]
            if kind != "lie_ok" and confirmed:
                problems.append("алиби засчитано (%s), хотя ложь не удалась" % confirmed[0]["by"])
            if kind == "lie_ok" and start is not None:
                helpers = [l.g(2) for l in between(rnd, start, line) if l.kind == "alibi_ok" and l.g(1) == who]
                missing = [h for h in helpers if not any(e["by"] == h for e in confirmed)]
                if missing:
                    problems.append("алиби %s принято, а AlibiConfirmed нет" % ", ".join(missing))
            note = "%s%s: %s, штраф %g, страйков %s, алиби %d" % (who, " (бот)" if bot else "", choice, penalty, line.g(10) or "?", alibi)
            if took is not None:
                note += ", %.1f с" % took
            if choice == "Lie" and answered is not None:
                note += ", шанс %s, бросок %s" % (line.g(7), line.g(8))
            if line.g(12) is not None:
                note += ", подозрение %s" % line.g(12)
            cases.append((rnd, line, kind, problems, note))
    return cases


def interrogation_check(kind):
    def run(ctx):
        out = []
        cut = 0
        for rnd, line, case_kind, problems, note in ctx.cache("interro", interrogation_cases):
            if case_kind == kind:
                out.append(Result(FAIL if problems else PASS, line, "; ".join(problems) or note, rnd))
            cut += 1 if case_kind == "cut" else 0
        if kind == "timeout" and cut and not out:
            out.append(Result(SKIP, None, "допросов, прерванных концом раунда: %d — не в счёт" % cut))
        return out
    return run


def check_lie_statistics(ctx):
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
    if len(lies) < 10:
        return [Result(SKIP, None, note + " — для вывода нужно 10+ случаев")]
    if abs(z) > 3.5:
        return [Result(FAIL, None, note + " — шанс в игре не совпадает с настройкой")]
    return [Result(PASS, None, note)]


def check_bot_answer_mix(ctx):
    """Доли ответов ботов против весов из Round config (botConfess/botLie/botSilent)."""
    counts = {"Confess": 0, "Lie": 0, "Silent": 0}
    weights = None
    for rnd in ctx.rounds:
        for line in rnd.of("bot_answers"):
            if line.g(2) in counts:
                counts[line.g(2)] += 1
        if rnd.config_known and "botLie" in rnd.config and weights is None:
            weights = (rnd.config["botConfess"], rnd.config["botLie"], rnd.config["botSilent"])
    total = sum(counts.values())
    if total == 0:
        return []
    note = "ответы ботов: сознались %d, соврали %d, молчали %d" % (counts["Confess"], counts["Lie"], counts["Silent"])
    if weights is None or sum(weights) <= 0:
        return [Result(SKIP, None, note + " (весов в журнале нет)")]
    worst = 0.0
    for choice, weight in zip(("Confess", "Lie", "Silent"), weights):
        p = weight / sum(weights)
        sigma = math.sqrt(total * p * (1 - p)) or 1.0
        worst = max(worst, abs(counts[choice] - total * p) / sigma)
    note += ", веса %g/%g/%g, отклонение до %.1f σ" % (weights + (worst,))
    if total < 12:
        return [Result(SKIP, None, note + " — для вывода нужно 12+ ответов")]
    return [Result(FAIL if worst > 3.5 else PASS, None, note)]


def check_immunity(ctx):
    """После допроса Палыч не начинает новую погоню за тем же жильцом, пока идёт иммунитет."""
    out = []
    for rnd in ctx.rounds:
        if not rnd.config_known:
            continue
        immunity = rnd.config["immunity"]
        resolved = rnd.of("interro_res")
        if not resolved:
            continue
        bad = None
        for line in resolved:
            who, t = line.g(1), tline(rnd, line, 13)
            for chase in rnd.of("chase_start"):
                tc = fnum(chase.g(9))
                if chase.g(1) != who or chase.idx < line.idx or tc is None or t is None or tc > t + immunity - 0.2:
                    continue
                search = prev_before(rnd, chase, ("searched",), limit=6)
                if (search is not None and search.g(3) == who) or forced_chase(rnd, chase) is not None:
                    continue
                bad = (chase, "%s снова в погоне через %.1f с после допроса (иммунитет %g с)" % (who, tc - t, immunity))
                break
            if bad:
                break
        if bad:
            out.append(Result(FAIL, bad[0], bad[1], rnd))
        else:
            out.append(Result(PASS, resolved[0], "допросов %d, повторных погонь в окне %g с нет" % (len(resolved), immunity), rnd))
    return out


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
            if lie_ok and rnd.is_bot(helper):
                thanks = next_after(rnd, res, ("notice_bot",), stop_kinds=("interro_start",),
                                    predicate=lambda o: o.g(1) == helper and "алиби сработало" in o.g(2), limit=40)
                if thanks is None:
                    problems.append("боту %s не пришло «Твоё алиби сработало»" % helper)
            note = "%s подтвердил алиби %s с %.0f см; %s" % (helper, suspect, distance,
                                                           "ложь удалась, алиби засчитано" if lie_ok else "%s — алиби не засчитано, как и должно" % res.g(2))
            out.append(Result(FAIL if problems else PASS, line, "; ".join(problems) or note, rnd))
        # Бот проверяет радиус, укрытие и призрака сам, в том же кадре: отказ сервера значит, что они считают по-разному.
        for line in rnd.of("bot_confirms"):
            helper, suspect = line.g(1), line.g(2)
            answer = prev_before(rnd, line, ("alibi_ok", "alibi_rej"), lambda o: o.g(1) == suspect and o.g(2) == helper, limit=12)
            if answer is None or answer.kind != "alibi_ok":
                out.append(Result(FAIL, answer or line, "бот %s решил подтвердить алиби %s, а сервер %s" % (
                    helper, suspect, "отказал: %s" % answer.g(3) if answer is not None else "не ответил"), rnd))
    rejected = sum(len(r.of("alibi_rej")) for r in ctx.rounds)
    if rejected and not any(r.status == PASS for r in out):
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
        if evicted_events > 1:
            problems.append("%s выселен %d раз" % (name, evicted_events))
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


# ---------------------------------------------------------------- длинная погоня, упор Палыча

def snaps_between(rnd, first, last):
    """[(строка «stuck», следующая строка, расстояние переноса см)] между двумя строками раунда."""
    out = []
    for line in between(rnd, first, last):
        if line.kind != "stuck":
            continue
        follow = rnd.lines[line.idx + 1] if line.idx + 1 < len(rnd.lines) else None
        jump = dist2d(vec(line.g(1)), vec(follow.g(1))) if follow is not None and follow.kind == "snapped" else 0.0
        out.append((line, follow, jump))
    return out


def check_long_chase(ctx):
    out = []
    longest = None
    for rnd in ctx.rounds:
        events = ev_list(rnd)
        for start in rnd.of("chase_start"):
            if start.g(3) == "None":
                continue
            who, item, z0 = start.g(1), start.g(3), fnum(start.g(6))
            end = next_after(rnd, start, ("chase_end",), predicate=lambda other: other.g(1) == who)
            if end is None:
                continue
            duration, seen, ran, rooms, z1, t1 = fnum(end.g(4)), fnum(end.g(5)), fnum(end.g(7)), int(end.g(10)), fnum(end.g(11)), fnum(end.g(19))
            forced = forced_chase(rnd, start)
            problems = []
            # Переносы Палыча: каждый — сразу «snapped … Chase», их число = stuck=, а их прыжки не считаются бегом.
            snaps = snaps_between(rnd, start, end)
            for stuck, follow, _ in snaps:
                if follow is None or follow.kind not in ("snapped", "snap_skipped"):
                    problems.append("после «stuck» в погоне нет переноса")
                elif follow.kind == "snapped" and follow.g(2) != "Chase":
                    problems.append("перенос в погоне записан с состоянием %s" % follow.g(2))
            if len(snaps) != int(end.g(18)):
                problems.append("строк «stuck» в погоне %d, а stuck=%s" % (len(snaps), end.g(18)))
            ran_run = ran - sum(jump for _, _, jump in snaps)
            # Что было в руках в конце: то же, если не ронял и не бросал.
            dropped = [e for e in events if e["by"] == who and e["type"] in ("ItemDropped", "ItemThrown") and start.idx < e["line"].idx < end.idx]
            if end.g(12) != item and not dropped:
                problems.append("начал погоню с %s, а в конце в руках %s" % (item, end.g(12)))
            if end.g(3) == "caught":
                interro = prev_before(rnd, end, ("interro_start",), lambda o: o.g(1) == who, limit=6)
                if interro is None or (interro.g(5) is not None and not close(fnum(interro.g(5)), t1, 0.15)):
                    problems.append("поймал, а строки «Interrogation started» прямо перед концом погони нет")
                else:
                    if interro.g(3) is not None and interro.g(3) != end.g(12):
                        problems.append("в конце погони в руках %s, а на допросе %s" % (end.g(12), interro.g(3)))
                    held = interro.g(3)
                    if interro.g(2) == "1":
                        taken = [e for e in events if e["type"] == "ContrabandConfiscated" and e["by"] == who and e["actor"] == held and e["target"] == who
                                 and close(e["t"], t1, 0.15)]
                        if not taken:
                            problems.append("пойман с запрещёнкой %s, а изъятия из рук нет" % held)
                    elif held not in (None, "None"):
                        problems.extend(return_problems(rnd, interro, held, end.g(9), who))
            enough = duration >= LONG_CHASE_SECONDS and seen >= LONG_CHASE_SEEN and ran_run >= LONG_CHASE_CM \
                and (rooms >= LONG_CHASE_ROOMS or abs(z1 - z0) >= FLOOR_CHANGE_CM)
            if longest is None or duration > longest[0]:
                longest = (duration, end, rnd)
            if problems:
                out.append(Result(FAIL, end, "%s с %s: %s" % (who, item, "; ".join(problems)), rnd))
            elif enough:
                note = "%s с %s (%s): %.1f с, на глазах %.1f с, бег %.0f м, %s → %s (%d смен, этаж %+.0f см), итог %s, переносов %d%s" % (
                    who, start.g(4), item, duration, seen, ran_run / 100, end.g(8), end.g(9), rooms, z1 - z0, end.g(3), len(snaps),
                    ", погоня по ключу -TestChaseAt" if forced is not None else "")
                out.append(Result(PASS, end, note, rnd))
    if not any(r.status == PASS for r in out) and longest is not None:
        duration, end, rnd = longest
        out.append(Result(SKIP, end, "погони с вещью были, самая длинная %.1f с / на глазах %s с / %.0f м — короче порога %g с, %g с, %g м" % (
            duration, end.g(5), fnum(end.g(7)) / 100, LONG_CHASE_SECONDS, LONG_CHASE_SEEN, LONG_CHASE_CM / 100), rnd))
    return out


def return_problems(rnd, interro, actor, caught_room, who):
    """Краденое, с которым поймали не у него дома, Палыч через 3 с относит на место (ItemReturned от вора)."""
    res = next_after(rnd, interro, ("interro_res",), predicate=lambda o: o.g(1) == who)
    end_item = next((l for l in rnd.of("end_item") if l.g(1) == actor), None)
    home = end_item.g(5) if end_item is not None else None
    if res is None:
        return []
    t_res = tline(rnd, res, 13)
    returned = next_after(rnd, res, ("returned",), stop_kinds=("interro_start",), predicate=lambda o: o.g(1) == actor)
    events = [e for e in ev_list(rnd) if e["type"] == "ItemReturned" and e["actor"] == actor and e["by"] == who]
    if returned is not None:
        problems = []
        if home is not None and returned.g(2) != home:
            problems.append("Палыч отнёс %s в %s, а дом у неё %s" % (actor, returned.g(2), home))
        if not any(t_res is not None and t_res + 2.0 <= e["t"] <= t_res + 6.0 for e in events):
            problems.append("нет события ItemReturned от %s через 2–6 с после допроса" % who)
        return problems
    if home is None or caught_room in ("None", home):
        return []
    picked = [e for e in ev_list(rnd) if e["type"] == "ItemPickedUp" and e["actor"] == actor and t_res is not None and t_res <= e["t"] <= t_res + 3.5]
    ended = rnd.duration() is not None and t_res is not None and rnd.duration() - t_res < 3.5
    if picked or ended or end_item is None or end_item.g(4) == home:
        return []
    return ["поймали с %s в %s (дом %s), а Палыч её не вернул" % (actor, caught_room, home)]


def check_stuck(ctx):
    out = []
    for rnd in ctx.rounds:
        stucks = rnd.of("stuck")
        for line in stucks:
            follow = rnd.lines[line.idx + 1] if line.idx + 1 < len(rnd.lines) else None
            if follow is None or follow.kind not in ("snapped", "snap_skipped"):
                out.append(Result(FAIL, line, "после «stuck» сразу нет ни переноса, ни отказа", rnd))
            # Тот же отрезок пути снова через считаные секунды — Палыч переносится по кругу.
            t = tline(rnd, line, 6)
            again = next((o for o in stucks if o.idx > line.idx and o.g(2) == line.g(2) and t is not None
                          and tline(rnd, o, 6) is not None and tline(rnd, o, 6) - t <= 10.0), None)
            if again is not None:
                out.append(Result(FAIL, again, "снова упёрся по пути к той же точке через %.1f с — перенос по кругу" % (tline(rnd, again, 6) - t), rnd))
        for line in rnd.of("blocked_pawn"):
            n, limit = int(line.g(3)), int(line.g(4))
            follow = rnd.lines[line.idx + 1] if line.idx + 1 < len(rnd.lines) else None
            if n > limit:
                out.append(Result(FAIL, line, "ждёт жильца %d-й раз из %d" % (n, limit), rnd))
            elif n == limit and (follow is None or follow.kind != "gave_up"):
                out.append(Result(FAIL, line, "дождался %d из %d, а путь не бросил" % (n, limit), rnd))
        if rnd.telemetry and "komendantStuck" in rnd.telemetry and rnd.finished:
            if int(rnd.telemetry["komendantStuck"]) != len(stucks):
                out.append(Result(FAIL, rnd.telemetry_line, "komendantStuck=%s, а строк «stuck» %d" % (rnd.telemetry["komendantStuck"], len(stucks)), rnd))
        tests = rnd.of("test_stuck")
        if not tests and rnd.finished and len(stucks) > 2:
            out.append(Result(FAIL, stucks[2], "Палыч сам упирался %d раз за раунд (было не больше 2)" % len(stucks), rnd))
        for line in tests:
            t0 = fnum(line.g(1))
            exact = [l for l in stucks if l.g(6) is not None]
            if exact:
                window = (t0 + STUCK_SECONDS - 0.2, t0 + STUCK_SECONDS + 1.1)
                hit = next((l for l in stucks if l.idx > line.idx and window[0] <= tline(rnd, l, 6) <= window[1]), None)
            else:
                hit = next((l for l in stucks if l.idx > line.idx and rnd.t_of(l) is not None and t0 <= rnd.t_of(l) <= t0 + STUCK_SECONDS + 3.0), None)
            state = next_after(rnd, line, ("state",))
            if hit is None:
                blocked = next_after(rnd, line, ("blocked_pawn",), limit=40)
                if state is not None and tline(rnd, state) is not None and tline(rnd, state) <= t0 + STUCK_SECONDS:
                    out.append(Result(SKIP, line, "проверочный упор прерван сменой состояния (%s)" % state.g(1), rnd))
                elif blocked is not None and tline(rnd, blocked) is not None and tline(rnd, blocked) <= t0 + STUCK_SECONDS + 1.1:
                    out.append(Result(SKIP, blocked, "упор пришёлся на жильца в проходе — Палыч ждёт, а не переносится", rnd))
                else:
                    out.append(Result(FAIL, line, "проверочный упор не вызвал переноса за %.1f с" % (STUCK_SECONDS + 1.1), rnd))
                continue
            follow = rnd.lines[hit.idx + 1] if hit.idx + 1 < len(rnd.lines) else None
            if follow is not None and follow.kind == "snap_skipped":
                out.append(Result(SKIP, follow, "у точки нет места — перенос пропущен, Палыч ждёт", rnd))
            elif follow is None or follow.kind != "snapped":
                out.append(Result(FAIL, hit, "упёрся, а переноса нет", rnd))
            elif follow.g(2) != "Patrol":
                out.append(Result(FAIL, follow, "проверочный упор на обходе, а перенос в состоянии %s" % follow.g(2), rnd))
            else:
                t1 = fnum(follow.g(3))
                going = next_after(rnd, follow, ("patrol_to", "state"), predicate=lambda o: tline(rnd, o) is not None and tline(rnd, o) <= t1 + 30.0)
                if going is None:
                    out.append(Result(FAIL, follow, "после переноса Палыч 30 с никуда не пошёл", rnd))
                else:
                    out.append(Result(PASS, follow, "проверочный упор t=%.1f → перенос на точку через %.1f с, дальше: %s" % (
                        t0, tline(rnd, hit, 6) - t0 if hit.g(6) else t1 - t0, clip(going.text, 50)), rnd))
        if not tests:
            for line in stucks:
                follow = rnd.lines[line.idx + 1] if line.idx + 1 < len(rnd.lines) else None
                if follow is not None and follow.kind == "snapped":
                    out.append(Result(PASS, follow, "упёрся сам и перенесён на точку (состояние %s)" % follow.g(2), rnd))
    return out


# ---------------------------------------------------------------- выселение

def eviction_cases(ctx, bot):
    out = []
    for rnd in ctx.rounds:
        evictions = [l for l in rnd.of("evicted") if l.g(2) is not None]
        cfg = rnd.config
        # Третий страйк без выселения.
        if rnd.config_known:
            for line in rnd.of("interro_res"):
                if line.g(10) is None or int(line.g(10)) < cfg["evictStrikes"] or (rnd.is_bot(line.g(1)) is True) != bot:
                    continue
                if not any(e.g(1) == line.g(1) and e.idx < line.idx for e in evictions) and \
                        not any(e.g(1) == line.g(1) and e.idx > line.idx and close(fnum(e.g(6)), tline(rnd, line, 13), 1.0) for e in evictions):
                    out.append(Result(FAIL, line, "%s набрал %s страйка, а выселения нет" % (line.g(1), line.g(10)), rnd))
        for line in evictions:
            if (line.g(5) == "1") != bot:
                continue
            who, strikes, before, after, t = line.g(1), int(line.g(2)), fnum(line.g(3)), fnum(line.g(4)), fnum(line.g(6))
            problems = []
            if rnd.config_known:
                if strikes != cfg["evictStrikes"]:
                    problems.append("выселен при %d страйках (порог %g)" % (strikes, cfg["evictStrikes"]))
                if abs(before - after - cfg["evictPenalty"]) > 0.5:
                    problems.append("штраф %g, ждали %g" % (before - after, cfg["evictPenalty"]))
                struck = [l for l in rnd.of("interro_res") if l.g(1) == who and l.g(3) == "1" and l.idx < line.idx]
                if len(struck) != cfg["evictStrikes"]:
                    problems.append("страйков по допросам %d" % len(struck))
                elif struck[-1].g(10) is not None and (int(struck[-1].g(10)) != strikes or not 0 <= t - tline(rnd, struck[-1], 13) <= 1.0):
                    problems.append("последний допрос: страйков %s, t=%s" % (struck[-1].g(10), struck[-1].g(13)))
            if sum(1 for e in evictions if e.g(1) == who) > 1:
                problems.append("выселен больше одного раза")
            events = ev_list(rnd)
            if not any(e["type"] == "PlayerEvicted" and e["by"] == who and close(e["t"], t, 0.15) for e in events):
                problems.append("нет события PlayerEvicted")
            later = [e for e in events if e["by"] == who and e["type"] in EVICTED_FORBIDDEN and e["t"] > t + 0.15]
            if later:
                problems.append("после выселения у него %s (t=%.1f)" % (later[0]["type"], later[0]["t"]))
            again = [l for l in rnd.lines[line.idx + 1:] if l.kind in ("interro_start", "sees", "chase_start") and l.g(1) == who]
            if again:
                problems.append("после выселения: %s" % clip(again[0].text, 50))
            result = next((l for l in rnd.of("result") if l.g(1) == who), None)
            if result is not None and (result.g(6) != "1" or result.g(7) != ("1" if bot else "0")):
                problems.append("в итогах evicted %s, bot %s" % (result.g(6), result.g(7)))
            player = next((p for p in (rnd.telemetry or {}).get("players", []) if p.get("name") == who), None)
            if player is not None and (not player.get("evicted") or int(player.get("strikes", 0)) < strikes or bool(player.get("bot")) != bot):
                problems.append("в телеметрии evicted=%s strikes=%s bot=%s" % (player.get("evicted"), player.get("strikes"), player.get("bot")))
            chronicle = [l.g(1) for l in rnd.of("chronicle")]
            top = sum(1 for e in events if e["type"] in ("PlayerEvicted", "PlayerFramed"))
            if chronicle and top <= cfg["chronicle"] and not any(who in text and any(w in text for w in EVICTED_WORDS) for text in chronicle):
                problems.append("в хронике нет строки о его выселении")
            if rnd.next is not None:
                reset = next((l for l in rnd.next.pre if l.kind == "reset"), None)
                if reset is not None and (reset.g(6) != "0" or reset.g(7) != "0" or reset.g(8) != "0"):
                    problems.append("в следующем раунде призраков %s, выселенных %s, со страйками %s" % (reset.g(6), reset.g(7), reset.g(8)))
            if bot:
                left = next_after(rnd, line, ("bot_left",), predicate=lambda other: other.g(1) == who) or \
                    next((l for l in rnd.post if l.kind == "bot_left" and l.g(1) == who), None)
                end_t = rnd.duration()
                if left is None and not problems:
                    if end_t is not None and end_t - t < 40:
                        out.append(Result(SKIP, line, "раунд кончился через %.0f с — до выхода бот не дошёл" % (end_t - t), rnd))
                    else:
                        out.append(Result(FAIL, line, "выселенный бот так и не ушёл через ночной выход", rnd))
                    continue
                if left is not None and left.round is not rnd:
                    problems.append("бот ушёл уже после конца раунда")
                note = "%s: %d страйка, %g → %g, ушёл через ночной выход%s" % (
                    who, strikes, before, after, (" через %.0f с" % (fnum(left.g(2)) - t)) if left is not None and left.g(2) else "")
            else:
                ghost = prev_before(rnd, line, ("ghost",), lambda other: other.g(1) == who and other.g(2) == "1" and not other.g(4), limit=12)
                if ghost is None:
                    problems.append("нет строки «ghost 1» — не стал наблюдателем")
                elif ghost.g(5) is not None and (ghost.g(5) != "1" or ghost.g(6) != "0"):
                    problems.append("призрак: невидим %s, мешает людям %s" % (ghost.g(5), ghost.g(6)))
                # NotifyAll пишет «выселен» всем до строки «evicted», а на экран оно может выйти позже (очередь сообщений).
                since = ghost.idx if ghost is not None else max(0, line.idx - 20)
                told = next((l for l in rnd.lines[since:] + rnd.post if l.kind in ("notice_shown", "notice_bot") and l.g(1) == who and "выселен" in l.g(2)), None)
                if told is None:
                    problems.append("сообщения «выселен» на экране не было")
                note = "%s: %d страйка, %g → %g, стал призраком%s" % (who, strikes, before, after,
                                                                    (" (ходит: %s)" % ghost.g(7)) if ghost is not None and ghost.g(7) else "")
                skipped = [l for l in rnd.lines[line.idx + 1:] if l.kind == "test_catch_skip" and "ghost" in l.g(1)]
                if skipped:
                    note += "; следующая поимка по ключу пропущена — призрака не ловят"
                if rnd.next is not None and any(l.kind == "lives" and l.g(1) == who for l in rnd.next.pre):
                    note += ", в реванше снова жилец"
            out.append(Result(FAIL if problems else PASS, line, "; ".join(problems) or note, rnd))
    return out


def check_evict_bot(ctx):
    return eviction_cases(ctx, True)


def check_evict_human(ctx):
    return eviction_cases(ctx, False)


# ---------------------------------------------------------------- Крыса

def check_rat_bonus(ctx):
    out = []
    bot_tips, refusals = 0, {}
    for rnd in ctx.rounds:
        bot_tips += len(rnd.of("bot_tipped"))
        for line in rnd.of("tip_refused"):
            reason = line.g(3).split(" (")[0]
            refusals[reason] = refusals.get(reason, 0) + 1
        knows = rnd.of("rat_knows")
        roles = dict((l.g(1), l.g(2)) for l in rnd.of("role"))
        if len(knows) > 1:
            out.append(Result(FAIL, knows[1], "Крыс, знающих чужое задание, %d" % len(knows), rnd))
        for line in knows:
            if roles and roles.get(line.g(1)) != "Rat":
                out.append(Result(FAIL, line, "%s знает чужое задание, а роль у него %s" % (line.g(1), roles.get(line.g(1))), rnd))
        known = dict((l.g(1), l.g(2)) for l in knows)
        pendings = rnd.of("rat_pending")
        if len(pendings) > 1:
            out.append(Result(FAIL, pendings[1], "бонус Крысе начислен %d раз за раунд" % len(pendings), rnd))
        for line in pendings:
            if roles and roles.get(line.g(1)) != "Rat":
                out.append(Result(FAIL, line, "бонус получил не Крыса (%s)" % roles.get(line.g(1)), rnd))
        catches = [e for e in ev_list(rnd) if e["type"] == "PlayerCaught"]
        for line in rnd.of("tip_accepted"):
            rat, victim, t0, window = line.g(1), line.g(2), fnum(line.g(5)), fnum(line.g(4))
            problems = []
            if rat in known and known[rat] != victim:
                problems.append("Крыса знает задание %s, а стук принят на %s" % (known[rat], victim))
            in_window = [e for e in catches if e["by"] == victim and t0 - 0.15 <= e["t"] <= t0 + window + 0.15]
            after = [e for e in catches if e["by"] == victim and e["t"] > t0 + window + 0.15]
            pending = next_after(rnd, line, ("rat_pending",), predicate=lambda other: other.g(1) == rat)
            final = next((l for l in rnd.of("final_score") if l.g(1) == rat), None)
            player = next((p for p in (rnd.telemetry or {}).get("players", []) if p.get("name") == rat), None)
            if not in_window:
                if pending is not None:
                    problems.append("бонус без поимки в окне %g с" % window)
                if after and next_after(rnd, line, ("rat_expired",), predicate=lambda o: o.g(1) == rat) is None:
                    problems.append("%s поймали через %.0f с — после окна, а строки «Rat tip expired» нет" % (victim, after[0]["t"] - t0))
                if final is not None and fnum(final.g(4)) != 0:
                    problems.append("стук не сработал, а в очках бонус +%s" % final.g(4))
                if player is not None and fnum(player.get("ratBonus"), 0) != 0:
                    problems.append("в телеметрии ratBonus %s" % player.get("ratBonus"))
                if problems:
                    out.append(Result(FAIL, line, "; ".join(problems), rnd))
                else:
                    out.append(Result(SKIP, line, "стук не сработал: %s не поймали за %g с%s" % (victim, window, ", поймали позже" if after else ""), rnd))
                continue
            catch = in_window[0]
            forced = forced_by_test(rnd, victim, catch["t"])
            if pending is None:
                problems.append("поймали %s в окне, а бонуса нет" % victim)
            else:
                if rnd.config_known and fnum(pending.g(2)) != rnd.config["ratBonus"]:
                    problems.append("бонус %s, ждали %g" % (pending.g(2), rnd.config["ratBonus"]))
                if pending.g(3) != victim or fnum(pending.g(4)) > window + 0.5:
                    problems.append("бонус за %s через %s с" % (pending.g(3), pending.g(4)))
                succeeded = next_after(rnd, pending, ("event",), predicate=lambda e: e.g(1) == "TipSucceeded" and e.g(3) == rat and e.g(7) == victim)
                if succeeded is None:
                    problems.append("нет события TipSucceeded")
                if final is not None and abs(fnum(final.g(4)) - fnum(pending.g(2))) > 0.5:
                    problems.append("в итоговых очках бонус %s" % final.g(4))
                if player is not None and "ratBonus" in player and abs(fnum(player["ratBonus"], 0) - fnum(pending.g(2))) > 0.5:
                    problems.append("в телеметрии ratBonus %s" % player["ratBonus"])
            note = "%s стукнул на %s, поймали через %.0f с (%s), бонус +%s" % (
                rat, victim, catch["t"] - t0, "поимка по ключу" if forced is not None else "сам", pending.g(2) if pending else "?")
            out.append(Result(FAIL if problems else PASS, pending or line, "; ".join(problems) or note, rnd))
    if not any(r.status == PASS for r in out) and (bot_tips or refusals):
        text = ", ".join("%s: %d" % (k, v) for k, v in refusals.items())
        out.append(Result(SKIP, None, "боты стучали %d раз; отказы сервера — %s (отказ «busy» сжигает стук бота на весь раунд)" % (bot_tips, text or "нет")))
    return out


# ---------------------------------------------------------------- СМС

def sms_cases(ctx):
    cases = []
    for rnd in ctx.rounds:
        roles = dict((p.get("name"), p.get("role")) for p in (rnd.telemetry or {}).get("players", []))
        roles.update(dict((l.g(1), l.g(2)) for l in rnd.of("role")))
        players = set(rnd.players()) | set(l.g(2) for l in rnd.of("sms"))
        events = ev_list(rnd)
        for line in rnd.of("sms"):
            index, reader, role, t, text, source = int(line.g(1)), line.g(2), line.g(3), fnum(line.g(4)), line.g(5), line.g(6)
            problems, warnings = [], []
            if roles.get(reader) and roles[reader] != role:
                problems.append("роль в СМС %s, в журнале/телеметрии %s" % (role, roles[reader]))
            if rnd.config_known and index > rnd.config["sms"]:
                problems.append("СМС №%d при лимите %g" % (index, rnd.config["sms"]))
            words = SMS_TEXT.match(text)
            kind = "quiet"
            match = re.match(r"^event (\w+) by (.+) room (\S+) t=(" + NUM + r")$", source)
            lie = re.match(r"^lie verb (\d+) room (\S+)$", source)
            if match:
                kind = "true"
                etype, by, room, et = match.group(1), match.group(2), match.group(3), fnum(match.group(4))
                if role == "Paranoid":
                    problems.append("Параноику пришла правда")
                if by == reader:
                    problems.append("слух про самого читателя")
                found = [e for e in events if e["type"] == etype and e["by"] == by and e["room"] == room and close(e["t"], et, 0.11 if e["exact"] else 1.5)]
                if not found:
                    problems.append("события %s у %s в %s t=%s в журнале нет" % (etype, by, room, match.group(4)))
                elif et > t + 0.11:
                    problems.append("слух о событии из будущего")
                if words is None:
                    problems.append("текст не по шаблону: %s" % clip(text, 60))
                else:
                    verb = SMS_VERBS.index(words.group(2))
                    if SMS_EVENT[verb] != etype:
                        problems.append("глагол «%s», а событие %s" % (words.group(2), etype))
                    if words.group(1) != by:
                        problems.append("в тексте %s, а событие у %s" % (words.group(1), by))
                    info = rnd.room(room)
                    if info is not None and words.group(3) != info[3]:
                        problems.append("комната в тексте «%s», а %s — «%s»" % (words.group(3), room, info[3]))
                    if verb == 3 and info is not None and info[0] in NOT_LURK_ROOMS:
                        problems.append("«крутился без дела» в %s (%s)" % (room, info[0]))
                    if verb == 1 and info is not None and info[2] == 0:
                        warnings.append("«прятал» в %s, где тайников нет (прятал из соседней комнаты) — у Параноика такой фразы не бывает" % room)
            elif lie:
                kind = "lie"
                verb, room = int(lie.group(1)), lie.group(2)
                info = rnd.room(room)
                if role != "Paranoid":
                    problems.append("выдумка пришла не Параноику (%s)" % role)
                if not 0 <= verb < len(SMS_VERBS):
                    problems.append("глагол №%d" % verb)
                elif info is not None:
                    if verb == 1 and info[2] == 0:
                        problems.append("выдумка «прятал» в %s, где тайников нет" % room)
                    if verb == 3 and info[0] in NOT_LURK_ROOMS:
                        problems.append("выдумка «крутился без дела» в %s (%s)" % (room, info[0]))
                if words is None:
                    problems.append("текст не по шаблону: %s" % clip(text, 60))
                else:
                    if 0 <= verb < len(SMS_VERBS) and words.group(2) != SMS_VERBS[verb]:
                        problems.append("глагол «%s», а выбран №%d" % (words.group(2), verb))
                    if words.group(1) == reader:
                        problems.append("выдумка про самого Параноика")
                    elif players and words.group(1) not in players:
                        problems.append("выдумка про %s, которого нет в раунде" % words.group(1))
                    if info is not None and words.group(3) != info[3]:
                        problems.append("комната в тексте «%s», а %s — «%s»" % (words.group(3), room, info[3]))
            else:
                if text != SMS_QUIET:
                    problems.append("источник %s, текст не «тихо»: %s" % (source, clip(text, 50)))
                if role == "Paranoid" and len(players) >= 2:
                    problems.append("Параноику пришло «тихо», хотя соседи есть (выдумка есть всегда)")
                elif role != "Paranoid":
                    eligible = [e for e in events if e["by"] not in (reader, "-") and e["t"] <= t + 0.11 and rnd.room(e["room"]) is not None
                                and (e["type"] in ("ItemPickedUp", "ItemHidden", "Noise")
                                     or (e["type"] == "RoomEntered" and rnd.room(e["room"])[0] not in NOT_LURK_ROOMS))]
                    if eligible:
                        problems.append("пришло «тихо», а событие для слуха было: %s у %s (t=%.1f)" % (eligible[0]["type"], eligible[0]["by"], eligible[0]["t"]))
            cases.append((rnd, line, kind, problems, warnings))
    return cases


def check_sms_true(ctx):
    out = []
    for rnd, line, kind, problems, warnings in ctx.cache("sms", sms_cases):
        if line.g(3) == "Paranoid":
            continue
        if kind in ("true", "quiet") or problems:
            note = "; ".join(problems) or ("слух по настоящему событию: %s" % line.g(6) if kind == "true" else "«тихо» — событий для слуха не было")
            if warnings and not problems:
                note += "; внимание: " + warnings[0]
            out.append(Result(FAIL if problems else PASS, line, note, rnd))
    return out


def check_sms_paranoid(ctx):
    out = []
    for rnd, line, kind, problems, warnings in ctx.cache("sms", sms_cases):
        if line.g(3) == "Paranoid" or kind == "lie":
            out.append(Result(FAIL if problems else PASS, line, "; ".join(problems) or "Параноику выдумка: %s" % line.g(6), rnd))
    # Роль Параноика не должна всплыть на его экране до итогов.
    for rnd in ctx.rounds:
        paranoid = [l.g(1) for l in rnd.of("role") if l.g(2) == "Paranoid"]
        for line in rnd.of("notice_shown") + rnd.of("notice_bot"):
            if line.g(1) in paranoid and "Параноик" in line.g(2):
                out.append(Result(FAIL, line, "Параноику до итогов показали его роль", rnd))
        for name in paranoid:
            result = next((l for l in rnd.of("result") if l.g(1) == name), None)
            if result is not None and result.g(2) != "Paranoid":
                out.append(Result(FAIL, result, "в итогах у Параноика роль %s" % result.g(2), rnd))
    return out


def check_sms_batches(ctx):
    """СМС приходят пачками: по одной каждому жильцу, sms раз за раунд, в середине отрезков раунда."""
    out = []
    for rnd in ctx.rounds:
        lines = rnd.of("sms")
        started = rnd.of("round_started")
        if not started or not rnd.finished:
            continue
        total, players = fnum(started[0].g(1)), rnd.num_players()
        count = int(rnd.config["sms"]) if rnd.config_known else None
        duration = rnd.duration()
        problems = []
        batches = {}
        for line in lines:
            batches.setdefault(int(line.g(1)), []).append(line)
        if count:
            interval = total / count
            # Расписание считается от настроенной длины раунда (с -TestPhaseSeconds раунд короче, и часть СМС не успевает);
            # пачка в ту же секунду, что и конец раунда, может прийти, а может и нет.
            expected = [interval * (i - 0.5) for i in range(1, count + 1)]
            sure = [t for t in expected if duration is None or t < duration - 1.0]
            maybe = [t for t in expected if duration is not None and duration - 1.0 <= t <= duration + 1.0]
            expected = sure + maybe
            if not len(sure) <= len(batches) <= len(sure) + len(maybe):
                problems.append("пачек СМС %d, ждали %d (раунд %s с)" % (len(batches), len(sure), "%.0f" % duration if duration is not None else "?"))
            for index, at in enumerate(expected, 1):
                batch = batches.get(index, [])
                if batch and not all(close(fnum(l.g(4)), at, 1.0) for l in batch):
                    problems.append("пачка %d пришла в t=%s, ждали ≈%.0f" % (index, batch[0].g(4), at))
        for index, batch in sorted(batches.items()):
            names = [l.g(2) for l in batch]
            if len(batch) != players or len(set(names)) != len(names):
                problems.append("в пачке %d СМС %d (жильцов %d, имён разных %d)" % (index, len(batch), players, len(set(names))))
        if not lines and not problems:
            continue
        out.append(Result(FAIL if problems else PASS, lines[0] if lines else started[0], "; ".join(problems[:3]) or
                          "пачек %d по %d СМС, время t=%s" % (len(batches), players, ", ".join(batches[i][0].g(4) for i in sorted(batches))), rnd))
    return out


# ---------------------------------------------------------------- отбой и подозрение

def check_curfew(ctx):
    out = []
    natural = 0
    for rnd in ctx.rounds:
        # В кадр смены фазы порядок строк и фаза в игре могут разойтись — такие секунды не судим.
        edges = [rnd.t_of(l) for l in rnd.of("phase") if l.g(1) in ("Night", "Morning")]

        def on_edge(t):
            return t is None or any(e is not None and abs(t - e) <= 0.3 for e in edges)

        for line in rnd.of("sees"):
            who, room, carry, curfew, rate = line.g(1), line.g(2), line.g(3), line.g(4) == "1", fnum(line.g(5))
            phase = rnd.phase_at(line)
            info = rnd.room(room)
            if on_edge(tline(rnd, line, 7)):
                continue
            problems = []
            if curfew:
                if phase != "Night":
                    problems.append("отбой засчитан в фазе %s" % phase)
                if info is not None and not info[1]:
                    problems.append("комната %s не запретна после отбоя" % room)
                pers = personality_numbers(rnd, line)
                if carry == "none" and rnd.config_known and pers is not None:
                    expected = rnd.config["curfewRate"] * pers["mult"]
                    if abs(expected - rate) > 0.06:
                        problems.append("скорость %.1f/с, ждали %.1f" % (rate, expected))
            elif phase == "Night" and info is not None and info[1]:
                problems.append("ночью в запретной %s отбой не засчитан" % room)
            if curfew or problems:
                out.append(Result(FAIL if problems else PASS, line, "; ".join(problems) or "%s ночью в %s: подозрение +%.1f/с, сейчас %s" % (who, room, rate, line.g(6)), rnd))
        # Заметил ночью в запретной комнате — должна быть строка «sees … » (рост подозрения), кроме допроса, иммунитета и выселенных.
        if not rnd.of("sees") or not rnd.config_known:
            continue
        windows = interrogation_windows(rnd)
        resolved = [(l.g(1), tline(rnd, l, 13)) for l in rnd.of("interro_res")]
        evicted = dict((l.g(1), fnum(l.g(6))) for l in rnd.of("evicted") if l.g(6) is not None)
        sees = [(l.g(1), tline(rnd, l, 7)) for l in rnd.of("sees")]
        night = [(l.idx, l.g(1)) for l in rnd.of("phase")]
        for e in ev_list(rnd):
            if e["type"] != "PlayerSpotted" or not e["exact"]:
                continue
            info = rnd.room(e["room"])
            phase = "Evening"
            for idx, name in night:
                if idx < e["line"].idx:
                    phase = name
            if phase != "Night" or info is None or not info[1] or on_edge(e["t"]):
                continue
            who, t = e["by"], e["t"]
            if any(s is not None and s - 0.05 <= t <= (f if f is not None else 1e9) + 0.5 for _, s, f in windows):
                continue
            if any(name == who and r is not None and r <= t <= r + rnd.config["immunity"] + 0.2 for name, r in resolved):
                continue
            if who in evicted and t >= evicted[who]:
                continue
            if not any(name == who and s is not None and t - 5.1 <= s <= t + 0.15 for name, s in sees):
                out.append(Result(FAIL, e["line"], "%s замечен ночью в запретной %s, а подозрение не росло (строки «sees» нет)" % (who, e["room"]), rnd))
                break
        for chase in rnd.of("chase_start"):
            if chase.g(3) != "None" or rnd.phase_at(chase) != "Night" or forced_chase(rnd, chase) is not None:
                continue
            tc = fnum(chase.g(9))
            if any(name == chase.g(1) and s is not None and tc - 10.0 <= s <= tc for name, s in
                   [(l.g(1), tline(rnd, l, 7)) for l in rnd.of("sees") if l.g(4) == "1"]):
                natural += 1
    if natural and out:
        out.append(Result(PASS, None, "погонь за нарушителями отбоя (без вещи, сами): %d" % natural))
    return out


# ---------------------------------------------------------------- мстительный Палыч

def check_vindictive(ctx):
    out = []
    misses = []
    for rnd in ctx.rounds:
        week = next((l for l in rnd.pre if l.kind == "week"), None)
        planned = int(week.g(4)) if week is not None and week.g(4) is not None else None
        first = next((l for l in rnd.of("personality") if l.g(2) == "0"), None)
        # Имя ассета и флаг мести должны совпадать: иначе DA_KomendantPersonality настроен не так.
        for line in rnd.of("personality"):
            if line.g(9) is not None and ("Vindictive" in line.g(1)) != (line.g(9) == "1"):
                out.append(Result(FAIL, line, "личность %s, а grudge=%s — ассет настроен не так" % (line.g(1), line.g(9)), rnd))
                break
        if first is not None:
            style = personality_style(first.g(1))
            if planned is not None and planned >= 0 and style != planned:
                out.append(Result(FAIL, first, "ночь недели велит манеру %d, а выпала %s (%d)" % (planned, first.g(1), style), rnd))
            elif planned is None and week is not None and int(week.g(2)) == WEEK_NIGHTS - 1 and style != 2:
                out.append(Result(FAIL, first, "пятница, а Палыч не мстительный (%s)" % first.g(1), rnd))
            if rnd.telemetry and "komendantStyle" in rnd.telemetry and int(rnd.telemetry["komendantStyle"]) != style:
                out.append(Result(FAIL, rnd.telemetry_line, "в телеметрии komendantStyle=%s, а личность %s" % (rnd.telemetry["komendantStyle"], first.g(1)), rnd))
        grudge_lines = rnd.of("grudge")
        on_lines = rnd.of("grudge_on")
        holds = grudge_lines[0].g(2) == "1" if grudge_lines else (first is not None and first.g(9) == "1")
        if not holds:
            if on_lines:
                out.append(Result(FAIL, on_lines[0], "Палыч не мстительный, а подозрение с прошлого раунда выставил", rnd))
            continue
        caught_before = int(grudge_lines[0].g(3)) if grudge_lines else None
        if not caught_before:
            out.append(Result(SKIP, grudge_lines[0] if grudge_lines else first, "мстительный, но в прошлом раунде никого не ловил", rnd))
            continue
        friday = [l.g(1) for l in rnd.pre + rnd.lines if l.kind == "friday_grudge"]
        allowed = set(friday)
        if not friday and rnd.prev is not None:
            allowed = set(e.g(3) for e in rnd.prev.of("event") if e.g(1) == "PlayerCaught")
        problems = []
        if friday and caught_before != 1:
            problems.append("в пятницу помнит одного (%s), а записано %d" % (friday[0], caught_before))
        if not friday and rnd.prev is not None and caught_before != len(allowed):
            problems.append("помнит %d, а в прошлом раунде ловили %d" % (caught_before, len(allowed)))
        if len(on_lines) != caught_before:
            problems.append("помнит %d, а строк «grudge on» %d" % (caught_before, len(on_lines)))
        for line in on_lines:
            who = line.g(1)
            if rnd.config_known and abs(fnum(line.g(2)) - rnd.config["grudge"]) > 0.5:
                problems.append("%s: подозрение %s, ждали %g" % (who, line.g(2), rnd.config["grudge"]))
            if fnum(line.g(4)) < 19.5:
                problems.append("%s: жар комнаты %s, ждали ≥20" % (who, line.g(4)))
            if rnd.homes.get(who) and line.g(3) != rnd.homes[who]:
                problems.append("%s живёт в %s, а жар добавлен в %s" % (who, rnd.homes[who], line.g(3)))
            if (rnd.prev is not None or friday) and who not in allowed:
                problems.append("%s не попадался в прошлом раунде" % who)
        if rnd.telemetry and "grudge" in rnd.telemetry:
            if sorted(rnd.telemetry["grudge"]) != sorted(l.g(1) for l in on_lines):
                problems.append("в телеметрии grudge=%s" % rnd.telemetry["grudge"])
        # Поведение (мягко): в первые 60 с он ходит к комнате того, кого помнит.
        rooms = set(l.g(3) for l in on_lines)
        catch_at = [fnum(l.g(2)) for l in rnd.of("test_catch")]
        limit = min([60.0] + [t for t in catch_at if t is not None])
        visit = next((l for l in rnd.of("patrol_to") if l.g(2) in rooms and fnum(l.g(4)) <= limit), None)
        if visit is None:
            misses.append((rnd, on_lines[0] if on_lines else grudge_lines[0]))
        names = ", ".join("%s (%s)" % (l.g(1), l.g(2)) for l in on_lines)
        note = "мстительный помнит: %s; %s" % (names, ("в t=%s пошёл к его комнате %s" % (visit.g(4), visit.g(2))) if visit is not None
                                               else "к его комнате в первые %.0f с не ходил" % limit)
        out.append(Result(FAIL if problems else PASS, on_lines[0] if on_lines else grudge_lines[0], "; ".join(problems) or note, rnd))
    if len(misses) >= 3:
        rnd, line = misses[0]
        out.append(Result(FAIL, line, "в %d мстительных раундах Палыч ни разу не пошёл к комнате того, кого помнит, в первые 60 с" % len(misses), rnd))
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


# ---------------------------------------------------------------- телевизор и запрещёнка до конца

def check_tv_scenario(ctx):
    out = []
    interrupted = 0
    for rnd in ctx.rounds:
        interrupted += sum(1 for l in rnd.of("hide_interrupted") if l.g(2) == "hide")
        tv_ids = set(l.g(6) for l in rnd.of("task_assigned") if l.g(1) in ("steal_tv", "protect_tv") and l.g(6) and l.g(6) != "None")
        events = ev_list(rnd)
        if not tv_ids:
            tv_ids = set(e["id"] for e in events if e["id"].lower() in ("tv", "television"))
        end_items = dict((l.g(1), l) for l in rnd.of("end_item"))
        done = False
        for pick in events:
            if done or pick["type"] != "ItemPickedUp" or pick["id"] not in tv_ids:
                continue
            who, actor = pick["by"], pick["actor"]
            home = rnd.homes.get(who)
            end = end_items.get(actor)
            tv_home = end.g(5) if end is not None else None
            if tv_home and pick["room"] != tv_home:
                continue
            hide = next((l for l in rnd.lines[pick["line"].idx + 1:] if l.kind == "hid_item" and l.g(2) == actor), None)
            if hide is None or hide.g(4) != who:
                continue
            gap = rnd.lines[pick["line"].idx + 1: hide.idx]
            lost = [l for l in gap if l.kind == "event" and l.g(3) == who and (l.g(1) in ("ItemDropped", "ItemThrown", "PlayerCaught"))]
            if lost:
                continue  # уронил по дороге — цепочка начнётся со следующего подъёма
            if hide.g(5) != home:
                continue
            problems = []
            teleport = [l for l in gap if l.kind == "bot_stuck" and l.g(1) == who]
            entered = [l for l in gap if l.kind == "event" and l.g(1) == "RoomEntered" and l.g(3) == who and l.g(6) == home]
            if not entered:
                problems.append("нет события RoomEntered в %s по дороге" % home)
            moved_later = any(l.kind == "event" and l.g(1) == "ItemPickedUp" and l.g(4) == actor for l in rnd.lines[hide.idx + 1:])
            if end is not None and not moved_later and (end.g(3) != "Hidden" or end.g(4) != home or end.g(6) != who):
                problems.append("в конце ТВ: %s в %s, спрятал %s" % (end.g(3), end.g(4), end.g(6)))
            for line in rnd.of("task_final"):
                if line.g(1) == "steal_tv" and line.g(3) == "COMPLETED" and end is not None and end.g(4) != rnd.homes.get(line.g(2)):
                    problems.append("steal_tv у %s засчитано, а ТВ в конце в %s" % (line.g(2), end.g(4)))
                if line.g(1) == "protect_tv" and line.g(3) == "COMPLETED" and end is not None and end.g(4) != tv_home:
                    problems.append("protect_tv у %s засчитано, а ТВ в конце в %s" % (line.g(2), end.g(4)))
            steal = task_final_of(rnd, "steal_tv", who)
            tail = (", steal_tv: %s, +%s" % (steal.g(3), steal.g(4) or "?")) if steal is not None else ""
            if problems:
                out.append(Result(FAIL, hide, "; ".join(problems), rnd))
            elif teleport:
                out.append(Result(SKIP, teleport[0], "%s донёс ТВ до %s, но по дороге застрял и был перенесён — не в счёт" % (who, home), rnd))
            else:
                out.append(Result(PASS, hide, "%s взял ТВ в %s, донёс до своей %s и спрятал в %s за %.0f с%s" % (
                    who, pick["room"], home, hide.g(3), (ev_t(rnd, hide) or pick["t"]) - pick["t"], tail), rnd))
                done = True
    if not any(r.status in (PASS, FAIL) for r in out) and interrupted:
        out.append(Result(SKIP, None, "сорванных «спрятать» (отошёл раньше 2 с): %d" % interrupted))
    return out


def ev_t(rnd, line):
    """Секунды раунда для строки без t=: по событию ItemHidden сразу перед ней."""
    event = prev_before(rnd, line, ("event",), lambda e: e.g(1) == "ItemHidden", limit=4)
    return fnum(event.g(2)) if event is not None else rnd.t_of(line)


def check_contraband_survived(ctx):
    """«Спрячь запрещёнку»: спрятанная пролежала до конца → задание выполнено, награда — в очках, телеметрии и на экране итогов."""
    out = []
    for rnd in ctx.rounds:
        if not rnd.finished:
            continue
        rows = dict((l.g(1), l) for l in rnd.post if l.kind == "results_row")
        for end in rnd.of("end_item"):
            if end.g(9) != "1" or end.g(3) != "Hidden":
                continue
            actor, hider, spot = end.g(1), end.g(6), end.g(8)
            task = task_final_of(rnd, "hide_contraband", hider)
            if task is None:
                continue
            problems = []
            hide = None
            for line in rnd.of("hid_item"):
                if line.g(2) == actor:
                    hide = line
            if hide is None:
                problems.append("нет строки, как %s прятали" % actor)
            else:
                if hide.g(3) != spot or hide.g(4) != hider or hide.g(7) != "1":
                    problems.append("последний раз %s прятал %s в %s, а в конце: %s в %s" % (actor, hide.g(4), hide.g(3), hider, spot))
                after = rnd.lines[hide.idx + 1:]
                if any(l.kind == "took_out" and l.g(2) == actor for l in after):
                    problems.append("её доставали, а в конце она в тайнике")
                if any(l.kind == "searched" and l.g(2) == actor for l in after) or \
                        any(l.kind == "event" and l.g(1) == "ContrabandConfiscated" and l.g(4) == actor for l in after):
                    problems.append("её изымали, а в конце она в тайнике")
            reward = fnum(task.g(4)) if task.g(4) is not None else None
            if task.g(3) != "COMPLETED":
                problems.append("пролежала до конца, а «спрячь запрещёнку» у %s провалено" % hider)
            final = next((l for l in rnd.of("final_score") if l.g(1) == hider), None)
            if final is not None and reward is not None and fnum(final.g(3)) < reward:
                problems.append("в очках за задания +%s, меньше награды %g" % (final.g(3), reward))
            player = next((p for p in (rnd.telemetry or {}).get("players", []) if p.get("name") == hider), None)
            if player is not None:
                entry = next((t for t in player.get("tasks", []) if t.get("id") == "hide_contraband"), None)
                if entry is None or not entry.get("completed") or (reward is not None and fnum(entry.get("reward")) != reward):
                    problems.append("в телеметрии %s" % json.dumps(entry, ensure_ascii=False))
            row = rows.get(hider)
            shown = ""
            if row is not None and reward is not None:
                if ("выполнено +%d" % reward) not in (row.g(3) or ""):
                    problems.append("на экране итогов нет «выполнено +%d»: %s" % (reward, clip(row.g(3) or "", 60)))
                else:
                    shown = ", на экране «выполнено +%d»" % reward
            elif not rows:
                shown = ", экран итогов не рисовался (строк «Results row» нет)"
            searched_after = [l for l in (rnd.lines[hide.idx + 1:] if hide is not None else []) if l.kind in ("searched", "not_reach") and l.g(1) == spot]
            why = "Палыч не дошёл до тайника" if any(l.kind == "not_reach" for l in searched_after) else \
                ("тайник обыскали — пусто?" if searched_after else "тайник не обыскивали")
            if any(l.kind == "searched" for l in searched_after):
                problems.append("тайник %s обыскали, а запрещёнку не нашли" % spot)
            out.append(Result(FAIL if problems else PASS, task, "; ".join(problems) or "%s спрятал %s в %s, пролежала до конца (%s), +%s%s" % (
                hider, actor, spot, why, task.g(4) or "?", shown), rnd))
    return out


# ---------------------------------------------------------------- задания

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
                    out.append(Result(FAIL, line, "%s — сервер: %s, по журналу: %s (%s)" % (owner, "выполнено" if done else "провалено",
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
        events = ev_list(rnd)
        if task_id == "curfew_breach":
            for line in rnd.of("sortie"):
                outside, needed, counted = fnum(line.g(3)), fnum(line.g(4)), line.g(5) == "1"
                if (outside >= needed) != counted:
                    out.append(Result(FAIL, line, "гулял %.1f с из %.1f, засчитано=%d" % (outside, needed, counted), rnd))
                elif counted and next_after(rnd, line, ("event",), predicate=lambda e: e.g(1) == "ReturnedToBuilding" and e.g(3) == line.g(1)) is None:
                    out.append(Result(FAIL, line, "вылазка засчитана, а события ReturnedToBuilding нет", rnd))
                elif not counted and next_after(rnd, line, ("event",), predicate=lambda e: e.g(1) == "ReturnedToBuilding" and e.g(3) == line.g(1), limit=4):
                    out.append(Result(FAIL, line, "вылазка не засчитана, а ReturnedToBuilding записан", rnd))
            for e in events:
                if e["type"] == "LeftBuilding" and rnd.phase_at(e["line"]) != "Night":
                    out.append(Result(FAIL, e["line"], "%s вышел через ночной выход в фазе %s" % (e["by"], rnd.phase_at(e["line"])), rnd))
        elif task_id == "fake_alarm" and rnd.of("heard"):
            for e in rnd.of("event"):
                if e.g(1) != "KomendantAlerted":
                    continue
                heard = prev_before(rnd, e, ("heard",), lambda other: other.g(1) == e.g(3) and other.g(4) == "1")
                exact = e.g(2) is not None
                if heard is None or not close(fnum(heard.g(6)), tline(rnd, e, 2), 0.15 if exact else 1.5):
                    out.append(Result(FAIL, e, "KomendantAlerted без строки «heard … investigating 1»", rnd))
        elif task_id == "tip_off":
            for tip in rnd.of("room_tip"):
                by, room, t0 = tip.g(1), tip.g(2), fnum(tip.g(3))
                queued = next_after(rnd, tip, ("tipped_room",), predicate=lambda o: o.g(1) == room, limit=10)
                if queued is None:
                    out.append(Result(FAIL, tip, "стук на %s принят, а Палыч обыск не запланировал" % room, rnd))
                    continue
                hidden = [l for l in rnd.lines[:tip.idx] if l.kind == "hid_item" and l.g(5) == room and l.g(7) == "1"]
                if int(queued.g(2)) == 0 and hidden:
                    out.append(Result(FAIL, queued, "в %s спрятана запрещёнка, а тайников в очереди 0" % room, rnd))
                found = [e for e in events if e["type"] == "ContrabandConfiscated" and e["room"] == room and e["t"] > t0]
                final = task_final_of(rnd, "tip_off", by)
                if final is not None and found and final.g(3) != "COMPLETED":
                    out.append(Result(FAIL, final, "после стука в %s изъяли запрещёнку, а tip_off провалено" % room, rnd))
                if final is not None and not found and final.g(3) == "COMPLETED":
                    out.append(Result(FAIL, final, "tip_off засчитано без изъятия в %s" % room, rnd))
        elif task_id == "frame_roommate":
            for detail in rnd.of("contraband_found"):
                if detail.g(7) != "1":
                    continue
                final = task_final_of(rnd, "frame_roommate", detail.g(4))
                if final is not None and final.g(3) != "COMPLETED":
                    out.append(Result(FAIL, final, "%s подбросил запрещёнку (%s), а frame_roommate провалено" % (detail.g(4), detail.g(6)), rnd))
        elif task_id == "investigate_theft":
            for line in rnd.of("accuse"):
                if line.g(4) == "1" or line.g(4) is None:
                    continue
                by, suspect, right = line.g(1), line.g(2), line.g(3) == "right"
                row = next((l for l in rnd.of("task_assigned") if l.g(3) == by and l.g(5) == "CorrectAccusation"), None)
                if row is None or row.g(7) in (None, "None"):
                    continue
                item, room = row.g(6), row.g(7)
                event = next_after(rnd, line, ("event",), predicate=lambda e: e.g(1) == "Accusation" and e.g(3) == by, limit=4)
                at = fnum(event.g(2)) if event is not None else rnd.t_of(line)
                picks = [e for e in events if e["type"] == "ItemPickedUp" and e["by"] == suspect and e["room"] == room
                         and (item == "None" or e["id"] == item) and (at is None or e["t"] <= at + 0.15)]
                carried_out = any(e["by"] == suspect and e["type"] in ("ItemDropped", "ItemThrown", "ItemHidden", "PlayerCaught") and e["actor"] == p["actor"]
                                  and e["t"] >= p["t"] and e["room"] != room and (at is None or e["t"] <= at + 0.15) for p in picks for e in events)
                holding = line.g(6) == suspect and line.g(7) not in (None, room)
                thief = carried_out or holding
                if right != thief:
                    out.append(Result(FAIL, line, "обвинение %s, а по событиям %s %s вор" % ("верное" if right else "мимо", suspect, "—" if thief else "не"), rnd))
        elif task_id == "sabotage_kitchen":
            for line in rnd.of("bot_breaks"):
                # Ломают несколько секунд, а между началом и концом может пройти допрос с десятком событий — ищем по времени, а не по счёту строк.
                t0 = rnd.t_of(line)
                broke = next_after(rnd, line, ("event",), predicate=lambda e: e.g(1) == "DeviceBroken" and e.g(3) == line.g(1)
                                   and (t0 is None or fnum(e.g(2)) is None or fnum(e.g(2)) <= t0 + 15.0))
                if broke is None:
                    out.append(Result(FAIL, line, "бот сломал %s, а события DeviceBroken нет" % line.g(2), rnd))
    return out


# ---------------------------------------------------------------- реванш и экран итогов

def check_rematch(ctx):
    out = []
    names = ["вещей не на месте", "спрятано", "в руках", "сломано приборов", "открытых дверей", "призраков", "выселенных", "со страйками", "с подозрением"]
    for rnd in ctx.rounds:
        reset = next((l for l in rnd.pre if l.kind == "reset"), None)
        if reset is None:
            continue
        values = [int(reset.g(i)) for i in range(1, 10)]
        dirty = ["%s %d" % (name, value) for name, value in zip(names, values) if value]
        problems = []
        if dirty:
            problems.append(", ".join(dirty))
        week = next((l for l in rnd.pre if l.kind == "week"), None)
        prev = rnd.prev
        if rnd.telemetry and week is not None:
            if int(rnd.telemetry.get("weekNight", -9)) != int(week.g(2)) or int(rnd.telemetry.get("weekId", -9)) != int(week.g(1)):
                problems.append("в телеметрии неделя %s ночь %s, в журнале %s/%s" % (rnd.telemetry.get("weekId"), rnd.telemetry.get("weekNight"), week.g(1), week.g(2)))
        lives = [l for l in rnd.pre if l.kind == "lives"]
        if lives and rnd.of("round_started") and len(set(l.g(1) for l in lives)) != rnd.num_players():
            problems.append("перед раундом «lives in» у %d, жильцов %d" % (len(set(l.g(1) for l in lives)), rnd.num_players()))
        if prev is None:
            out.append(Result(FAIL if problems else PASS, reset, "; ".join(problems) or "первый раунд: мир и жильцы чистые", rnd))
            continue
        prev_week = next((l for l in prev.pre if l.kind == "week"), None)
        if week is not None and prev_week is not None:
            night, week_id = int(week.g(2)), int(week.g(1))
            p_night, p_week = int(prev_week.g(2)), int(prev_week.g(1))
            expected = (p_night + 1) % WEEK_NIGHTS
            if night != expected or week_id != p_week + (1 if expected == 0 else 0):
                problems.append("после недели %d ночи %d идёт неделя %d ночь %d" % (p_week, p_night, week_id, night))
        before = dict((l.g(1), fnum(l.g(6))) for l in prev.of("final_score"))
        for line in rnd.of("final_score"):
            if line.g(1) in before and abs(before[line.g(1)] - fnum(line.g(2))) > 0.5:
                problems.append("%s начал с %s, а кончил прошлый раунд с %g" % (line.g(1), line.g(2), before[line.g(1)]))
                break
        mess = sum(1 for l in prev.of("end_item") if l.g(3) in ("Hidden", "Carried") or l.g(4) != l.g(5)) + \
            sum(1 for l in prev.of("end_device") if l.g(3) == "1")
        if problems:
            out.append(Result(FAIL, reset, "реванш: " + "; ".join(problems), rnd))
        elif not mess and prev.of("end_item"):
            out.append(Result(SKIP, reset, "реванш после чистого раунда — сброс ничего не доказывает", rnd))
        else:
            out.append(Result(PASS, reset, "реванш: после раунда с %d вещами/приборами не на месте мир и жильцы сброшены, очки перенесены" % mess, rnd))
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
        for line in headline + rnd.of("result"):
            if "{" in line.text or "}" in line.text:
                problems.append("неподставленный шаблон: %s" % clip(line.text, 60))
        if rnd.telemetry and rnd.of("chronicle"):
            if "chronicle" in rnd.telemetry and rnd.telemetry["chronicle"] != lines:
                problems.append("хроника в телеметрии другая (%d строк)" % len(rnd.telemetry["chronicle"]))
            if headline and "headline" in rnd.telemetry and rnd.telemetry["headline"] != headline[0].g(1):
                problems.append("заголовок в телеметрии другой: %s" % clip(rnd.telemetry["headline"], 40))
        top = [e for e in events if e["type"] in ("PlayerEvicted", "PlayerFramed")]
        if lines and limit is not None and len(top) <= limit:
            for e in top:
                stamp = "%02d:%02d" % divmod(int(math.floor(e["t"])), 60)
                if not any(text[:5] == stamp and e["by"] in text for text in lines):
                    problems.append("нет строки о самом важном: %s у %s в %s" % (e["type"], e["by"], stamp))
                    break
        note = "%d строк, первая: %s" % (len(lines), clip(lines[0], 70)) if lines else "событий для хроники не было"
        if headline:
            note += "; заголовок: %s" % clip(headline[0].g(1), 50)
        out.append(Result(FAIL if problems else PASS, (rnd.of("chronicle") or [rnd.telemetry_line])[0], "; ".join(problems) or note, rnd))
    return out


def check_layout(ctx):
    out = []
    for rnd in ctx.rounds:
        results = len(rnd.of("result"))
        chronicle = len(rnd.of("chronicle"))
        for line in rnd.post + [l for l in rnd.lines if l.kind == "layout"]:
            if line.kind == "results_trunc":
                out.append(Result(FAIL, line, "на экране итогов не влезли игроки: показано %s из %s" % (line.g(1), line.g(2)), rnd))
                continue
            if line.kind != "layout":
                continue
            problems = []
            if line.g(7) != "1":
                problems.append("не влезает над «Реваншем»")
            if results and int(line.g(8)) != results:
                problems.append("игроков на экране %s, в итогах %d" % (line.g(8), results))
            if rnd.of("chronicle") and int(line.g(9)) != chronicle:
                problems.append("строк хроники на экране %s, в журнале %d" % (line.g(9), chronicle))
            note = "раскладка %s, высота %s из %s, игроков %s, хроника %s, окно %sx%s" % (
                line.g(1), line.g(5), line.g(6), line.g(8), line.g(9), line.g(10), line.g(11))
            out.append(Result(FAIL if problems else PASS, line, ("; ".join(problems) + ": " + note) if problems else note, rnd))
    return out


def check_results_rows(ctx):
    """Строки игроков на экране итогов = роль, титул, очки и задания из журнала."""
    out = []
    for rnd in ctx.rounds:
        rows = [l for l in rnd.post if l.kind == "results_row"]
        if not rows:
            continue
        latest = {}
        for row in rows:
            latest[row.g(1)] = row
        problems = []
        for result in rnd.of("result"):
            name = result.g(1)
            row = latest.get(name)
            if row is None:
                problems.append("нет строки игрока %s" % name)
                continue
            header, tasks = row.g(2), row.g(3) or ""
            role = ROLE_TEXT.get(result.g(2), result.g(2))
            if ("— %s," % role) not in header:
                problems.append("%s: роль на экране не «%s»" % (name, role))
            if ("«%s»" % result.g(3)) not in header:
                problems.append("%s: титул на экране не «%s»" % (name, result.g(3)))
            # Числа на экране — FText::AsNumber: в нём бывают разделители тысяч и другой знак минуса.
            shown = re.search(r"Очки: (.+?), страйки: (\d+)", header)
            score = re.sub(r"[\s\u00a0\u202f,]", "", shown.group(1)).replace("\u2212", "-") if shown else None
            if shown is None or fnum(score) != fnum(result.g(4)) or shown.group(2) != result.g(5):
                problems.append("%s: очки/страйки на экране не %s/%s" % (name, result.g(4), result.g(5)))
            finals = [l for l in rnd.of("task_final") if l.g(2) == name]
            done = sorted(int(fnum(l.g(4), 0)) for l in finals if l.g(3) == "COMPLETED")
            shown_done = sorted(int(x) for x in re.findall(r"выполнено \+(\d+)", tasks))
            failed = sum(1 for l in finals if l.g(3) == "FAILED")
            if finals and (done != shown_done or tasks.count("провалено") != failed):
                problems.append("%s: на экране задания «%s», в журнале выполнено %s, провалено %d" % (name, clip(tasks, 50), done, failed))
            if "{" in header + tasks or "}" in header + tasks:
                problems.append("%s: неподставленный шаблон" % name)
        out.append(Result(FAIL if problems else PASS, rows[0], "; ".join(problems[:3]) or "строк игроков %d: роль, титул, очки и задания совпали с журналом" % len(latest), rnd))
    return out


CHECKS = [
    ("Журнал без падений", check_no_crash, "журнала нет"),
    ("Раунды доиграны до итогов", check_rounds_complete, "в журнале нет ни одного раунда (RoundStarted)"),
    ("Порядок строк раунда, итоги всех заданий", check_round_structure, "законченных раундов нет"),
    ("События журнала = телеметрия", check_events_match, "нет пары журнал + файл телеметрии"),
    ("Итоги заданий = телеметрия", check_task_verdicts, "нет итогов заданий"),
    ("Пересчёт всех заданий по журналу", check_task_recompute, "нет итогов заданий с условиями"),
    ("Арифметика очков", check_score_math, "нет строк «Final score»"),
    ("Побег: Палыч потерял из виду", check_escape, "не было; ключ -TestChaseAt=N даёт погоню"),
    ("Побег: Палыч говорит «ушёл»", check_escape_speech, "побегов не было"),
    ("Погоня: у каждого начала есть конец", check_chase_pairs, "погонь не было"),
    ("Прятки в шкафу во время погони", check_wardrobe, "не было"),
    ("Ночной выход как укрытие от погони", check_night_exit, "не было"),
    ("Палыч не видит спрятавшихся", check_xray, "никто не прятался"),
    ("Утренний обыск нашёл запрещёнку", check_morning_contraband, "утром запрещёнки в тайниках не было"),
    ("Утренний обыск: до тайников доходит", check_unreachable_spots, "утренней проверки не было (раунд короче утра?)"),
    ("Допрос: сознался", interrogation_check("confess"), "никто не сознавался"),
    ("Допрос: неудачная ложь", interrogation_check("lie_fail"), "не было"),
    ("Допрос: удачная ложь", interrogation_check("lie_ok"), "не было"),
    ("Допрос: молчание (ответил)", interrogation_check("silent"), "не было"),
    ("Допрос: не ответил за время → молчание", interrogation_check("timeout"), "ключ -TestCatchHostAt=40 (хост не отвечает)"),
    ("Ложь: доля удач по статистике", check_lie_statistics, "врали меньше двух раз"),
    ("Ответы ботов по весам", check_bot_answer_mix, "боты не отвечали на допросах"),
    ("После допроса — иммунитет", check_immunity, "допросов не было"),
    ("Алиби на допросе", check_alibi, "свидетеля рядом с допросом не было"),
    ("Телеметрия: поимки, допросы, страйки", check_telemetry_invariants, "нет файлов телеметрии"),
    ("Длинная погоня с вещью в руках", check_long_chase, "погонь с вещью не было; ключ -TestChaseAt=150,330"),
    ("Палыч упёрся → перенос на точку", check_stuck, "не упирался; ключ -TestKomendantStuck=N"),
    ("Выселение бота (уходит через выход)", check_evict_bot, "ботов не выселяли; ключ -TestCatchBotAt=60,130,200,270"),
    ("Выселение человека → призрак", check_evict_human, "людей не выселяли; ключ -TestCatchHostAt=40,100,160,220"),
    ("Бонус Крысе за поимку после стука", check_rat_bonus, "стуков Крысы не было"),
    ("СМС: правдивые слухи", check_sms_true, "СМС не было (раунд короче 2 минут?)"),
    ("СМС Параноика — выдумки", check_sms_paranoid, "Параноика не было (наверняка есть при 6+ жильцах: -Bots=5)"),
    ("СМС: пачки по расписанию, каждому по одной", check_sms_batches, "СМС не было"),
    ("Ночью в запретной зоне растёт подозрение", check_curfew, "ночью в запретных зонах никого не замечали"),
    ("Мстительный комендант", check_vindictive, "мстительный не выпадал; нужны раунды подряд (-AutoRounds=2 -WeekNight=3 — четверг и пятница)"),
    ("Раунд на 4+ жильцов", check_four_players, "законченных раундов на 4+ не было"),
    ("ТВ из гостиной в свой шкаф", check_tv_scenario, "никто не донёс ТВ до своей комнаты"),
    ("Запрещёнка пролежала до конца → награда на итогах", check_contraband_survived, "спрятанная запрещёнка до конца не доживала"),
] + [(name, task_row(task_id), "не выдавалось") for task_id, name in TASK_NAMES] + [
    ("Реванш: мир и счёт сброшены", check_rematch, "нет строк «Round reset check»"),
    ("Экран итогов: титулы", check_titles, "нет телеметрии с титулами"),
    ("Экран итогов: хроника", check_chronicle, "нет строк «Chronicle» и телеметрии с хроникой"),
    ("Экран итогов: всё влезает", check_layout, "нет строк «Results layout» (окно не рисовало итоги)"),
    ("Экран итогов: строки игроков", check_results_rows, "нет строк «Results row» (окно не рисовало итоги или старая сборка)"),
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


def telemetry_dir_for(log_path, forced):
    """Папка телеметрии: заданная ключом; иначе Saved/Telemetry рядом с Saved/Logs этого журнала (так и в упакованной сборке); иначе проекта."""
    if forced:
        return forced
    if log_path:
        near = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(log_path))), "Telemetry")
        if os.path.isdir(near):
            return near
    return os.path.join(ROOT, "Saved", "Telemetry")


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
    parser.add_argument("--telemetry", help="папка телеметрии (по умолчанию Telemetry рядом с папкой журнала, иначе Saved/Telemetry проекта)")
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
            log.parse(telemetry_dir_for(path, args.telemetry))
        except OSError as error:
            header.append("Не читается %s: %s" % (path, error))
            continue
        logs.append(log)
        header.append("Журнал %s: раундов %d, строк Obshaga %d (подробных %d)" % (path, len(log.rounds), log.obshaga_lines, log.verbose_lines))
        if log.obshaga_lines and not log.verbose_lines:
            header.append("  ! подробных строк нет — запускайте игру с -LogCmds=\"LogObshaga Verbose\"")
        if log.rounds and not any(r.config_known for r in log.rounds):
            header.append("  ! нет строк «Round config» — журнал записан сборкой без автопроверки, числа не сверяются")
        if log.rounds and not any(r.of("says") for r in log.rounds):
            header.append("  ! нет строк «Komendant says» — журнал старее этой версии скрипта, часть пунктов не проверится")
    extra = []
    folder = telemetry_dir_for(paths[-1] if paths else None, args.telemetry)
    if args.newest > 0 and os.path.isdir(folder):
        files = sorted(glob.glob(os.path.join(folder, "round_*.json")), key=os.path.getmtime, reverse=True)
        extra = files[: args.newest]
        header.append("Телеметрия без журнала: %d файлов из %s" % (len(extra), folder))

    ctx = Context(logs, extra)
    results = []
    for name, check, hint in CHECKS:
        try:
            items = check(ctx) if (logs or check is check_telemetry_invariants) else []
        except Exception:  # одна сломанная проверка не должна прятать остальные
            if os.environ.get("VERIFY_TRACE"):
                traceback.print_exc()
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
    except SystemExit:
        pass  # неверные ключи: argparse уже напечатал подсказку, код выхода всё равно 0
    except Exception:
        print("Сбой скрипта (итог не посчитан):")
        traceback.print_exc(file=sys.stdout)
    sys.exit(0)
