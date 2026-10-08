# Импортирует персонажей Kenney Mini Characters в Content/Obshaga/Characters/Mini.
# Первый персонаж приносит скелет и анимации, остальные — только меш на том же скелете.
# Запуск (редактор закрыт). Путь к скрипту — полный: относительный движок ищет от Engine/Binaries/Win64.
# Папку-источник можно дать относительно папки проекта.
#   UnrealEditor-Cmd.exe <Obshaga.uproject> -run=pythonscript -script="C:/полный/путь/к/проекту/tools/import_characters.py <папка с FBX> first,second,..."
import os
import sys

import unreal

DEST = "/Game/Obshaga/Characters/Mini"


def asset_name(name):
    return "SK_" + "".join(part.capitalize() for part in name.replace("character-", "").split("-"))


def import_one(source_dir, name, skeleton):
    options = unreal.FbxImportUI()
    options.import_mesh = True
    options.import_as_skeletal = True
    options.mesh_type_to_import = unreal.FBXImportType.FBXIT_SKELETAL_MESH
    options.import_materials = True
    options.import_textures = True
    options.import_animations = skeleton is None
    options.create_physics_asset = False
    if skeleton is not None:
        options.skeleton = skeleton

    task = unreal.AssetImportTask()
    task.filename = os.path.join(source_dir, name + ".fbx")
    task.destination_path = DEST
    task.destination_name = asset_name(name)
    task.automated = True
    task.replace_existing = True
    task.save = True
    task.options = options
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    return unreal.EditorAssetLibrary.load_asset(DEST + "/" + task.destination_name)


def main(source_dir, names):
    skeleton = None
    for name in names:
        mesh = import_one(source_dir, name, skeleton)
        if not mesh:
            unreal.log_warning("import_characters: not imported: " + name)
            continue
        if skeleton is None:
            skeleton = mesh.get_editor_property("skeleton")
        bounds = mesh.get_bounds()
        unreal.log("import_characters: %s origin=(%.0f,%.0f,%.0f) extent=(%.0f,%.0f,%.0f)" % (
            mesh.get_name(), bounds.origin.x, bounds.origin.y, bounds.origin.z,
            bounds.box_extent.x, bounds.box_extent.y, bounds.box_extent.z))

    assets = unreal.EditorAssetLibrary.list_assets(DEST, recursive=True)
    unreal.log("import_characters: assets: " + ", ".join(sorted(a.split("/")[-1].split(".")[0] for a in assets)))


def resolve(path):
    # Относительную папку считаем от папки проекта.
    return path if os.path.isabs(path) else os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), path)


if len(sys.argv) > 2:
    main(resolve(sys.argv[1]), sys.argv[2].split(","))
else:
    unreal.log_error("import_characters: usage: <folder> first,second,...")
