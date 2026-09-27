# M3a material builder - run ONCE in the GUI editor (idempotent, safe to rerun):
#   UnrealEditor.exe WorldGen.uproject -ExecutePythonScript="Tools\m3a_make_materials.py"
#
# 1. imports the project-owned procedural textures (Data/Assets/m3a/tex, made by
#    Tools/m3a_make_textures.py) into /Game/Generated/Textures
# 2. builds every scene material in /Game/Generated/Materials with WORLD-SPACE
#    TRIPLANAR projection, so the UV-less Hunyuan3D desk mesh and the scaled
#    placeholder cubes/walls all get correctly tiled detail (see wg_matlib.py)
import os
import sys
import json

import unreal

# Public-repo path resolution: derive every on-disk path from the UE
# project directory. (The original local build hardcoded absolute E:/
# paths; see README -> "Differences from the internal version".)
_ROOT = unreal.Paths.project_dir()
if not _ROOT.endswith(("/", "\\")):
    _ROOT += "/"

TOOLS = _ROOT + "Tools"
sys.path.append(TOOLS)
import wg_matlib as L  # noqa: E402

TEX_SRC = _ROOT + "Data/Assets/m3a/tex"
TEX_DST = "/Game/Generated/Textures"
REPORT = _ROOT + "Data/Reports/m3a_materials_report.json"

report = {"textures": [], "materials": [], "errors": []}

# ------------------------------------------------------------------- textures
MAPS = [
    ("T_Wood_D", True), ("T_Wood_R", False),
    ("T_Crate_D", True), ("T_Crate_R", False),
    ("T_Wall_D", True), ("T_Wall_R", False),
    ("T_Floor_D", True), ("T_Floor_R", False),
    ("T_Apron_D", True), ("T_Apron_R", False),
]
tasks = []
for name, _srgb in MAPS:
    png = os.path.join(TEX_SRC, name + ".png")
    if not os.path.isfile(png):
        report["errors"].append("missing texture source " + png)
        continue
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", png)
    t.set_editor_property("destination_path", TEX_DST)
    t.set_editor_property("destination_name", name)
    t.set_editor_property("automated", True)
    t.set_editor_property("replace_existing", True)
    t.set_editor_property("save", True)
    tasks.append((name, t))

if tasks:
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t for _, t in tasks])

for name, srgb in MAPS:
    path = TEX_DST + "/" + name
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        report["errors"].append("import failed: " + path)
        continue
    tex = unreal.EditorAssetLibrary.load_asset(path)
    try:
        tex.set_editor_property("srgb", bool(srgb))
    except Exception as e:
        report["errors"].append("srgb set failed on %s: %s" % (name, e))
    if not srgb:
        for cs in ("TC_GRAYSCALE", "TC_Grayscale"):
            try:
                tex.set_editor_property("compression_settings",
                                        getattr(unreal.TextureCompressionSettings, cs))
                break
            except Exception:
                continue
    try:
        tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD)
        tex.set_editor_property("filter", unreal.TextureFilter.TF_BILINEAR)
    except Exception as e:
        report["errors"].append("tex opts failed on %s: %s" % (name, e))
    unreal.EditorAssetLibrary.save_loaded_asset(tex)
    report["textures"].append({"name": name, "path": path, "srgb": bool(srgb)})
    L.log("texture imported: %s (srgb=%s)" % (path, srgb))

# ------------------------------------------------------------------ materials
T = lambda n: TEX_DST + "/" + n  # noqa: E731

SPECS = [
    # name,            base,        rough,        scale(1/cm), tint,                    lo,   hi,  metal, emissive
    ("M_DeskWood", T("T_Wood_D"),  T("T_Wood_R"),  1 / 60.0,  (1.00, 1.00, 1.00), 0.30, 0.62, 0.0, None),
    ("M_Crate",    T("T_Crate_D"), T("T_Crate_R"), 1 / 50.0,  (1.00, 1.00, 1.00), 0.55, 0.85, 0.0, None),
    ("M_Floor",    T("T_Floor_D"), T("T_Floor_R"), 1 / 100.0, (1.00, 1.00, 1.00), 0.45, 0.82, 0.0, None),
    ("M_Apron",    T("T_Apron_D"), T("T_Apron_R"), 1 / 200.0, (1.00, 1.00, 1.00), 0.70, 0.95, 0.0, None),
    ("M_Wall_A",   T("T_Wall_D"),  T("T_Wall_R"),  1 / 250.0, (0.86, 0.80, 0.70), 0.85, 0.95, 0.0, None),
    ("M_Wall_B",   T("T_Wall_D"),  T("T_Wall_R"),  1 / 250.0, (0.70, 0.77, 0.85), 0.85, 0.95, 0.0, None),
    ("M_Wall_C",   T("T_Wall_D"),  T("T_Wall_R"),  1 / 250.0, (0.90, 0.83, 0.71), 0.85, 0.95, 0.0, None),
    ("M_Door",     T("T_Wood_D"),  T("T_Wood_R"),  1 / 120.0, (0.07, 0.42, 0.38), 0.22, 0.42, 0.0, (0.02, 0.11, 0.10)),
]
for name, b, r, sc, tint, lo, hi, met, em in SPECS:
    try:
        L.build_textured_material(name, b, r, sc, tint=tint, rough_lo=lo, rough_hi=hi,
                                  metallic=met, emissive=em)
        report["materials"].append({"name": name, "base": b, "rough": r,
                                    "tile_cm": round(1.0 / sc, 1), "tint": list(tint)})
    except Exception as e:  # noqa: BLE001
        report["errors"].append("material %s failed: %s: %s" % (name, type(e).__name__, e))
        L.log("ERROR material %s: %s" % (name, e))

# M_Key is an unlit-ish metal: no texture needed, just gold + glow so the quest
# item is unmistakable in a wide shot.
try:
    kpath = L.MAT_DIR + "/M_Key"
    if unreal.EditorAssetLibrary.does_asset_exist(kpath):
        unreal.EditorAssetLibrary.delete_asset(kpath)
    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "M_Key", L.MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
    L.set_prop(mat, L.vector(mat, 1.00, 0.76, 0.22, -800, -200), "",
               unreal.MaterialProperty.MP_BASE_COLOR)
    # M3a fix #2: metallic=1.0 with no reflection captures renders the key as a
    # black silhouette (the room has no env map) - worse than the white ball.
    # Partial metal + warm base + modest emissive reads as GOLD everywhere.
    L.set_prop(mat, L.vector(mat, 1.00, 0.78, 0.30, -800, -200), "",
               unreal.MaterialProperty.MP_BASE_COLOR)
    L.set_prop(mat, L.scalar(mat, 0.38, -800, 0), "", unreal.MaterialProperty.MP_ROUGHNESS)
    L.set_prop(mat, L.scalar(mat, 0.45, -800, 200), "", unreal.MaterialProperty.MP_METALLIC)
    L.set_prop(mat, L.vector(mat, 0.30, 0.19, 0.04, -800, 400), "",
               unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    L.MEL.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat)
    report["materials"].append({"name": "M_Key", "base": "procedural", "desc": "gold + emissive"})
    L.log("material built: M_Key")
    # Bake M_Key into the key prop's static-mesh asset slot. Component-level
    # overrides on WorldGenPickup.KeyMesh proved unreliable in captures; the
    # asset-level slot is the source of truth (see Docs/M3a_VisualAndWeb.md).
    try:
        kmesh = unreal.EditorAssetLibrary.load_asset("/Game/Generated/Assets/SM_Key_Prop")
        if kmesh is not None:
            kmesh.set_material(0, mat)
            unreal.EditorAssetLibrary.save_loaded_asset(kmesh)
            L.log("key prop material baked: SM_Key_Prop slot0 -> M_Key")
    except Exception as e:  # noqa: BLE001
        report["errors"].append("key prop material bake failed: %s" % e)
except Exception as e:  # noqa: BLE001
    report["errors"].append("M_Key failed: %s" % e)

with open(REPORT, "w", encoding="utf-8") as f:
    json.dump(report, f, ensure_ascii=False, indent=1)
L.log("report: %s (%d textures, %d materials, %d errors)"
      % (REPORT, len(report["textures"]), len(report["materials"]), len(report["errors"])))
unreal.SystemLibrary.quit_editor()
