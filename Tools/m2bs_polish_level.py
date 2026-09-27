# M2bs/M3a demo: VISUAL polish for a generated level.
# The plan's single low-angle sun + cubemap-less skylight leaves the room
# badly lit in screenshots and in -game. This step rebuilds the lighting rig
# (steep main sun, opposite fill, near-vertical top light, a warm accent over
# the desk and a cool accent at the exit door), adds a neutral ground slab so
# the room does not float in a black void, and re-asserts the generated
# materials. All MOVABLE (no lighting build needed). Geometry, collision and
# interactions are untouched; the E2E rerun after polishing re-validates the
# level. Idempotent: stale DEMO_* actors are removed first.
#
# UE5.7 Python gotcha (verified by experiment, Tools/m2bs_rotexp.py):
# positional unreal.Rotator(a, b, c) does NOT map to (pitch, yaw, roll) here -
# fields end up shifted. Always construct with KEYWORDS and verify by reading
# back after set_actor_rotation.
import unreal

# Public-repo path resolution: derive every on-disk path from the UE
# project directory. (The original local build hardcoded absolute E:/
# paths; see README -> "Differences from the internal version".)
_ROOT = unreal.Paths.project_dir()
if not _ROOT.endswith(("/", "\\")):
    _ROOT += "/"
import json
import sys

sys.path.append(_ROOT + "Tools")
import wg_matlib  # noqa: E402

CFG_PATH = _ROOT + "Tools/m2bs_polish_config.json"
with open(CFG_PATH, "r") as f:
    cfg = json.load(f)

LEVEL = cfg["level"]
SCENE_ID = cfg.get("scene_id") or LEVEL.rstrip("/").split("/")[-1]
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
act = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
if not les.load_level(LEVEL):
    unreal.log_error("[POLISH] cannot load %s" % LEVEL)
    raise SystemExit(1)
unreal.log("[POLISH] loaded %s" % LEVEL)


def make_rot(pitch, yaw, roll):
    r = unreal.Rotator(pitch=pitch, yaw=yaw, roll=roll)
    if abs(r.pitch - pitch) > 0.01 or abs(r.yaw - yaw) > 0.01 or abs(r.roll - roll) > 0.01:
        unreal.log_warning("[POLISH] keyword Rotator mismatch (%s), falling back" % r)
        r = unreal.Rotator(roll=roll, pitch=pitch, yaw=yaw)
    return r


def set_light_dir(actor, pitch, yaw):
    actor.set_actor_rotation(make_rot(pitch, yaw, 0.0), False)
    got = actor.get_actor_rotation()
    ok = abs(got.pitch - pitch) < 0.05 and abs(got.yaw - yaw) < 0.05
    unreal.log("[POLISH] set dir (%.0f, %.0f) -> readback (%.1f, %.1f, %.1f) %s" % (
        pitch, yaw, got.pitch, got.yaw, got.roll, "OK" if ok else "MISMATCH"))
    return ok


for a in act.get_all_level_actors():
    if a.get_actor_label().startswith("DEMO_"):
        act.destroy_actor(a)
        unreal.log("[POLISH] removed stale %s" % a.get_actor_label())

# --- rig calibration -------------------------------------------------------
# The engine exposure in this project is effectively FIXED. Measured on
# 2026-09-27: an override on the SceneCapture2D's post_process_settings, an
# unbound PostProcessVolume, and the r.DefaultFeature.AutoExposure.{Method,Bias}
# console variables (all verified to read back the values we set) leave the
# rendered frame BIT-IDENTICAL, and the -game view was just as washed out. The
# only knob honoured by BOTH the editor capture and gameplay is light
# intensity, so the rig below is calibrated by measurement (Tools/m3a_evprobe3.py
# and m3a_evprobe5.py, wide shot of scene A at 1600x900):
#     x1.000 -> mean 245.9  p50 249  walls clipped      = the "clay" look
#     x0.120 -> mean 178.4  p50 189
#     x0.070 -> mean 140.1  p50 150
#     x0.055 -> mean 120.3  p50 130
#     x0.040 -> mean  93.4  p50 102
# 0.06 targets mean ~128 / p50 ~140: plaster walls that still read as plaster,
# a floor that is clearly darker, and unclipped highlights.
LIGHT_RIG_SCALE = float(cfg.get("light_scale", 0.06))


def rig(v):
    """Apply the calibrated rig scale to a raw intensity."""
    return float(v) * LIGHT_RIG_SCALE


main_light = None
for a in act.get_all_level_actors():
    comps = a.get_components_by_class(unreal.DirectionalLightComponent)
    for c in comps:
        if a.get_actor_label().startswith("DEMO_"):
            continue
        c.set_editor_property("intensity", rig(120.0))   # lux (rig-scaled)
        c.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
        set_light_dir(a, -45.0, 30.0)
        main_light = a
        unreal.log("[POLISH] main sun adjusted: %s -> %.2f lux (x%.3f)"
                   % (a.get_actor_label(), rig(120.0), LIGHT_RIG_SCALE))
if main_light is None:
    unreal.log_warning("[POLISH] no directional light found")

# the plan's skylight is part of the calibrated rig: scale it too, otherwise the
# ambient would be left 16x too strong and the room would wash out again
for a in act.get_all_level_actors():
    comps = a.get_components_by_class(unreal.SkyLightComponent)
    for c in comps:
        if a.get_actor_label().startswith("DEMO_"):
            continue
        c.set_editor_property("intensity", rig(1.0))
        unreal.log("[POLISH] %s intensity -> %.4f (x%.3f)"
                   % (a.get_actor_label(), rig(1.0), LIGHT_RIG_SCALE))


def add_fill(label, pitch, yaw, intensity):
    a = act.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 500))
    a.set_actor_label(label)
    c = a.get_components_by_class(unreal.DirectionalLightComponent)[0]
    c.set_editor_property("intensity", rig(intensity))
    c.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    set_light_dir(a, pitch, yaw)
    unreal.log("[POLISH] added %s (pitch %.0f yaw %.0f lux %.2f)"
               % (label, pitch, yaw, rig(intensity)))


add_fill("DEMO_FillLight", -35.0, 200.0, 55.0)
add_fill("DEMO_TopLight", -89.0, 0.0, 70.0)
add_fill("DEMO_BounceLight", -12.0, 60.0, 30.0)

# ------------------------------------------------------- M3a visual additions
# Find the three landmarks the demo must make readable: the generated desk, the
# key on it and the exit door. Everything is derived from the level itself.
desk_actor = None
key_actor = None
door_actor = None
for a in act.get_all_level_actors():
    label = a.get_actor_label()
    cls = a.get_class().get_name()
    if cls == "StaticMeshActor" and label.startswith("WG_obj_"):
        comp = a.get_editor_property("static_mesh_component")
        m = comp.get_editor_property("static_mesh")
        if m is not None and "M2A_desk_001" in m.get_path_name():
            desk_actor = a
    elif cls == "WorldGenPickup":
        key_actor = a
    elif cls == "WorldGenDoor":
        door_actor = a


def add_point(label, loc, intensity, radius, color, attenuation=1.0):
    a = act.spawn_actor_from_class(unreal.PointLight, unreal.Vector(loc[0], loc[1], loc[2]))
    a.set_actor_label(label)
    c = a.get_components_by_class(unreal.PointLightComponent)[0]
    c.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    c.set_editor_property("intensity", float(intensity))
    c.set_editor_property("attenuation_radius", float(radius))
    c.set_editor_property("light_falloff_exponent", float(attenuation))
    try:
        c.set_editor_property("use_inverse_squared_falloff", True)
        c.set_editor_property("intensity_units", unreal.LightUnits.CANDELAS)
    except Exception as e:  # noqa: BLE001
        unreal.log_warning("[POLISH] point light units not set: %s" % e)
    try:
        c.set_editor_property("light_color", unreal.Color(int(color[0]), int(color[1]), int(color[2]), 255))
    except Exception as e:  # noqa: BLE001
        unreal.log_warning("[POLISH] point light color not set: %s" % e)
    unreal.log("[POLISH] added %s at %s intensity=%.0f radius=%.0f" % (label, loc, intensity, radius))
    return a


# Warm accent above the desk: makes the wooden table the brightest object in a
# wide shot and the key (emissive gold) reads as a highlight.
if desk_actor is not None:
    d = desk_actor.get_actor_location()
    add_point("DEMO_DeskLight", (d.x, d.y, d.z + 190.0), rig(cfg.get("desk_light", 260.0)),
              float(cfg.get("desk_light_radius", 700.0)), (255, 214, 170), 1.2)
else:
    unreal.log_warning("[POLISH] desk actor not found - no desk accent light")

# Cool accent in front of the exit door so the doorway is readable from inside.
if door_actor is not None:
    d = door_actor.get_actor_location()
    r = door_actor.get_actor_rotation()
    import math
    yawn = math.radians(r.yaw)
    inward = (-math.cos(yawn), -math.sin(yawn))
    add_point("DEMO_DoorLight", (d.x + inward[0] * 200.0, d.y + inward[1] * 200.0, d.z + 230.0),
              rig(cfg.get("door_light", 190.0)), float(cfg.get("door_light_radius", 600.0)),
              (170, 220, 255), 1.2)
else:
    unreal.log_warning("[POLISH] exit door actor not found - no door accent light")


# ------------------------------------------------------------- DEMO labels
# Floating text tags so a single wide shot answers "where did the AI put the
# desk / key / exit". Visual only: TextRenderActors have no collision and the
# DEMO_ prefix marks them as polish-owned (they are wiped on every rebuild).
def add_label(text, loc, color=(255, 255, 255), size=26.0):
    try:
        a = act.spawn_actor_from_class(unreal.TextRenderActor,
                                       unreal.Vector(loc[0], loc[1], loc[2]))
        a.set_actor_label("DEMO_Label_" + text)
        c = a.get_components_by_class(unreal.TextRenderComponent)[0]
        c.set_editor_property("text", text)
        c.set_editor_property("world_size", float(size))
        # alignment enums are not exposed under these names on 5.7 - optional
        for prop, val in (("horizontal_alignment", "TextHorizAlign"),
                          ("vertical_alignment", "TextVertAlign")):
            try:
                c.set_editor_property(prop, getattr(unreal, val).HRTA_CENTER
                                      if prop.startswith("h") else getattr(unreal, val).VRTA_CENTER)
            except Exception:  # noqa: BLE001
                pass  # default alignment is fine
        # face the room centre (text renders along the actor's +X)
        yaw = math.degrees(math.atan2(0.0 - loc[1], 0.0 - loc[0]))
        a.set_actor_rotation(unreal.Rotator(0.0, yaw, 0.0), False)
        try:
            c.set_editor_property("text_render_color",
                                  unreal.Color(color[0], color[1], color[2], 255))
        except Exception:  # noqa: BLE001
            pass
        unreal.log("[POLISH] label %s at (%.0f, %.0f, %.0f)" % (text, loc[0], loc[1], loc[2]))
    except Exception as e:  # noqa: BLE001
        unreal.log_warning("[POLISH] label %s failed: %s" % (text, e))


center_x, center_y = 0.0, 0.0
if key_actor is not None:
    try:
        km = key_actor.get_editor_property("KeyMesh")
        m = km.get_editor_property("static_mesh") if km else None
        unreal.log("[POLISH] key mesh = %s" % (m.get_path_name() if m else "NONE"))
    except Exception as e:  # noqa: BLE001
        unreal.log("[POLISH] key mesh introspect failed: %s" % e)
if desk_actor is not None:
    d = desk_actor.get_actor_location()
    add_label("DESK", (d.x, d.y, d.z + 260.0), (255, 214, 170), 30.0)
if key_actor is not None:
    k = key_actor.get_actor_location()
    add_label("KEY", (k.x, k.y, k.z + 150.0), (255, 230, 120), 26.0)
if door_actor is not None:
    d = door_actor.get_actor_location()
    add_label("EXIT", (d.x, d.y, d.z + 320.0), (170, 220, 255), 30.0)

# Ground slab: the room otherwise floats in a black void in every wide shot.
# Visual slab only (top face 1.5 cm BELOW the floor top so it can never be
# mistaken for or conflict with the real floor / apron collision planes).
ground = act.spawn_actor_from_class(unreal.StaticMeshActor,
                                    unreal.Vector(0.0, 0.0, -11.5))
ground.set_actor_label("DEMO_Ground")
gsize = float(cfg.get("ground_size_cm", 4200.0))
ground.set_actor_scale3d(unreal.Vector(gsize / 100.0, gsize / 100.0, 0.2))
gc = ground.get_editor_property("static_mesh_component")
gc.set_static_mesh(unreal.EditorAssetLibrary.load_asset("/Engine/BasicShapes/Cube.Cube"))
gmat = unreal.EditorAssetLibrary.load_asset(wg_matlib.MAT_DIR + "/M_Floor")
if gmat is not None:
    gc.set_material(0, gmat)
gc.set_collision_profile_name("NoCollision")   # never affects gameplay physics
unreal.log("[POLISH] added DEMO_Ground %.0f x %.0f cm (visual only, no collision)" % (gsize, gsize))

# Re-assert the generated materials so a polish-only rerun cannot leave a
# half-textured level behind.
try:
    wg_matlib.apply_scene_materials(SCENE_ID, act)
except Exception as e:  # noqa: BLE001
    unreal.log_warning("[POLISH] material re-assert failed: %s" % e)

les.save_current_level()
unreal.log("[POLISH] done, level saved")
unreal.SystemLibrary.quit_editor()
