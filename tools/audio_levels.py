# Громкости звуков: либо выгружает все звуки в WAV для измерения, либо записывает подобранные громкости в ассеты.
# Запуск (редактор закрыт), путь к скрипту — полный:
#   ... -run=pythonscript -script="C:/.../tools/audio_levels.py export <папка>"      — выгрузить WAV
#   ... -run=pythonscript -script="C:/.../tools/audio_levels.py apply <файл.json>"   — записать {"S_Door": 0.8, ...}
#   ... apply <файл.json> S_Door,S_Notice — записать только названным звукам. Без списка apply перезапишет все,
#   в том числе подобранные вручную в редакторе.
# Измеряет и считает громкости tools/audio-levels.ps1 (ему нужны выгруженные WAV).
import json
import os
import sys

import unreal

AUDIO_DIR = "/Game/Obshaga/Audio"


def sounds():
    for path in sorted(unreal.EditorAssetLibrary.list_assets(AUDIO_DIR)):
        asset = unreal.EditorAssetLibrary.load_asset(path)
        if isinstance(asset, unreal.SoundWave):
            yield asset


def export(folder):
    os.makedirs(folder, exist_ok=True)
    done = []
    for sound in sounds():
        task = unreal.AssetExportTask()
        task.object = sound
        task.filename = os.path.join(folder, sound.get_name() + ".wav")
        task.automated = True
        task.prompt = False
        task.replace_identical = True
        if unreal.Exporter.run_asset_export_task(task):
            done.append(sound.get_name())
    unreal.log("audio_levels: exported " + ", ".join(done))


def apply(json_path, only=None):
    with open(json_path, "r", encoding="utf-8-sig") as handle:
        volumes = json.load(handle)
    if only:
        # Только перечисленные звуки: остальным громкость могли подобрать вручную.
        volumes = {name: volume for name, volume in volumes.items() if name in only}
    done = []
    for sound in sounds():
        if sound.get_name() in volumes:
            sound.set_editor_property("volume", float(volumes[sound.get_name()]))
            unreal.EditorAssetLibrary.save_loaded_asset(sound)
            done.append("%s=%.2f" % (sound.get_name(), volumes[sound.get_name()]))
    unreal.log("audio_levels: applied " + ", ".join(done))


if len(sys.argv) > 2 and sys.argv[1] == "export":
    export(sys.argv[2])
elif len(sys.argv) > 2 and sys.argv[1] == "apply":
    apply(sys.argv[2], sys.argv[3].split(",") if len(sys.argv) > 3 else None)
else:
    unreal.log_error("audio_levels: usage: export <folder> | apply <file.json>")
