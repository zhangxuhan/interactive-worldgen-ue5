# M2a: normalize the raw Hunyuan3D mesh for UE import.
# - up-axis auto-detect (desk: top-heavy axis = +Z up), verified via preview PNG
# - non-uniform scale to the exact spec footprint (desk_01: 140x70x75 cm)
# - pivot at bottom center (z=0), long horizontal axis -> local +X
# - exports preview PNG + normalized OBJ (cm units baked into geometry)
import sys, os, json, hashlib, argparse
import numpy as np
import trimesh

DESK_TARGET = [140.0, 70.0, 75.0]  # from plan op: box 140x70x75, yaw 90


def hash_file(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def preview_png(mesh, path):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    v = np.asarray(mesh.vertices)
    f = np.asarray(mesh.faces)
    fig = plt.figure(figsize=(15, 5))
    views = [("XY (top)", 0, 1), ("XZ (side)", 0, 2), ("YZ (front)", 1, 2)]
    for i, (title, a, b) in enumerate(views):
        ax = fig.add_subplot(1, 3, i + 1)
        ax.triplot(v[:, a], v[:, b], f, linewidth=0.05, color="steelblue")
        ax.set_title(title)
        ax.set_aspect("equal")
    fig.tight_layout()
    fig.savefig(path, dpi=110)
    plt.close(fig)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--raw", required=True)
    ap.add_argument("--out-dir", required=True)
    ap.add_argument("--target", nargs=3, type=float, default=DESK_TARGET)
    args = ap.parse_args()
    os.makedirs(args.out_dir, exist_ok=True)

    mesh = trimesh.load(args.raw, force="mesh", process=True)
    print(f"[m2a] raw: verts={len(mesh.vertices)} faces={len(mesh.faces)} "
          f"bounds={mesh.bounds.tolist()}", flush=True)

    # --- up-axis detection: the desk's top face holds most of the triangle
    # area near the max end of the vertical axis; legs are thin at the bottom.
    best = None
    for axis in range(3):
        v = np.asarray(mesh.vertices)[:, axis]
        lo, hi = v.min(), v.max()
        span = max(hi - lo, 1e-6)
        top_frac = ((v - lo) / span > 0.7).mean()
        bottom_frac = ((v - lo) / span < 0.3).mean()
        score = top_frac - bottom_frac
        if best is None or score > best[1]:
            best = (axis, score)
    up = best[0]
    print(f"[m2a] up-axis detected: {['X','Y','Z'][up]} (score={best[1]:.3f})", flush=True)

    # Rotation must map the detected up axis onto +Z (UE up).
    # NOTE the sign: trimesh rotation_matrix(angle, axis) is right-handed, so
    #   up=+Y -> +pi/2 about X maps (0,1,0) -> (0,0,1)
    #   up=+X -> -pi/2 about Y maps (1,0,0) -> (0,0,1)
    # An earlier revision had both signs inverted; it silently imported the
    # desk upside down (tabletop at z=0, frame legs pointing up).
    if up == 0:
        mesh.apply_transform(trimesh.transformations.rotation_matrix(-np.pi / 2, [0, 1, 0]))
    elif up == 1:
        mesh.apply_transform(trimesh.transformations.rotation_matrix(np.pi / 2, [1, 0, 0]))

    # --- verify the top-heavy end really ended up at +Z; flip 180 deg if not
    def top_heavy_score(vals):
        lo_, hi_ = vals.min(), vals.max()
        sp_ = max(hi_ - lo_, 1e-6)
        return ((vals - lo_) / sp_ > 0.7).mean() - ((vals - lo_) / sp_ < 0.3).mean()

    vz = np.asarray(mesh.vertices)[:, 2]
    zscore = top_heavy_score(vz)
    flipped = False
    if zscore < 0:
        mesh.apply_transform(trimesh.transformations.rotation_matrix(np.pi, [1, 0, 0]))
        flipped = True
        zscore = top_heavy_score(np.asarray(mesh.vertices)[:, 2])
    print(f"[m2a] up check: top_heavy_score_z={zscore:.3f} flipped={flipped}", flush=True)

    # --- align long horizontal axis to local +X (rotate 90 deg about Z if needed)
    ext = mesh.extents  # x,y,z after up-correction
    if ext[0] < ext[1]:
        mesh.apply_transform(trimesh.transformations.rotation_matrix(np.pi / 2, [0, 0, 1]))
        ext = mesh.extents
        print("[m2a] rotated 90 deg about Z (long axis -> +X)", flush=True)

    # --- scale to exact target footprint, pivot bottom center
    lo, hi = mesh.bounds
    center_xy = (lo[:2] + hi[:2]) / 2.0
    mesh.apply_translation([-center_xy[0], -center_xy[1], -lo[2]])
    scale = np.array(args.target) / ext[[0, 1, 2]]
    mesh.apply_scale(scale)
    print(f"[m2a] scaled by {scale.tolist()} -> extents {mesh.extents.tolist()}", flush=True)

    png_path = os.path.join(args.out_dir, "normalized_preview.png")
    preview_png(mesh, png_path)
    obj_path = os.path.join(args.out_dir, "normalized_mesh.obj")
    mesh.export(obj_path)

    report = {
        "raw_mesh": os.path.abspath(args.raw),
        "raw_sha256": hash_file(args.raw),
        "raw_verts": int(len(mesh.vertices)),
        "raw_faces": int(len(mesh.faces)),
        "up_axis_detected": int(up),
        "up_check_z_score": round(float(zscore), 4),
        "up_flipped_180": bool(flipped),
        "scale_xyz": [round(float(s), 5) for s in scale],
        "final_extents_cm": [round(float(e), 2) for e in mesh.extents],
        "normalized_obj": os.path.abspath(obj_path),
        "normalized_sha256": hash_file(obj_path),
        "normalized_faces": int(len(mesh.faces)),
    }
    with open(os.path.join(args.out_dir, "normalize_report.json"), "w") as f:
        json.dump(report, f, indent=2)
    print("[m2a] DONE", json.dumps(report, indent=2), flush=True)


if __name__ == "__main__":
    main()
