# Создаёт или перезаполняет DA_Story — тексты сюжета (жильцы, реплики коменданта, завязки вечера) из текстов, зашитых в код.
# Запуск (редактор закрыт), путь к скрипту — полный:
#   UnrealEditor-Cmd.exe <Obshaga.uproject> -run=pythonscript -script="C:/полный/путь/tools/setup_story.py"
# Повторный запуск затрёт ручные правки в ассете.
import unreal

FOLDER = "/Game/Obshaga/Data"
NAME = "DA_Story"
path = "%s/%s" % (FOLDER, NAME)
asset = unreal.EditorAssetLibrary.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
if not asset:
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(NAME, FOLDER, unreal.ObshagaStoryConfig, unreal.DataAssetFactory())
asset.fill_from_defaults()
unreal.EditorAssetLibrary.save_loaded_asset(asset)
unreal.log_warning("setup_story: saved %s: %d residents, %d line sets, %d stories" % (
    path, len(asset.get_editor_property("residents")), len(asset.get_editor_property("komendant_lines")), len(asset.get_editor_property("stories"))))
