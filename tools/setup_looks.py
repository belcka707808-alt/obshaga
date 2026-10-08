# Создаёт или обновляет ассет DA_Looks: какие модели у жильцов и коменданта и какая анимация на что.
# Запуск (редактор закрыт). Путь к скрипту — полный: относительный движок ищет от Engine/Binaries/Win64.
# Папку-источник можно дать относительно папки проекта.
#   UnrealEditor-Cmd.exe <Obshaga.uproject> -run=pythonscript -script="C:/полный/путь/к/проекту/tools/setup_looks.py"
import unreal

DIR = "/Game/Obshaga/Characters"
MINI = DIR + "/Mini/"
ANIM = MINI + "SK_MaleA"  # анимации импортированы вместе с первой моделью и названы SK_MaleA<имя>

RESIDENTS = ["SK_MaleA", "SK_FemaleA", "SK_MaleB", "SK_FemaleB", "SK_MaleC", "SK_FemaleC", "SK_MaleD", "SK_FemaleD"]
KOMENDANT = "SK_MaleF"

# свойство UObshagaLookConfig -> анимация из пака
ANIMS = {
    "idle": "idle",
    "walk": "walk",
    "sprint": "sprint",
    "crouch": "crouch",
    "carry": "holding-both",
    "pick_up": "pick-up",
    "throw": "interact-right",
    "fall": "fall",
    "caught": "sit",
}
GESTURES = ["emote-yes", "emote-no", "interact-left", "interact-right"]


def load(path):
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if not asset:
        unreal.log_warning("setup_looks: missing " + path)
    return asset


config = unreal.EditorAssetLibrary.load_asset(DIR + "/DA_Looks")
if not config:
    config_class = unreal.load_class(None, "/Script/Obshaga.ObshagaLookConfig")
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", config_class)
    config = unreal.AssetToolsHelpers.get_asset_tools().create_asset("DA_Looks", DIR, config_class, factory)

config.set_editor_property("resident_meshes", [m for m in (load(MINI + name) for name in RESIDENTS) if m])
config.set_editor_property("komendant_mesh", load(MINI + KOMENDANT))
for prop, anim in ANIMS.items():
    config.set_editor_property(prop, load(ANIM + anim))
config.set_editor_property("emote_gestures", [a for a in (load(ANIM + name) for name in GESTURES) if a])
unreal.EditorAssetLibrary.save_loaded_asset(config)

# Материалы моделей — для отчёта: видно, подхватилась ли текстура.
mesh = load(MINI + RESIDENTS[0])
materials = [str(m.get_editor_property("material_interface").get_path_name()) if m.get_editor_property("material_interface") else "None"
             for m in mesh.get_editor_property("materials")]
unreal.log("setup_looks: done; residents=%d; materials of %s: %s" % (len(RESIDENTS), RESIDENTS[0], ", ".join(materials)))
