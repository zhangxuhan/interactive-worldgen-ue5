# M3a: import the procedural key prop into UE (no collision needed - it is a
# pure visual on WorldGenPickup.KeyMesh which is NoCollision already).
#   UnrealEditor.exe WorldGen.uproject -ExecutePythonScript="<this file>"
import unreal

# Public-repo path resolution: derive every on-disk path from the UE
# project directory. (The original local build hardcoded absolute E:/
# paths; see README -> "Differences from the internal version".)
_ROOT = unreal.Paths.project_dir()
if not _ROOT.endswith(("/", "\\")):
    _ROOT += "/"
import os

OBJ_PATH = _ROOT + "Data/Assets/m3a/key/SM_Key_Prop.obj"
DEST = "/Game/Generated/Assets"
ASSET_NAME = "SM_Key_Prop"


def log(msg):
    unreal.log("[M3aKey] " + msg)


def main():
    if unreal.EditorAssetLibrary.does_asset_exist("%s/%s" % (DEST, ASSET_NAME)):
        unreal.EditorAssetLibrary.delete_asset("%s/%s" % (DEST, ASSET_NAME))
        log("deleted previous import")
    task = unreal.AssetImportTask()
    task.filename = OBJ_PATH
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
    b = mesh.get_bounding_box()
    ext = (b.max.x - b.min.x, b.max.y - b.min.y, b.max.z - b.min.z)
    log("imported %s  extent %.1f x %.1f x %.1f cm" % (path, ext[0], ext[1], ext[2]))
    unreal.EditorAssetLibrary.save_directory(DEST, True)
    log("done")


main()
