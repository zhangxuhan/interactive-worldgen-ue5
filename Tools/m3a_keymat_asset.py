# M3a fix: bake M_Key into the SM_Key_Prop static mesh asset's material slot.
# Component-level overrides proved unreliable for the WorldGenPickup.KeyMesh
# render path; the asset-level slot survives everything and needs no rebuild.
import unreal

MESH = "/Game/Generated/Assets/SM_Key_Prop"
MAT = "/Game/Generated/Materials/M_Key"

mesh = unreal.EditorAssetLibrary.load_asset(MESH)
mat = unreal.EditorAssetLibrary.load_asset(MAT)
if mesh is None or mat is None:
    raise RuntimeError("asset missing: mesh=%s mat=%s" % (mesh, mat))
mesh.set_material(0, mat)
ok = unreal.EditorAssetLibrary.save_loaded_asset(mesh)
cur = mesh.get_material(0)
unreal.log("[M3aKey] mesh slot0 now=%s saved=%s" % (cur.get_path_name() if cur else "NONE", ok))
unreal.SystemLibrary.quit_editor()
