# Добавляет в DT_Tasks задание expose_liar («Найди автора слуха») и связывает его с start_rumor;
# пишет новые описания expose_liar (подсказка на бумаге записки) и fake_alarm (засчитывается только приманка издалека).
# Остальные строки таблицы не трогает; повторный запуск ничего не портит.
# Запуск (редактор закрыт), путь к скрипту — полный:
#   UnrealEditor-Cmd.exe <Obshaga.uproject> -run=pythonscript -script="C:/полный/путь/tools/setup_tasks.py"
import json

import unreal

PATH = "/Game/Obshaga/Data/DT_Tasks"
table = unreal.EditorAssetLibrary.load_asset(PATH)
rows = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
by_name = {row["Name"]: row for row in rows}

rumor = by_name["start_rumor"]
theft = by_name["investigate_theft"]

# За основу берётся «расследование кражи»: то же условие (верно указать коменданту на человека),
# но предмет — записка, поэтому сервер ищет не вора, а того, кто эту записку подкинул.
liar = by_name.get("expose_liar")
if liar is None:
    liar = dict(theft)
    liar["Name"] = "expose_liar"
    rows.append(liar)
liar.update({
    "Title": "Найди автора слуха",
    "Description": "Кто-то подкидывает записки со слухами. Прочитай записку [F]: по бумаге видно, из каких комнат автор. "
                   "Вычисли его и покажи на него коменданту [R]. Попытка одна.",
    "bEnabled": True,
    "bCanBeMain": True,
    "Phase": "None",
    "MinPlayers": rumor["MinPlayers"],
    "Difficulty": 3,
    "BaseReward": theft["BaseReward"],
    "SuccessCondition": "CorrectAccusation",
    "ItemId": "Note",
    "RoomId": "None",
    "bUseOwnRoom": False,
    "Count": 0,
    "ConflictsWith": ["start_rumor"],
    "Boosts": [],
})
if "expose_liar" not in rumor["ConflictsWith"]:
    rumor["ConflictsWith"].append("expose_liar")

# «Ложная тревога»: в счёт идёт только брошенное подальше от себя (не ближе LureMinSelfDistance), не у Палыча на глазах,
# и потом LureUnseenSeconds (10 с) не попадаться ему; дверь, прибор или уроненное не считаются.
by_name["fake_alarm"]["Description"] = ("Брось что-нибудь звонкое подальше от себя, чтобы Палыч пошёл на грохот. "
                                        "Потом 10 секунд не попадайся ему на глаза.")

unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(table, json.dumps(rows, ensure_ascii=False))
unreal.EditorAssetLibrary.save_loaded_asset(table)
for name in ("start_rumor", "investigate_theft", "expose_liar", "fake_alarm"):
    row = {r["Name"]: r for r in json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))}[name]
    unreal.log_warning("setup_tasks: %s" % json.dumps(row, ensure_ascii=False))
unreal.log_warning("setup_tasks: rows %d" % len(unreal.DataTableFunctionLibrary.get_data_table_row_names(table)))
