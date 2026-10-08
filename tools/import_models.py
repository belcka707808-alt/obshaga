# Импортирует FBX-модели мебели в Content/Obshaga/Map/Furniture вместе с их материалами (родные цвета набора).
# Запуск (редактор закрыт). Путь к скрипту — полный: относительный движок ищет от Engine/Binaries/Win64.
# Папку-источник можно дать относительно папки проекта.
#   UnrealEditor-Cmd.exe <Obshaga.uproject> -run=pythonscript -script="C:/полный/путь/к/проекту/tools/import_models.py <папка с FBX> name1,name2,..."
# Модель name.fbx становится ассетом SM_<Name>.
import os
import sys

import unreal

DEST = "/Game/Obshaga/Map/Furniture"


def main(source_dir, names):
    tasks = []
    for name in names:
        options = unreal.FbxImportUI()
        options.import_mesh = True
        options.import_as_skeletal = False
        options.import_materials = True
        options.import_textures = False
        options.import_animations = False
        options.static_mesh_import_data.combine_meshes = True
        options.static_mesh_import_data.auto_generate_collision = False

        task = unreal.AssetImportTask()
        task.filename = os.path.join(source_dir, name + ".fbx")
        task.destination_path = DEST
        task.destination_name = "SM_" + name[0].upper() + name[1:]
        task.automated = True
        task.replace_existing = True
        task.save = True
        task.options = options
        tasks.append(task)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

    done = []
    for task in tasks:
        mesh = unreal.EditorAssetLibrary.load_asset(DEST + "/" + task.destination_name)
        if mesh:
            box = mesh.get_bounding_box()
            size = box.max - box.min
            done.append("%s(%.0fx%.0fx%.0f)" % (task.destination_name, size.x, size.y, size.z))
        else:
            unreal.log_warning("import_models: not imported: " + task.filename)
    unreal.log("import_models: " + ", ".join(done))


def resolve(path):
    # Относительную папку считаем от папки проекта.
    return path if os.path.isabs(path) else os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), path)


if len(sys.argv) > 2:
    main(resolve(sys.argv[1]), sys.argv[2].split(","))
else:
    unreal.log_error("import_models: usage: <folder> name1,name2,...")
