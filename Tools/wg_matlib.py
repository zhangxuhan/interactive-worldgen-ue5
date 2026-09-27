# M3a shared material library for the generated-scene pipeline.
#
# WHY THIS EXISTS
# The Hunyuan3D-2.1 mesh (Content/Generated/Assets/M2A_desk_001) was normalized
# from a UV-less OBJ (grep "^vt " normalized_mesh.obj -> 0 hits), so a normal
# UV texture lookup produces a single texel and the desk renders as a flat
# white blob. We therefore texture every generated scene with WORLD-SPACE
# TRIPLANAR projection built from material nodes: each of the three principal
# axes is sampled with the two world coordinates that span it, and the three
# samples are blended by the absolute world-space vertex normal. No UV channel
# is required, and because the tile size is expressed in world centimetres the
# same material works for any actor scale (the walls/floor are scaled cubes).
#
# Materials are created ONCE by Tools/m3a_make_materials.py into
# /Game/Generated/Materials and are assigned here by actor label, so a level
# rebuild always reproduces the same look (no manual editor work).
import unreal

MAT_DIR = "/Game/Generated/Materials"
MEL = unreal.MaterialEditingLibrary

# Per-scene wall palettes (warm storage / cool showroom / warm square hall).
# Chosen deterministically from the scene_id so the same spec always gets the
# same walls, and the three demo scenes stay visibly distinct while sharing one
# overall look.
WALL_MATERIALS = ["M_Wall_A", "M_Wall_B", "M_Wall_C"]

# The three demo scenes are pinned to three DIFFERENT palettes (a plain hash
# could collide and make two demo rooms look identical). Any other scene_id
# still gets a stable palette from the hash, so a rerun reproduces it exactly.
PINNED_WALL = {
    "StorageRoomKeyQuest": 0,   # warm sand storage room
    "ShowroomHall_01": 1,       # cool blue-grey showroom
    "SquareHallKeyRoom": 2,     # warm light square hall
}


def log(msg):
    unreal.log("[WGMat] " + str(msg))


def wall_material_for_scene(scene_id):
    import zlib
    if scene_id in PINNED_WALL:
        idx = PINNED_WALL[scene_id]
    else:
        idx = zlib.crc32((scene_id or "scene").encode("utf-8")) % len(WALL_MATERIALS)
    return WALL_MATERIALS[idx], idx


def load_material(name):
    path = MAT_DIR + "/" + name
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        return None
    return unreal.EditorAssetLibrary.load_asset(path)


def _material_for_actor(actor, scene_id):
    label = actor.get_actor_label()
    cls = actor.get_class().get_name()
    if cls == "WorldGenDoor":
        return "M_Door"
    if cls == "WorldGenPickup":
        return "M_Key"
    if cls == "StaticMeshActor":
        if label.startswith("WG_Floor"):
            return "M_Floor"
        if label.startswith("WG_Apron"):
            return "M_Apron"
        if label.startswith("WG_Wall"):
            return wall_material_for_scene(scene_id)[0]
        if label.startswith("WG_obj_"):
            comp = actor.get_editor_property("static_mesh_component")
            mesh = comp.get_editor_property("static_mesh")
            path = mesh.get_path_name() if mesh is not None else ""
            if "M2A_desk_001" in path:
                return "M_DeskWood"
            return "M_Crate"
    return None


def apply_scene_materials(scene_id, actor_subsystem=None):
    """Assign the generated materials to every actor of the level that is
    currently open in the editor. Idempotent; called by the level builder (so a
    rebuild reproduces the look) and re-asserted by the polish step.
    Returns {material_name: count}."""
    act = actor_subsystem or unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    counts = {}
    missing = set()
    for actor in act.get_all_level_actors():
        name = _material_for_actor(actor, scene_id)
        if not name:
            continue
        mat = load_material(name)
        if mat is None:
            missing.add(name)
            continue
        comps = actor.get_components_by_class(unreal.StaticMeshComponent)
        for c in comps:
            c.set_material(0, mat)
        counts[name] = counts.get(name, 0) + 1
        if name.startswith("M_Wall"):
            log("wall material %s on %s" % (name, actor.get_actor_label()))
    if missing:
        log("WARNING missing materials (run Tools/m3a_make_materials.py): %s" % sorted(missing))
    log("materials applied: %s (scene_id=%s)" % (counts, scene_id))
    return counts


# --------------------------------------------------------------------------
# Material authoring helpers (used by m3a_make_materials.py)

def expr(mat, cls, x, y):
    return MEL.create_material_expression(mat, cls, x, y)


def conn(src, src_pin, dst, dst_pin=None):
    if dst_pin is None:
        try:
            names = [str(n) for n in MEL.get_input_names(dst)]
        except Exception:
            names = []
        dst_pin = names[0] if names else ""
    ok = MEL.connect_material_expressions(src, src_pin, dst, dst_pin)
    if not ok:
        raise RuntimeError("connect failed: %s.%s -> %s.%s"
                           % (src.get_class().get_name(), src_pin,
                              dst.get_class().get_name(), dst_pin))
    return dst


def set_prop(mat, src, src_pin, prop):
    ok = MEL.connect_material_property(src, src_pin, prop)
    if not ok:
        raise RuntimeError("connect_material_property failed for %s" % prop)
    return ok


def scalar(mat, v, x, y):
    e = expr(mat, unreal.MaterialExpressionConstant, x, y)
    e.set_editor_property("r", float(v))
    return e


def vector(mat, r, g, b, x, y):
    e = expr(mat, unreal.MaterialExpressionConstant3Vector, x, y)
    e.set_editor_property("constant", unreal.LinearColor(r, g, b, 1.0))
    return e


def vec_param(mat, name, r, g, b, x, y):
    e = expr(mat, unreal.MaterialExpressionVectorParameter, x, y)
    e.set_editor_property("parameter_name", name)
    e.set_editor_property("default_value", unreal.LinearColor(r, g, b, 1.0))
    return e


def scalar_param(mat, name, v, x, y):
    e = expr(mat, unreal.MaterialExpressionScalarParameter, x, y)
    e.set_editor_property("parameter_name", name)
    e.set_editor_property("default_value", float(v))
    return e


def mask(mat, src, r, g, b, x, y, src_pin=""):
    e = expr(mat, unreal.MaterialExpressionComponentMask, x, y)
    e.set_editor_property("r", bool(r))
    e.set_editor_property("g", bool(g))
    e.set_editor_property("b", bool(b))
    e.set_editor_property("a", False)
    conn(src, src_pin, e)
    return e


def mul(mat, a, a_pin, b, b_pin, x, y):
    e = expr(mat, unreal.MaterialExpressionMultiply, x, y)
    conn(a, a_pin, e, "A")
    conn(b, b_pin, e, "B")
    return e


def add(mat, a, a_pin, b, b_pin, x, y):
    e = expr(mat, unreal.MaterialExpressionAdd, x, y)
    conn(a, a_pin, e, "A")
    conn(b, b_pin, e, "B")
    return e


def div(mat, a, a_pin, b, b_pin, x, y):
    e = expr(mat, unreal.MaterialExpressionDivide, x, y)
    conn(a, a_pin, e, "A")
    conn(b, b_pin, e, "B")
    return e


def clamp01(mat, src, x, y):
    e = expr(mat, unreal.MaterialExpressionSaturate, x, y)
    conn(src, "", e)
    return e


def lerp(mat, alpha, a, b, x, y):
    """alpha is a 0..1 float, returns lerp(a, b, alpha)."""
    ia = expr(mat, unreal.MaterialExpressionOneMinus, x, y)
    conn(alpha, "", ia)
    ma = mul(mat, a, "", alpha, "", x, y + 40)
    mb = mul(mat, b, "", ia, "", x, y + 80)
    return add(mat, ma, "", mb, "", x, y + 120)


def sample_with_uv(mat, tex, uv, x, y):
    s = expr(mat, unreal.MaterialExpressionTextureSample, x, y)
    s.set_editor_property("texture", tex)
    pin = None
    try:
        names = [str(n) for n in MEL.get_input_names(s)]
    except Exception:
        names = []
    for cand in ("UVs", "UV", "Coordinates"):
        if cand in names:
            pin = cand
            break
    if pin is not None:
        conn(uv, "", s, pin)
    return s


def triplanar(mat, tex, scale, x0, y0, channel=""):
    """World-space triplanar sample of `tex`. Returns (expression, output_pin).
    channel="" -> RGB, channel="R" -> single float (grayscale maps)."""
    wp = expr(mat, unreal.MaterialExpressionWorldPosition, x0, y0)
    nrm = expr(mat, unreal.MaterialExpressionVertexNormalWS, x0, y0 + 40)
    absn = expr(mat, unreal.MaterialExpressionAbs, x0 + 170, y0 + 40)
    conn(nrm, "", absn)
    one = vector(mat, 1.0, 1.0, 1.0, x0 + 170, y0 + 110)
    dotn = expr(mat, unreal.MaterialExpressionDotProduct, x0 + 340, y0 + 40)
    conn(absn, "", dotn, "A")
    conn(one, "", dotn, "B")
    w = div(mat, absn, "", dotn, "", x0 + 500, y0 + 40)

    sl = scalar(mat, scale, x0 + 170, y0 + 200)
    parts = []
    # (which world components to KEEP as the UV pair) -> (weight channel)
    #   keep (Y,Z) -> the projection seen from X, weighted by |nx| -> w.x
    #   keep (X,Z) -> seen from Y,                          weighted by |ny| -> w.y
    #   keep (X,Y) -> seen from Z,                          weighted by |nz| -> w.z
    axes = [((False, True, True), "R"),
            ((True, False, True), "G"),
            ((True, True, False), "B")]
    yy = y0
    for (kr, kg, kb), wch in axes:
        uvm = mask(mat, wp, kr, kg, kb, x0 + 170, yy + 280)
        uv = mul(mat, uvm, "", sl, "", x0 + 340, yy + 280)
        s = sample_with_uv(mat, tex, uv, x0 + 500, yy + 280)
        wm = mask(mat, w, wch == "R", wch == "G", wch == "B", x0 + 340, yy + 400)
        if channel:
            p = mul(mat, s, channel, wm, "", x0 + 660, yy + 280)
        else:
            p = mul(mat, s, "RGB", wm, "", x0 + 660, yy + 280)
        parts.append((p, ""))
        yy += 260

    s01 = add(mat, parts[0][0], "", parts[1][0], "", x0 + 820, y0 + 500)
    s012 = add(mat, s01, "", parts[2][0], "", x0 + 980, y0 + 560)
    return s012, ""


def build_textured_material(name, base_tex, rough_tex, scale, tint=(1.0, 1.0, 1.0),
                            rough_lo=0.40, rough_hi=0.75, metallic=0.0,
                            emissive=None, scene_tint_param=True):
    """Create/replace a triplanar material from a base-colour and a roughness
    map. `tint` multiplies the albedo (per-scene wall colour)."""
    path = MAT_DIR + "/" + name
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    at = unreal.AssetToolsHelpers.get_asset_tools()
    mat = at.create_asset(name, MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
    if mat is None:
        raise RuntimeError("create_asset failed for " + name)

    btex = unreal.EditorAssetLibrary.load_asset(base_tex)
    rtex = unreal.EditorAssetLibrary.load_asset(rough_tex)
    if btex is None or rtex is None:
        raise RuntimeError("texture missing: %s / %s" % (base_tex, rough_tex))

    col, _ = triplanar(mat, btex, scale, -1600, -300, channel="")
    if scene_tint_param:
        tint_e = vec_param(mat, "SceneTint", tint[0], tint[1], tint[2], -1600, -900)
    else:
        tint_e = vector(mat, tint[0], tint[1], tint[2], -1600, -900)
    albedo = mul(mat, col, "", tint_e, "", -1100, -400)
    set_prop(mat, albedo, "", unreal.MaterialProperty.MP_BASE_COLOR)

    r, _ = triplanar(mat, rtex, scale, -1600, 1200, channel="R")
    rough = lerp(mat, clamp01(mat, r, -600, 1200), scalar(mat, rough_lo, -700, 1400),
                 scalar(mat, rough_hi, -700, 1500), -900, 1300)
    set_prop(mat, rough, "", unreal.MaterialProperty.MP_ROUGHNESS)

    if metallic:
        set_prop(mat, scalar(mat, metallic, -1400, 1900), "", unreal.MaterialProperty.MP_METALLIC)

    if emissive is not None:
        em = vector(mat, emissive[0], emissive[1], emissive[2], -1400, 2100)
        set_prop(mat, em, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    MEL.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat)
    log("material built: %s (scale 1/%.0f cm, rough %.2f-%.2f)" % (name, 1.0 / scale, rough_lo, rough_hi))
    return mat
