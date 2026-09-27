# M2b: load a saved generated level in the GUI editor, dump every actor's
# label/class and (for StaticMeshActors) the assigned static mesh path, then
# quit. The dump is written to Data/Reports/m2b_umap_check_result.json and is
# checked OFFLINE - this proves what is actually persisted in the umap.
#
# Input: Data/Reports/m2b_umap_check_input.json = {"level_path": "/Game/..."}
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

INPUT_PATH = _ROOT + "Data/Reports/m2b_umap_check_input.json"
OUT_PATH = _ROOT + "Data/Reports/m2b_umap_check_result.json"


def log(msg):
    unreal.log("[M2bCheck] " + msg)


with open(INPUT_PATH, "r") as f:
    cfg = json.load(f)

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actor_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

# Decisive ordering probe: positional unreal.Rotator(a,b,c) misplacement check.
probe = unreal.Rotator(0.0, 0.0, 90.0)
log("ROTATOR PROBE positional (0,0,90) -> roll=%s pitch=%s yaw=%s"
    % (probe.roll, probe.pitch, probe.yaw))
probe2 = unreal.Rotator(roll=0.0, pitch=0.0, yaw=90.0)
log("ROTATOR PROBE keyword    (0,0,90) -> roll=%s pitch=%s yaw=%s"
    % (probe2.roll, probe2.pitch, probe2.yaw))

result = {"level_path": cfg["level_path"], "loaded": False, "actors": []}
if les.load_level(cfg["level_path"]):
    result["loaded"] = True
    log("level loaded: " + cfg["level_path"])
    rows = []
    for a in actor_sub.get_all_level_actors():
        row = {
            "label": a.get_actor_label(),
            "class": a.get_class().get_name(),
        }
        if row["class"] == "StaticMeshActor":
            try:
                comp = a.get_editor_property("static_mesh_component")
                m = comp.get_editor_property("static_mesh")
                row["mesh"] = m.get_path_name() if m is not None else None
            except Exception as e:  # noqa: BLE001
                row["mesh_error"] = str(e)
        loc = a.get_actor_location()
        row["location"] = [round(loc.x, 1), round(loc.y, 1), round(loc.z, 1)]
        try:
            r = a.get_actor_transform().rotation
            row["rotation"] = [round(r.roll, 1), round(r.pitch, 1), round(r.yaw, 1)]
        except Exception:  # noqa: BLE001
            pass
        rows.append(row)
    rows.sort(key=lambda r: r["label"])
    result["actor_count"] = len(rows)
    result["actors"] = rows
    log("dumped %d actors" % len(rows))
else:
    log("FAILED to load level: " + cfg["level_path"])

with open(OUT_PATH, "w") as f:
    json.dump(result, f, indent=1)
log("result written: " + OUT_PATH)

unreal.SystemLibrary.quit_editor()
