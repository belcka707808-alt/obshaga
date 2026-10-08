# Импортирует звуки в Content/Obshaga/Audio и назначает их в ассет DA_Audio.
# Запуск (редактор закрыт):
#   UnrealEditor-Cmd.exe <Obshaga.uproject> -run=pythonscript -script="tools/import_audio.py <папка со звуками>"
# Имя файла решает, куда звук попадёт: S_Heartbeat.wav -> ячейка Heartbeat, S_Door.wav -> Door и т.д.
# Файлы с именем S_Music*.wav помечаются зацикленными.
import os
import sys

import unreal

AUDIO_DIR = "/Game/Obshaga/Audio"
CONFIG_PATH = AUDIO_DIR + "/DA_Audio"

# имя файла без расширения -> свойство UObshagaAudioConfig
SLOTS = {
    "S_Heartbeat": "heartbeat",
    "S_ItemImpactLight": "item_impact_light",
    "S_ItemImpactHeavy": "item_impact_heavy",
    "S_Door": "door",
    "S_DeviceBreak": "device_break",
    "S_HidingSpotRustle": "hiding_spot_rustle",
    "S_Footstep": "footstep",
    "S_ChaseStart": "chase_start",
    "S_Notice": "notice",
    "S_Caught": "caught",
    "S_MusicCalm": "music_calm",
    "S_MusicTense": "music_tense",
    "S_MusicResults": "music_results",
}


def main(source_dir):
    files = [f for f in sorted(os.listdir(source_dir)) if f.lower().endswith((".wav", ".ogg"))]
    tasks = []
    for name in files:
        task = unreal.AssetImportTask()
        task.filename = os.path.join(source_dir, name)
        task.destination_path = AUDIO_DIR
        task.automated = True
        task.replace_existing = True
        task.save = True
        tasks.append(task)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

    config = unreal.EditorAssetLibrary.load_asset(CONFIG_PATH)
    if not config:
        config_class = unreal.load_class(None, "/Script/Obshaga.ObshagaAudioConfig")
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", config_class)
        config = unreal.AssetToolsHelpers.get_asset_tools().create_asset("DA_Audio", AUDIO_DIR, config_class, factory)

    assigned = []
    for name in files:
        base = os.path.splitext(name)[0]
        sound = unreal.EditorAssetLibrary.load_asset(AUDIO_DIR + "/" + base)
        if not sound:
            unreal.log_warning("import_audio: not imported: " + name)
            continue
        if base.startswith("S_Music"):
            sound.set_editor_property("looping", True)
            unreal.EditorAssetLibrary.save_loaded_asset(sound)
        if base in SLOTS:
            config.set_editor_property(SLOTS[base], sound)
            assigned.append(base)
    unreal.EditorAssetLibrary.save_loaded_asset(config)
    unreal.log("import_audio: imported %d files, assigned: %s" % (len(files), ", ".join(assigned)))


if len(sys.argv) > 1:
    main(sys.argv[1])
else:
    unreal.log_error("import_audio: no source folder given")
