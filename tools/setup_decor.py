# Создаёт или перезаписывает DA_Decor — оформление общаги: цвета пола и стен, лампы и украшения для каждого типа зоны.
# Запуск (редактор закрыт), путь к скрипту — полный:
#   UnrealEditor-Cmd.exe <Obshaga.uproject> -run=pythonscript -script="C:/полный/путь/tools/setup_decor.py"
# После запуска оформление можно править прямо в редакторе: ассет /Game/Obshaga/Map/Furniture/DA_Decor.
# Повторный запуск скрипта затрёт ручные правки.
import unreal

FOLDER = "/Game/Obshaga/Map/Furniture"
NAME = "DA_Decor"
WALL, HIGH, CENTER = unreal.DecorPlace.WALL, unreal.DecorPlace.WALL_HIGH, unreal.DecorPlace.CENTER
CREAM = (0.74, 0.68, 0.54)
WARM = (1.0, 0.8, 0.52)


def color(rgb):
    return unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0)


def entry(model, place=WALL, count=1, mount=130.0, scale=0.2):
    mesh = unreal.EditorAssetLibrary.load_asset("%s/SM_%s" % (FOLDER, model))
    if not mesh:
        unreal.log_warning("setup_decor: no model SM_%s" % model)
        return None
    item = unreal.DecorEntry()
    item.set_editor_property("model", mesh)
    item.set_editor_property("place", place)
    item.set_editor_property("count", count)
    item.set_editor_property("mount_height", mount)
    item.set_editor_property("scale", scale)
    return item


def style(room_type, floors, walls, lamp, decor, top=CREAM, lamp_color=WARM):
    result = unreal.RoomStyle()
    result.set_editor_property("room_type", room_type)
    result.set_editor_property("floor_colors", [color(c) for c in floors])
    result.set_editor_property("wall_colors", [color(c) for c in walls])
    result.set_editor_property("wall_top_color", color(top))
    result.set_editor_property("lamp_color", color(lamp_color))
    # Лампы выключены: 14 ламп без теней стоили 10 мс кадра на встроенной видеокарте (30 -> 24 кадра).
    # Вместо них краска стен и пола слегка светится сама (M_Paint). Включить лампу: убрать «* 0.0».
    result.set_editor_property("lamp_intensity", lamp * 0.0)
    result.set_editor_property("decor", [item for item in decor if item])
    return result


T = unreal.RoomType
STYLES = [
    # Жилые комнаты: у каждой свои обои и свой пол.
    style(T.BEDROOM,
          [(0.50, 0.30, 0.14), (0.42, 0.26, 0.16), (0.56, 0.36, 0.18), (0.38, 0.24, 0.13)],
          [(0.20, 0.50, 0.28), (0.85, 0.45, 0.18), (0.18, 0.40, 0.70), (0.72, 0.25, 0.38)],
          1.2,
          [entry("RugRectangle", CENTER), entry("BookcaseOpen"), entry("ChairDesk"), entry("PottedPlant"),
           entry("LampRoundFloor"), entry("Books", count=2), entry("Trashcan"), entry("CoatRackStanding"),
           entry("CardboardBoxOpen"), entry("SideTable"), entry("PlantSmall1"), entry("PlantSmall3"),
           entry("LampWall", HIGH, 1, 165.0)]),
    # Коридоры: линолеум и зелёная масляная краска.
    style(T.CORRIDOR,
          [(0.40, 0.24, 0.14), (0.34, 0.22, 0.16)],
          [(0.12, 0.38, 0.26), (0.14, 0.32, 0.46)],
          1.12,
          [entry("LampWall", HIGH, 4, 175.0), entry("PottedPlant", count=2), entry("BenchCushion"),
           entry("Trashcan"), entry("CardboardBoxOpen", count=2), entry("BookcaseOpenLow"),
           entry("CoatRackStanding"), entry("PlantSmall2", count=2), entry("RugDoormat", CENTER)]),
    # Вахта: дерево.
    style(T.VAHTA,
          [(0.46, 0.27, 0.12)],
          [(0.45, 0.24, 0.12)],
          1.4,
          [entry("RugSquare", CENTER), entry("ChairDesk"), entry("BookcaseOpen"), entry("PottedPlant"),
           entry("LampSquareFloor"), entry("CoatRackStanding"), entry("Trashcan"), entry("SideTable"),
           entry("Books", count=2), entry("LampWall", HIGH, 2, 170.0),
           entry("CardboardBoxOpen"), entry("PlantSmall3")]),
    # Кухня: светлая плитка и жёлтые стены.
    style(T.KITCHEN,
          [(0.78, 0.70, 0.52)],
          [(0.90, 0.68, 0.16)],
          1.54,
          [entry("RugRound", CENTER), entry("KitchenSink"), entry("KitchenFridgeSmall"), entry("Chair", count=3),
           entry("StoolBar", count=2), entry("Trashcan"), entry("PottedPlant"), entry("Washer"),
           entry("KitchenCabinetUpper", HIGH, 3, 150.0), entry("HoodModern", HIGH, 1, 155.0),
           entry("SideTable"), entry("PlantSmall1"), entry("LampWall", HIGH, 2, 185.0)]),
    # Гостиная: терракота и ковёр.
    style(T.LIVING_ROOM,
          [(0.44, 0.26, 0.20)],
          [(0.70, 0.30, 0.20)],
          1.54,
          [entry("RugRectangle", CENTER), entry("LoungeChair", count=2), entry("BookcaseOpen", count=2),
           entry("PottedPlant", count=2), entry("LampRoundFloor", count=2), entry("SideTable"), entry("Books", count=2),
           entry("BenchCushion"), entry("Trashcan"), entry("LampWall", HIGH, 2, 170.0), entry("PlantSmall2")]),
    # Санузел: бирюзовая плитка.
    style(T.BATHROOM,
          [(0.50, 0.74, 0.78)],
          [(0.16, 0.58, 0.64)],
          1.4,
          [entry("BathroomSink", count=2), entry("Toilet", count=2), entry("Washer"), entry("Dryer"),
           entry("Trashcan"), entry("BathroomMirror", HIGH, 2, 120.0), entry("LampWall", HIGH, 2, 180.0),
           entry("PlantSmall3"), entry("RugDoormat", CENTER)],
          top=(0.68, 0.78, 0.78), lamp_color=(0.85, 0.95, 1.0)),
    # Ночной выход: тамбур.
    style(T.NIGHT_EXIT,
          [(0.36, 0.30, 0.26)],
          [(0.50, 0.36, 0.20)],
          0.98,
          [entry("RugDoormat", CENTER), entry("CoatRackStanding"), entry("CardboardBoxOpen", count=2),
           entry("Trashcan"), entry("LampWall", HIGH, 1, 175.0), entry("PlantSmall2")]),
    # Лестницы.
    style(T.STAIRS,
          [(0.34, 0.30, 0.30)],
          [(0.16, 0.30, 0.50)],
          0.98,
          [entry("LampWall", HIGH, 2, 175.0), entry("PottedPlant"), entry("CardboardBoxOpen")]),
    # Балкон: стен нет, только пол.
    style(T.BALCONY, [(0.42, 0.40, 0.38)], [], 0.0, []),
]

# Краска, которая видна и в тени: цвет идёт и в основной цвет, и (умноженный на Glow) в собственное свечение.
mel = unreal.MaterialEditingLibrary
paint = unreal.EditorAssetLibrary.load_asset(FOLDER + "/M_Paint")
if not paint:
    paint = unreal.AssetToolsHelpers.get_asset_tools().create_asset("M_Paint", FOLDER, unreal.Material, unreal.MaterialFactoryNew())
mel.delete_all_material_expressions(paint)
paint_color = mel.create_material_expression(paint, unreal.MaterialExpressionVectorParameter, -600, 0)
paint_color.set_editor_property("parameter_name", "Color")
paint_color.set_editor_property("default_value", unreal.LinearColor(0.5, 0.5, 0.5, 1.0))
paint_glow = mel.create_material_expression(paint, unreal.MaterialExpressionScalarParameter, -600, 250)
paint_glow.set_editor_property("parameter_name", "Glow")
paint_glow.set_editor_property("default_value", 0.3)
paint_mul = mel.create_material_expression(paint, unreal.MaterialExpressionMultiply, -300, 150)
paint_rough = mel.create_material_expression(paint, unreal.MaterialExpressionConstant, -300, 350)
paint_rough.set_editor_property("r", 0.9)
mel.connect_material_expressions(paint_color, "", paint_mul, "A")
mel.connect_material_expressions(paint_glow, "", paint_mul, "B")
mel.connect_material_property(paint_color, "", unreal.MaterialProperty.MP_BASE_COLOR)
mel.connect_material_property(paint_mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
mel.connect_material_property(paint_rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
paint.set_editor_property("used_with_instanced_static_meshes", True)
mel.recompile_material(paint)
unreal.EditorAssetLibrary.save_loaded_asset(paint)
path = "%s/%s" % (FOLDER, NAME)
asset = unreal.EditorAssetLibrary.load_asset(path)
if not asset:
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(NAME, FOLDER, unreal.ObshagaDecorConfig, unreal.DataAssetFactory())
asset.set_editor_property("styles", STYLES)
unreal.EditorAssetLibrary.save_loaded_asset(asset)
unreal.log_warning("setup_decor: saved %s with %d styles, %d decor entries" % (path, len(STYLES), sum(len(s.get_editor_property("decor")) for s in STYLES)))
