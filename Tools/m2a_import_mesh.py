# M2a: import the normalized OBJ into UE and set up simple box collision.
# Run via the GUI editor:
#   UnrealEditor.exe WorldGen.uproject -ExecutePythonScript="<this file>" -abslog="<log>"
import unreal

# Public-repo path resolution: derive every on-disk path from the UE
# project directory. (The original local build hardcoded absolute E:/
# paths; see README -> "Differences from the internal version".)
_ROOT = unreal.Paths.project_dir()
if not _ROOT.endswith(("/", "\\")):
    _ROOT += "/"
import json
import os

OBJ_PATH = _ROOT + "Data/Assets/m2a/normalized/normalized_mesh.obj"
DEST = "/Game/Generated/Assets"
ASSET_NAME = "M2A_desk_001"
TARGET = (140.0, 70.0, 75.0)  # cm, must match normalize_report final_extents
REPORT_PATH = _ROOT + "Data/Reports/m2a_import_report.json"


def log(msg):
    unreal.log("[M2aImport] " + msg)


def main():
    obj_path = OBJ_PATH
    if not os.path.isfile(obj_path):
        raise RuntimeError("normalized obj not found: " + obj_path)

    # Re-import idempotency: delete previous import first.
    if unreal.EditorAssetLibrary.does_asset_exist("%s/%s" % (DEST, ASSET_NAME)):
        ok = unreal.EditorAssetLibrary.delete_asset("%s/%s" % (DEST, ASSET_NAME))
        log("deleted previous import -> %s" % ok)

    task = unreal.AssetImportTask()
    task.filename = obj_path
    task.destination_path = DEST
    task.destination_name = ASSET_NAME
    task.automated = True
    task.save = True
    task.replace_existing = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    path = "%s/%s" % (DEST, ASSET_NAME)
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        raise RuntimeError("import failed: " + path)
    mesh = unreal.EditorAssetLibrary.load_asset(path)
    log("imported: %s" % path)

    # --- verify imported bounds against the manifest target footprint
    bounds = mesh.get_bounding_box()
    ext = [bounds.max.x - bounds.min.x, bounds.max.y - bounds.min.y, bounds.max.z - bounds.min.z]
    log("bounds extent (cm): %.2f x %.2f x %.2f (target %.1f x %.1f x %.1f)"
        % (ext[0], ext[1], ext[2], TARGET[0], TARGET[1], TARGET[2]))
    tol = 1.0
    size_ok = all(abs(ext[i] - TARGET[i]) <= tol for i in range(3))
    log("size check: %s" % ("OK" if size_ok else "MISMATCH"))

    # --- simple box collision covering the full footprint (keeps blocking
    # behaviour equivalent to the placeholder cube; prevents the character
    # from clipping into legs or getting trapped under the top)
    # UE5.7: AddSimpleCollisionsWithNotification returns the index of the new
    # primitive (0 for the first box) or -1 / INDEX_NONE on failure.
    sms = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    ret = sms.add_simple_collisions_with_notification(
        mesh, unreal.ScriptCollisionShapeType.BOX, True)
    ok_col = ret >= 0
    log("simple collision box added: %s (prim index %d)" % (ok_col, ret))
    unreal.EditorAssetLibrary.save_asset(path)

    # Triangle count: 5.7 has no python LOD accessor; use the OBJ source face
    # count from the normalize report (all triangles, 1:1 with UE LOD0).
    with open(_ROOT + "Data/Assets/m2a/normalized/normalize_report.json") as f:
        nr = json.load(f)
    tris = nr["normalized_faces"]
    log("triangles (from normalized OBJ): %d" % tris)
    report = {
        "ue_asset_path": path,
        "extents_cm": [round(e, 2) for e in ext],
        "size_check_ok": bool(size_ok),
        "collision_box_added": bool(ok_col),
        "triangles": int(tris),
    }
    with open(REPORT_PATH, "w") as f:
        json.dump(report, f, indent=2)
    log("report written: %s" % REPORT_PATH)
    unreal.SystemLibrary.quit_editor()


main()
