# Импортирует FBX-модели мебели в Content/Obshaga/Map/Furniture (без материалов и текстур: красим сами).
# Запуск (редактор закрыт):
#   UnrealEditor-Cmd.exe <Obshaga.uproject> -run=pythonscript -script="tools/import_models.py <папка с FBX> name1,name2,..."
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
        options.import_materials = False
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


if len(sys.argv) > 2:
    main(sys.argv[1], sys.argv[2].split(","))
else:
    unreal.log_error("import_models: usage: <folder> name1,name2,...")
