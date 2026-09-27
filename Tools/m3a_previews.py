# M3a scene previews - run in the GUI editor with -ExecutePythonScript.
#
# Produces, for ONE level, from the CURRENTLY REBUILT map:
#   preview_overview.png    oblique top-down wide shot (room layout, desk, key,
#                           door). The walls between the camera and the room are
#                           hidden for this one shot (restored right after) so
#                           the layout is not swallowed by the near wall.
#   preview_room_eye.png    eye-level shot from the player start: what the
#                           player actually sees, all walls present
#   preview_desk_key.png    mid close-up of the desk + the floating key
#   preview_key.png         tight close-up on the key
#   preview_exit.png        close-up of the (locked) exit door from inside
#   showcase/<p>_%05d.png   a camera path INSIDE the room, rendered frame by
#                           frame (one rendered frame == one video frame; this
#                           is an editor camera path, NOT gameplay, NOT the E2E
#                           recording, and it is not time-compressed)
#   preview_manifest.json   scene_id / spec / level / timestamp / camera poses,
#                           so a web page can never show a stale image by accident
#
# Input: Tools/m3a_preview_config.json
#   {"level": "/Game/Generated/X", "scene_id": "...", "spec": "Data/Demo/scene_A/scene_spec.json",
#    "out_dir": "...", "prefix": "A", "entrance_wall": "west",
#    "showcase_frames": 240, "showcase_fps": 20,
#    "exposure": {"mode": "auto|basic|manual", "min_brightness": 8.0, "max_brightness": 12.0,
#                 "ev100": 11.0}}
#
# UE 5.7 notes verified by experiment (Tools/m3a_exposure_probe.py):
#   * unreal.AutoExposureMethod has AEM_BASIC / AEM_HISTOGRAM / AEM_MANUAL - there
#     is NO AEM_AUTO; the automatic metering mode is AEM_HISTOGRAM and the manual
#     mode reads EV100 from auto_exposure_bias. Getting this wrong silently
#     leaves the capture at the default exposure and the stills come out black.
#   * export_render_target may write the image WITHOUT a file extension; this
#     script normalises the name to <stem>.png afterwards.
import json
import math
import os
import time

import unreal

# Public-repo path resolution: derive every on-disk path from the UE
# project directory. (The original local build hardcoded absolute E:/
# paths; see README -> "Differences from the internal version".)
_ROOT = unreal.Paths.project_dir()
if not _ROOT.endswith(("/", "\\")):
    _ROOT += "/"

CFG_PATH = _ROOT + "Tools/m3a_preview_config.json"
with open(CFG_PATH, "r") as f:
    CFG = json.load(f)

LEVEL = CFG["level"]
OUT = CFG["out_dir"]
PREFIX = CFG.get("prefix", "x")
SCENE_ID = CFG.get("scene_id", "unknown")
N_SHOW = int(CFG.get("showcase_frames", 240))
FPS_SHOW = int(CFG.get("showcase_fps", 20))
EXP = CFG.get("exposure", {})
os.makedirs(OUT, exist_ok=True)
SHOW_DIR = os.path.join(OUT, "showcase")
os.makedirs(SHOW_DIR, exist_ok=True)
for d in (OUT, SHOW_DIR):
    for f in os.listdir(d):
        p = os.path.join(d, f)
        if os.path.isfile(p):
            os.remove(p)

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
act = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
if not les.load_level(LEVEL):
    unreal.log_error("[PREVIEW] cannot load %s" % LEVEL)
    raise SystemExit(1)
unreal.log("[PREVIEW] level loaded: %s" % LEVEL)

# ---------------------------------------------------------------- level facts
actors = list(act.get_all_level_actors())
shell_min = [1e9, 1e9, 1e9]
shell_max = [-1e9, -1e9, -1e9]
desk_actor = None
pickup_actor = None
door_actor = None
player_start = None
for a in actors:
    label = a.get_actor_label()
    cls = a.get_class().get_name()
    if cls == "StaticMeshActor":
        if label.startswith("DEMO_") or label.startswith("WG_Apron"):
            continue          # visual slab / outdoor apron: not part of the room shell
        if label.startswith("WG_obj_"):
            comp = a.get_editor_property("static_mesh_component")
            m = comp.get_editor_property("static_mesh")
            if m is not None and "M2A_desk_001" in m.get_path_name():
                desk_actor = a
            continue
        origin, extent = a.get_actor_bounds(False)
        for i, (o, e) in enumerate(((origin.x, extent.x), (origin.y, extent.y),
                                    (origin.z, extent.z))):
            shell_min[i] = min(shell_min[i], o - e)
            shell_max[i] = max(shell_max[i], o + e)
    elif cls == "WorldGenPickup":
        pickup_actor = a
    elif cls == "WorldGenDoor":
        door_actor = a
    elif cls == "PlayerStart":
        player_start = a

span_x = shell_max[0] - shell_min[0]
span_y = shell_max[1] - shell_min[1]
span_z = shell_max[2] - shell_min[2]
cx = (shell_min[0] + shell_max[0]) / 2.0
cy = (shell_min[1] + shell_max[1]) / 2.0
base_z = shell_min[2]
center = unreal.Vector(cx, cy, base_z + 0.45 * span_z)
unreal.log("[PREVIEW] shell x=[%.0f,%.0f] y=[%.0f,%.0f] z=[%.0f,%.0f] desk=%s door=%s key=%s"
           % (shell_min[0], shell_max[0], shell_min[1], shell_max[1], shell_min[2], shell_max[2],
              desk_actor.get_actor_label() if desk_actor else None,
              door_actor.get_actor_label() if door_actor else None,
              pickup_actor.get_actor_label() if pickup_actor else None))

desk_loc = desk_actor.get_actor_location() if desk_actor else unreal.Vector(cx, cy, 0)
key_loc = pickup_actor.get_actor_location() if pickup_actor else (desk_loc + unreal.Vector(0, 0, 100))
door_loc = door_actor.get_actor_location() if door_actor else unreal.Vector(cx, cy, 0)
door_yaw = door_actor.get_actor_rotation().yaw if door_actor else 0.0
if door_actor is not None:
    try:
        _drr = door_actor.get_editor_property("root_component").get_editor_property("relative_rotation")
        _drl = door_actor.get_editor_property("root_component").get_editor_property("relative_location")
        o, e = door_actor.get_actor_bounds(False)
        unreal.log("[PREVIEW] door diagnostic: actor_yaw=%.1f root_rel loc=(%.0f,%.0f,%.0f) rot=(%.1f,%.1f,%.1f)"
                   % (door_yaw, _drl.x, _drl.y, _drl.z, _drr.roll, _drr.pitch, _drr.yaw))
        unreal.log("[PREVIEW] door bounds origin=(%.1f,%.1f,%.1f) extent=(%.1f,%.1f,%.1f)"
                   % (o.x, o.y, o.z, e.x, e.y, e.z))
    except Exception as e:  # noqa: BLE001
        unreal.log("[PREVIEW] door diagnostic unavailable: %s" % e)

# outward-of-the-door direction (the door actor faces along its yaw)
_y = math.radians(door_yaw)
door_normal = unreal.Vector(math.cos(_y), math.sin(_y), 0.0)
room_dir_from_door = unreal.Vector(-door_normal.x, -door_normal.y, 0.0)

# ------------------------------------------------------------- capture rig
cap = act.spawn_actor_from_class(unreal.SceneCapture2D, unreal.Vector(cx, cy, 500))
cap.set_actor_label("M3A_Cap")
comp = cap.capture_component2d
comp.capture_source = unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR
comp.fov_angle = float(CFG.get("fov", 60.0))
comp.capture_every_frame = False

W_HI, H_HI = int(CFG.get("still_w", 1600)), int(CFG.get("still_h", 900))
W_LO, H_LO = int(CFG.get("clip_w", 1280)), int(CFG.get("clip_h", 720))

METHODS = {"auto": "AEM_HISTOGRAM", "histogram": "AEM_HISTOGRAM",
           "basic": "AEM_BASIC", "manual": "AEM_MANUAL"}
try:
    pp = comp.get_editor_property("post_process_settings")
    mode = str(EXP.get("mode", "auto")).lower()
    method = METHODS.get(mode, "AEM_HISTOGRAM")
    pp.set_editor_property("override_auto_exposure_method", True)
    pp.set_editor_property("auto_exposure_method", getattr(unreal.AutoExposureMethod, method))
    if method == "AEM_MANUAL":
        pp.set_editor_property("override_auto_exposure_bias", True)
        pp.set_editor_property("auto_exposure_bias", float(EXP.get("ev100", 11.0)))
    else:
        pp.set_editor_property("override_auto_exposure_min_brightness", True)
        pp.set_editor_property("auto_exposure_min_brightness", float(EXP.get("min_brightness", 8.0)))
        pp.set_editor_property("override_auto_exposure_max_brightness", True)
        pp.set_editor_property("auto_exposure_max_brightness", float(EXP.get("max_brightness", 12.0)))
    if EXP.get("bloom") is not None:
        pp.set_editor_property("override_bloom_intensity", True)
        pp.set_editor_property("bloom_intensity", float(EXP["bloom"]))
    comp.set_editor_property("post_process_settings", pp)
    unreal.log("[PREVIEW] exposure %s (%s) %s" % (mode, method, json.dumps(EXP)))
except Exception as e:  # noqa: BLE001
    unreal.log_error("[PREVIEW] exposure setup FAILED: %s" % e)

rt_hi = unreal.RenderingLibrary.create_render_target2d(
    cap, W_HI, H_HI, unreal.TextureRenderTargetFormat.RTF_RGBA8_SRGB,
    unreal.LinearColor(0.0, 0.0, 0.0, 1.0), False)
rt_lo = unreal.RenderingLibrary.create_render_target2d(
    cap, W_LO, H_LO, unreal.TextureRenderTargetFormat.RTF_RGBA8_SRGB,
    unreal.LinearColor(0.0, 0.0, 0.0, 1.0), False)


def shoot(rt, out_dir, stem, look_at, pos, fov=None, max_wait=30.0):
    if fov is not None:
        comp.fov_angle = float(fov)
    comp.texture_target = rt
    cap.set_actor_location(unreal.Vector(pos.x, pos.y, pos.z), False, False)
    cap.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(
        unreal.Vector(pos.x, pos.y, pos.z), unreal.Vector(look_at.x, look_at.y, look_at.z)), False)
    comp.capture_scene()
    before = set(os.listdir(out_dir))
    unreal.RenderingLibrary.export_render_target(cap, rt, out_dir, stem)
    deadline = time.time() + max_wait
    while time.time() < deadline:
        for f in set(os.listdir(out_dir)) - before:
            p = os.path.join(out_dir, f)
            if f.startswith(stem) and os.path.isfile(p) and os.path.getsize(p) > 10000:
                if not f.lower().endswith(".png"):
                    newp = os.path.join(out_dir, stem + ".png")
                    try:
                        os.replace(p, newp)
                    except OSError:
                        pass
                    else:
                        p = newp
                return p
        time.sleep(0.02)
    unreal.log_warning("[PREVIEW] timeout for %s" % stem)
    return None


shots = {}

# ---------------------------------------------------------------- wide shot
# Frame the whole room the way an architectural shot would: put the camera on
# the side OPPOSITE the exit door (so the door is always in view), at a steep
# elevation, and set the distance so the room's bounding sphere fills the frame
# (no guesswork, works for 5x7 and 9x5 rooms).
#
# Walls standing between the camera and the room are hidden FOR THIS SHOT ONLY
# and restored immediately afterwards. At ~50 deg elevation the near wall would
# otherwise swallow ~2.7 m of floor and could hide the desk / the key. This is
# a preview convention, not a level change: the saved umap is untouched and the
# E2E run always uses the real, closed room.
ov_fov = float(CFG.get("overview_fov", 60.0))
aspect = float(W_HI) / float(H_HI)
fov_v = 2.0 * math.degrees(math.atan(math.tan(math.radians(ov_fov) / 2.0) / aspect))
radius = 0.5 * math.sqrt(span_x ** 2 + span_y ** 2 + span_z ** 2)
dist = radius / math.sin(math.radians(min(ov_fov, fov_v) / 2.0)) * float(CFG.get("overview_fit", 1.02))

# camera azimuth: stand on the far side of the room from the door, then swing
# ~30 deg sideways for an oblique (rather than perfectly axial) composition
az_base = math.degrees(math.atan2(room_dir_from_door.y, room_dir_from_door.x))
az = math.radians(az_base + float(CFG.get("overview_az_offset", 30.0)))
el = math.radians(float(CFG.get("overview_elevation", 50.0)))
ov_pos = unreal.Vector(center.x + dist * math.cos(az) * math.cos(el),
                       center.y + dist * math.sin(az) * math.cos(el),
                       center.z + dist * math.sin(el))

cam_xy = (math.cos(az), math.sin(az))
wall_comps = []
for a in actors:
    if a.get_class().get_name() != "StaticMeshActor":
        continue
    if not a.get_actor_label().startswith("WG_Wall"):
        continue
    o, e = a.get_actor_bounds(False)
    dx, dy = o.x - cx, o.y - cy
    n = math.hypot(dx, dy)
    if n < 1.0:
        continue
    if (dx / n) * cam_xy[0] + (dy / n) * cam_xy[1] > 0.25:
        # NOTE: do not name this `comp` - it would shadow the module-level
        # SceneCaptureComponent2D and shoot() would then call fov_angle on a
        # StaticMeshComponent. (Same shadowing bug class that bit the builder.)
        wcomp = a.get_editor_property("static_mesh_component")
        wcomp.set_visibility(False)
        wall_comps.append((a.get_actor_label(), wcomp))
unreal.log("[PREVIEW] overview hides %d camera-side wall(s): %s"
           % (len(wall_comps), [w[0] for w in wall_comps]))
p = shoot(rt_hi, OUT, "preview_overview", center, ov_pos, ov_fov)
for _lbl, wcomp in wall_comps:
    wcomp.set_visibility(True)
shots["preview_overview"] = {"pos": [ov_pos.x, ov_pos.y, ov_pos.z],
                             "look": [center.x, center.y, center.z], "fov": ov_fov,
                             "hidden_walls": [w[0] for w in wall_comps],
                             "file": os.path.basename(p) if p else None}

# --------------------------------------------------- eye-level room shot
# The oblique shot is a diagram of the layout; this one shows what the player
# actually sees standing at the player start. Walls are NOT hidden here.
if player_start:
    _ep = player_start.get_actor_location()
    eye_pos = unreal.Vector(_ep.x, _ep.y, base_z + float(CFG.get("eye_height", 165.0)))
else:
    eye_pos = unreal.Vector(door_loc.x + room_dir_from_door.x * 150.0,
                            door_loc.y + room_dir_from_door.y * 150.0,
                            base_z + float(CFG.get("eye_height", 165.0)))
p = shoot(rt_hi, OUT, "preview_room_eye",
          unreal.Vector(cx, cy, base_z + 105.0), eye_pos, float(CFG.get("eye_fov", 78.0)))
shots["preview_room_eye"] = {"pos": [eye_pos.x, eye_pos.y, eye_pos.z],
                             "look": [cx, cy, base_z + 105.0],
                             "fov": float(CFG.get("eye_fov", 78.0)),
                             "file": os.path.basename(p) if p else None}

# ------------------------------------------------------------- desk + key
# The close-ups must not be blocked by another prop: in scene A the crate sits
# between the room centre and the desk and would fill the whole frame. Every
# candidate camera direction is therefore tested against the footprints of all
# other furniture (from the level itself, not hardcoded) and the first clear one
# is used.
obstacles = []
for a in actors:
    if a.get_class().get_name() != "StaticMeshActor":
        continue
    if not a.get_actor_label().startswith("WG_obj_") or a is desk_actor:
        continue
    o, e = a.get_actor_bounds(False)
    obstacles.append((o.x - e.x, o.y - e.y, o.x + e.x, o.y + e.y,
                      o.z - e.z, o.z + e.z))


def seg_hits_obstacle(p, q, margin=25.0):
    """2D segment p->q vs the axis-aligned footprints of the other props."""
    for (x0, y0, x1, y1, z0, z1) in obstacles:
        bx0, bx1 = x0 - margin, x1 + margin
        by0, by1 = y0 - margin, y1 + margin
        # Liang-Barsky clipping against the expanded box
        dx, dy = q.x - p.x, q.y - p.y
        t0, t1 = 0.0, 1.0
        ok = True
        for pp, dd, lo, hi in ((p.x, dx, bx0, bx1), (p.y, dy, by0, by1)):
            if abs(dd) < 1e-6:
                if pp < lo or pp > hi:
                    ok = False
                    break
            else:
                ta, tb = (lo - pp) / dd, (hi - pp) / dd
                if ta > tb:
                    ta, tb = tb, ta
                t0 = max(t0, ta)
                t1 = min(t1, tb)
                if t0 > t1:
                    ok = False
                    break
        if ok and t1 > 0.02:
            return True
    return False


def clear_direction(target, dist, prefer, want_z=None):
    """Pick a horizontal direction from `target` for a camera `dist` cm away
    whose sight line stays clear of the other props. Starts from `prefer`."""
    base = math.degrees(math.atan2(prefer.y, prefer.x))
    for off in (0, 35, -35, 70, -70, 105, -105, 140, -140, 180):
        az = math.radians(base + off)
        d = unreal.Vector(math.cos(az), math.sin(az), 0.0)
        z = target.z + (want_z if want_z is not None else 118.0)
        cam = unreal.Vector(target.x + d.x * dist, target.y + d.y * dist, z)
        if not seg_hits_obstacle(cam, target):
            return d, cam, off
    az = math.radians(base)
    d = unreal.Vector(math.cos(az), math.sin(az), 0.0)
    return d, unreal.Vector(target.x + d.x * dist, target.y + d.y * dist, target.z + 118.0), None


room_center_dir = unreal.Vector(cx - desk_loc.x, cy - desk_loc.y, 0.0)
if room_center_dir.length() < 0.1:
    room_center_dir = room_dir_from_door
desk_dir, dk_pos, off = clear_direction(desk_loc, float(CFG.get("desk_dist", 245.0)),
                                        room_center_dir, want_z=118.0)
unreal.log("[PREVIEW] desk camera direction offset=%s pos=%s" % (off, dk_pos))
dk_look = unreal.Vector(desk_loc.x, desk_loc.y, desk_loc.z + 30.0)
p = shoot(rt_hi, OUT, "preview_desk_key", dk_look, dk_pos, float(CFG.get("desk_fov", 55.0)))
shots["preview_desk_key"] = {"pos": [dk_pos.x, dk_pos.y, dk_pos.z], "file": os.path.basename(p) if p else None}

# M3a fix: the key prop floats 55 cm above the pickup origin (C++ KeyMesh
# offset). The old close-up aimed at the origin (desk surface) from below, so
# the key was entirely ABOVE the frame and the shot showed only the desk.
KEY_FLOAT_Z = 55.0
key_dir, k_pos, koff = clear_direction(key_loc, float(CFG.get("key_dist", 115.0)),
                                       room_center_dir, want_z=KEY_FLOAT_Z)
k_pos = unreal.Vector(k_pos.x, k_pos.y, key_loc.z + KEY_FLOAT_Z)
unreal.log("[PREVIEW] key camera direction offset=%s pos=%s" % (koff, k_pos))
p = shoot(rt_hi, OUT, "preview_key",
          unreal.Vector(key_loc.x, key_loc.y, key_loc.z + KEY_FLOAT_Z), k_pos,
          float(CFG.get("key_fov", 48.0)))
shots["preview_key"] = {"pos": [k_pos.x, k_pos.y, k_pos.z], "file": os.path.basename(p) if p else None}

# ------------------------------------------------------------------- door
# The door ACTOR origin is the HINGE (panel extends 120 cm along its local
# +Y). Framing the hinge put the exit camera almost IN the panel's plane, so
# the closed door rendered as a slanted fin ("half clipped into the wall").
# Frame the PANEL CENTER instead: hinge + half panel width along the panel
# direction, camera perpendicular to the wall through that center.
_py = math.radians(door_yaw)
panel_dir = unreal.Vector(-math.sin(_py), math.cos(_py), 0.0)
door_center = unreal.Vector(door_loc.x + panel_dir.x * 60.0,
                            door_loc.y + panel_dir.y * 60.0, door_loc.z)
unreal.log("[PREVIEW] door hinge=%s panel_dir=(%.2f,%.2f) center=(%.0f,%.0f)"
           % (door_loc, panel_dir.x, panel_dir.y, door_center.x, door_center.y))
e_dist = float(CFG.get("exit_dist", 330.0))
e_pos = unreal.Vector(door_center.x + room_dir_from_door.x * e_dist,
                      door_center.y + room_dir_from_door.y * e_dist,
                      base_z + float(CFG.get("eye_height", 165.0)))
p = shoot(rt_hi, OUT, "preview_exit", unreal.Vector(door_center.x, door_center.y, base_z + 115.0),
          e_pos, float(CFG.get("exit_fov", 60.0)))
shots["preview_exit"] = {"pos": [e_pos.x, e_pos.y, e_pos.z], "file": os.path.basename(p) if p else None}

# ------------------------------------------------------------ showcase clip
# Camera path inside the room: start near the player start, drift toward the desk
# (key in frame), then turn and walk up to the exit door.
start_pose = player_start.get_actor_location() if player_start else unreal.Vector(cx, cy, 100)
eye_z = base_z + float(CFG.get("eye_height", 165.0))
d1 = float(CFG.get("desk_dist", 245.0))
A = unreal.Vector(start_pose.x, start_pose.y, eye_z)
B = unreal.Vector(desk_loc.x + desk_dir.x * (d1 + 60.0), desk_loc.y + desk_dir.y * (d1 + 60.0), eye_z)
C = unreal.Vector(desk_loc.x - desk_dir.y * 150.0 + desk_dir.x * 190.0,
                  desk_loc.y + desk_dir.x * 150.0 + desk_dir.y * 190.0, eye_z)
D = unreal.Vector(door_center.x + room_dir_from_door.x * e_dist,
                  door_center.y + room_dir_from_door.y * e_dist, eye_z)
E = unreal.Vector(door_center.x + room_dir_from_door.x * 165.0,
                  door_center.y + room_dir_from_door.y * 165.0, eye_z)

waypoints = [
    (A, unreal.Vector(desk_loc.x, desk_loc.y, desk_loc.z + 40.0)),
    (B, unreal.Vector(desk_loc.x, desk_loc.y, desk_loc.z + 35.0)),
    (C, unreal.Vector(key_loc.x, key_loc.y, key_loc.z + 5.0)),
    (E, unreal.Vector(door_center.x, door_center.y, base_z + 115.0)),
    (D, unreal.Vector(door_center.x, door_center.y, base_z + 115.0)),
]
seg_starts = [0.0, 0.30, 0.52, 0.74, 1.0]


def smooth(t):
    return t * t * (3.0 - 2.0 * t)


def pose_at(u):
    u = min(max(u, 0.0), 1.0)
    for i in range(len(seg_starts) - 1):
        if seg_starts[i] <= u <= seg_starts[i + 1]:
            t = smooth((u - seg_starts[i]) / max(1e-6, seg_starts[i + 1] - seg_starts[i]))
            p0, l0 = waypoints[i]
            p1, l1 = waypoints[i + 1]
            return (unreal.Vector(p0.x + (p1.x - p0.x) * t, p0.y + (p1.y - p0.y) * t,
                                  p0.z + (p1.z - p0.z) * t),
                    unreal.Vector(l0.x + (l1.x - l0.x) * t, l0.y + (l1.y - l0.y) * t,
                                  l0.z + (l1.z - l0.z) * t))
    return waypoints[-1]


ok = 0
t0 = time.time()
for i in range(N_SHOW):
    pos, look = pose_at(i / float(max(1, N_SHOW - 1)))
    if shoot(rt_lo, SHOW_DIR, "%s_%05d" % (PREFIX, i), look, pos, float(CFG.get("clip_fov", 68.0))):
        ok += 1
unreal.log("[PREVIEW] showcase frames %d/%d in %.1f s -> %s" % (ok, N_SHOW, time.time() - t0, SHOW_DIR))

manifest = {
    "scene_id": SCENE_ID,
    "level": LEVEL,
    "spec": CFG.get("spec"),
    "generated_at": time.strftime("%Y-%m-%dT%H:%M:%S"),
    "note": ("Stills and showcase frames were rendered from the level as rebuilt in this run. "
             "The showcase clip is an editor camera path (one rendered frame per video frame); "
             "it is NOT the E2E gameplay recording."),
    "preview_convention": {
        "overview_hidden_walls": shots["preview_overview"].get("hidden_walls", []),
        "explain": ("preview_overview hides only the wall(s) standing between the camera and "
                    "the room so the layout stays readable at ~50 deg elevation; the umap is not "
                    "modified and every other shot (incl. preview_room_eye) shows all walls."),
    },
    "shell_bounds_cm": {"min": shell_min, "max": shell_max, "span": [span_x, span_y, span_z]},
    "actors": {
        "desk": desk_actor.get_actor_label() if desk_actor else None,
        "key": pickup_actor.get_actor_label() if pickup_actor else None,
        "exit_door": door_actor.get_actor_label() if door_actor else None,
        "player_start": player_start.get_actor_label() if player_start else None,
        "door_yaw_deg": door_yaw,
    },
    "camera": {"stills": shots, "clip_frames": ok, "clip_fps": FPS_SHOW,
               "clip_seconds": round(ok / float(FPS_SHOW), 2), "clip_dir": SHOW_DIR},
    "exposure": EXP,
    "files": sorted(f for f in os.listdir(OUT) if os.path.isfile(os.path.join(OUT, f))),
}
with open(os.path.join(OUT, "preview_manifest.json"), "w", encoding="utf-8") as f:
    json.dump(manifest, f, ensure_ascii=False, indent=1)
unreal.log("[PREVIEW] manifest written")
act.destroy_actor(cap)
unreal.SystemLibrary.quit_editor()
