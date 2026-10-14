# Меняет числа баланса в DA_RoundConfig (и при желании в личностях Палыча) и сохраняет ассеты.
# Запуск (редактор закрыт), путь к скрипту — полный:
#   UnrealEditor-Cmd.exe <Obshaga.uproject> -run=pythonscript -script="C:/полный/путь/tools/set_balance.py имя=значение ..."
#   Имя без точки — поле DA_RoundConfig (как в C++, например CarrySuspicionPerSecond=18).
#   Имя с точкой — поле личности: Strict.SightRadius=1500 (Strict, Absent, Vindictive).
# Без аргументов только печатает текущие значения.
import sys

import unreal

CONFIG = "/Game/Obshaga/Data/DA_RoundConfig"
PERSONALITY = "/Game/Obshaga/AI/DA_KomendantPersonality_%s"


def snake(name):
    out = ""
    for index, char in enumerate(name):
        if char.isupper() and index > 0 and (not name[index - 1].isupper() or (index + 1 < len(name) and name[index + 1].islower())):
            out += "_"
        out += char.lower()
    return out


def parse(text):
    for kind in (int, float):
        try:
            return kind(text)
        except ValueError:
            pass
    return text


changed = set()
config = unreal.EditorAssetLibrary.load_asset(CONFIG)
for argument in sys.argv[1:]:
    if "=" not in argument:
        continue
    name, value = argument.split("=", 1)
    if "." in name:
        who, field = name.split(".", 1)
        path = PERSONALITY % who
        asset = unreal.EditorAssetLibrary.load_asset(path)
    else:
        path, asset, field = CONFIG, config, name
    old = asset.get_editor_property(snake(field))
    new = type(old)(parse(value)) if isinstance(old, (int, float)) else parse(value)
    asset.set_editor_property(snake(field), new)
    changed.add(path)
    unreal.log_warning("set_balance: %s %s: %s -> %s" % (path.split("/")[-1], field, old, new))

for path in changed:
    unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
unreal.log_warning("set_balance: saved %d assets" % len(changed))
