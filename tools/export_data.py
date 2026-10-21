# Выгружает данные игры в JSON для чтения без редактора: таблицу заданий DT_Tasks и настройки раунда DA_RoundConfig.
# Только читает ассеты, ничего не меняет и не сохраняет.
# Запуск (редактор закрыт), путь к скрипту — полный:
#   UnrealEditor-Cmd.exe <Obshaga.uproject> -run=pythonscript -script="C:/полный/путь/tools/export_data.py"
# Пишет docs/data/DT_Tasks.json и docs/data/DA_RoundConfig.json.
import json
import os

import unreal

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "docs", "data")
os.makedirs(OUT, exist_ok=True)


def write(name, data):
    path = os.path.join(OUT, name)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=2, sort_keys=False)
        f.write("\n")
    unreal.log("export_data: wrote %s" % path)


def plain(value):
    """Значение свойства — в то, что понимает JSON: числа и строки как есть, структуры и массивы по частям."""
    if value is None or isinstance(value, (bool, int, float, str)):
        return value
    if isinstance(value, unreal.Name) or isinstance(value, unreal.Text):
        return str(value)
    if isinstance(value, unreal.EnumBase):
        return value.name
    if isinstance(value, (list, tuple, unreal.Array)):
        return [plain(v) for v in value]
    if isinstance(value, unreal.Map):
        return {str(plain(k)): plain(v) for k, v in value.items()}
    if isinstance(value, unreal.StructBase):
        out = {}
        for field in dir(value):
            if field.startswith("_"):
                continue
            try:
                out[field] = plain(value.get_editor_property(field))
            except Exception:
                continue
        return out or str(value)
    if isinstance(value, unreal.Object):
        return value.get_path_name()
    return str(value)


# --- DT_Tasks: строки как есть, в том же виде, что берёт setup_tasks.py.
table = unreal.EditorAssetLibrary.load_asset("/Game/Obshaga/Data/DT_Tasks")
write("DT_Tasks.json", json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table)))

# --- DA_RoundConfig: все читаемые свойства; «changed» — отличается ли от значения по умолчанию из C++.
config = unreal.EditorAssetLibrary.load_asset("/Game/Obshaga/Data/DA_RoundConfig")
defaults = unreal.get_default_object(config.get_class())
props = {}
for name in sorted(dir(config)):
    if name.startswith("_"):
        continue
    try:
        value = config.get_editor_property(name)
    except Exception:
        continue
    if callable(value):
        continue
    try:
        default = defaults.get_editor_property(name)
    except Exception:
        default = None
    value_json, default_json = plain(value), plain(default)
    props[name] = {"value": value_json, "default": default_json, "changed": value_json != default_json}

write("DA_RoundConfig.json", {
    "asset": config.get_path_name(),
    "class": config.get_class().get_name(),
    "changed": sorted(k for k, v in props.items() if v["changed"]),
    "properties": props,
})
