# M3a texture generator (ORIGINAL, project-owned assets).
#
# All maps in Data/Assets/m3a/tex are generated procedurally by THIS script
# with numpy + Pillow: no third-party image is copied in, so there is no
# third-party licence attached to them. They are seamless (tileable) because
# every noise term uses integer frequencies over the unit square.
#
# Run with the venv that has numpy + Pillow:
#   Tools\hy3d_env\Scripts\python.exe Tools\m3a_make_textures.py
import json
import os
import numpy as np
from PIL import Image

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   "Data", "Assets", "m3a", "tex")
N = 512
os.makedirs(OUT, exist_ok=True)

rng = np.random.default_rng(20260927)
yy, xx = np.mgrid[0:N, 0:N].astype(np.float64) / N  # u = x/N (along X), v = y/N


def value_noise(res_x, res_y, seed):
    """Tileable value noise: random grid (res_x x res_y) smoothly interpolated
    with WRAP-AROUND, so the result tiles exactly. Isotropic - no diagonal bias."""
    r = np.random.default_rng(seed)
    g = r.random((res_y, res_x))
    x = xx * res_x
    y = yy * res_y
    x0 = np.floor(x).astype(np.int64)
    y0 = np.floor(y).astype(np.int64)
    fx = x - x0
    fy = y - y0
    sx = fx * fx * (3.0 - 2.0 * fx)
    sy = fy * fy * (3.0 - 2.0 * fy)
    x0m = x0 % res_x
    x1m = (x0 + 1) % res_x
    y0m = y0 % res_y
    y1m = (y0 + 1) % res_y
    v00 = g[y0m, x0m]
    v10 = g[y0m, x1m]
    v01 = g[y1m, x0m]
    v11 = g[y1m, x1m]
    return (v00 * (1 - sx) + v10 * sx) * (1 - sy) + (v01 * (1 - sx) + v11 * sx) * sy


def fbm(res, octaves=5, seed=0, gain=0.5, aspect=1.0):
    """Fractal sum of tileable value noise. `res` = base grid resolution along Y;
    `aspect` < 1 stretches the noise along X (wood grain direction)."""
    out = np.zeros_like(xx)
    amp = 1.0
    tot = 0.0
    for o in range(octaves):
        ry = max(2, int(round(res * (2 ** o))))
        rx = max(2, int(round(ry * aspect)))
        out += amp * value_noise(rx, ry, seed + o * 977 + 13)
        tot += amp
        amp *= gain
    return out / max(tot, 1e-6)


def save(arr, name):
    """arr: float HxW(x3) in 0..1 -> 8-bit PNG."""
    a = np.clip(arr, 0.0, 1.0)
    if a.ndim == 2:
        img = Image.fromarray((a * 255.0 + 0.5).astype(np.uint8), mode="L")
    else:
        img = Image.fromarray((a * 255.0 + 0.5).astype(np.uint8), mode="RGB")
    p = os.path.join(OUT, name + ".png")
    img.save(p)
    print("wrote", p, img.size, img.mode)


def normal_from_height(h, strength):
    dx = (np.roll(h, -1, axis=1) - np.roll(h, 1, axis=1)) * 0.5
    dy = (np.roll(h, -1, axis=0) - np.roll(h, 1, axis=0)) * 0.5
    nz = np.ones_like(h)
    n = np.stack([-dx * strength, -dy * strength, nz], axis=-1)
    ln = np.sqrt((n ** 2).sum(-1))[..., None]
    n = n / np.maximum(ln, 1e-6)
    return n * 0.5 + 0.5


# ---------------------------------------------------------------- wood (desk)
def wood(base_rgb, planks, seed, grain_freq=46, seam_w=0.020, plank_contrast=0.14):
    """Planks run along X (u); seams are the constant-v lines between them."""
    v = yy
    u = xx
    pv = v * planks
    idx = np.floor(pv).astype(np.int64) % planks          # plank index (tileable)
    frac = pv - np.floor(pv)                              # 0..1 inside the plank
    per_plank = rng.random(planks) if False else np.random.default_rng(seed).random(planks)
    tone = per_plank[idx] * 2.0 - 1.0                     # -1..1 per plank

    # wavy grain: high frequency across the plank, warped by low-freq noise
    warp = 0.9 * (fbm(6, octaves=3, seed=seed + 11, aspect=0.35) - 0.5)
    grain = np.sin(2 * np.pi * (grain_freq * v + warp))
    grain = 0.5 + 0.5 * grain
    fine = fbm(96, octaves=3, seed=seed + 23, aspect=0.06)
    pores = fbm(160, octaves=3, seed=seed + 37, aspect=0.05)

    col = np.array(base_rgb, dtype=np.float64)[None, None, :]
    out = np.broadcast_to(col, (N, N, 3)).copy()
    out *= (1.0 + plank_contrast * tone)[..., None]
    out *= (1.0 + 0.22 * (grain - 0.5))[..., None]
    out *= (1.0 + 0.14 * (fine - 0.5))[..., None]
    out *= (1.0 - 0.14 * np.clip(pores - 0.45, 0, 1))[..., None]

    # darker seams between planks (+ a soft shading gradient inside each plank)
    d = np.minimum(frac, 1.0 - frac)
    seam = np.clip(d / seam_w, 0.0, 1.0)                  # 0 at seam, 1 inside
    out *= (0.30 + 0.70 * seam ** 0.5)[..., None]
    out *= (1.0 - 0.10 * (1.0 - seam))[..., None]
    height = seam ** 0.5 * 0.8 + 0.2 * grain
    rough = np.clip(0.52 + 0.20 * (grain - 0.5) + 0.12 * (fine - 0.5)
                    - 0.10 * (1 - seam), 0.25, 0.85)
    return np.clip(out, 0, 1), np.clip(rough, 0, 1), np.clip(height, 0, 1)


# M3a fix (desk reads too plain in the wide shot): warmer base, stronger plank
# tone steps and denser grain so the desk separates from floor/crates.
wood_col, wood_rough, wood_h = wood((0.520, 0.310, 0.140), planks=7, seed=7, seam_w=0.030,
                                    grain_freq=58, plank_contrast=0.26)
save(wood_col, "T_Wood_D")
save(wood_rough, "T_Wood_R")
save(normal_from_height(wood_h, 2.2), "T_Wood_N")

crate_col, crate_rough, crate_h = wood((0.640, 0.470, 0.280), planks=4, seed=21,
                                       grain_freq=34, seam_w=0.045, plank_contrast=0.10)
save(crate_col, "T_Crate_D")
save(crate_rough, "T_Crate_R")
save(normal_from_height(crate_h, 1.8), "T_Crate_N")

# ------------------------------------------------------------------- plaster
plaster = 0.80 + 0.20 * (fbm(5, octaves=4, seed=101) - 0.5) + 0.08 * (fbm(16, octaves=3, seed=102) - 0.5)
plaster = np.clip(plaster, 0, 1)
save(np.broadcast_to(plaster[..., None], (N, N, 3)), "T_Wall_D")
save(np.clip(0.78 + 0.16 * (fbm(7, octaves=3, seed=103) - 0.5), 0, 1), "T_Wall_R")
save(normal_from_height(plaster, 1.1), "T_Wall_N")

# ------------------------------------------------------------------ concrete
spec = fbm(28, octaves=4, seed=201)
agg = fbm(90, octaves=2, seed=202)
agg = np.clip((agg - 0.55) / 0.45, 0, 1) ** 2
conc = 0.46 + 0.10 * (spec - 0.5) + 0.20 * agg
# tileable grid seam: one cell per texture tile -> 100 cm grid at scale 1/100
gu = np.minimum(xx, 1.0 - xx)
gv = np.minimum(yy, 1.0 - yy)
seam = np.clip(np.minimum(gu, gv) / 0.012, 0, 1)
conc *= (0.62 + 0.38 * seam)
conc = np.clip(conc, 0, 1)
save(np.broadcast_to(conc[..., None], (N, N, 3)) * np.array([0.96, 0.97, 1.00]), "T_Floor_D")
save(np.clip(0.62 + 0.16 * (spec - 0.5) + 0.10 * (1 - agg), 0, 1), "T_Floor_R")
save(normal_from_height(conc, 1.4), "T_Floor_N")

# --------------------------------------------- apron (ground outside the exit)
grav = np.clip(0.30 + 0.22 * (fbm(48, octaves=4, seed=301) - 0.5)
               + 0.16 * (fbm(14, octaves=3, seed=302) - 0.5), 0, 1)
save(np.broadcast_to(grav[..., None], (N, N, 3)) * np.array([1.00, 0.99, 0.95]), "T_Apron_D")
save(np.clip(0.80 + 0.12 * (fbm(30, octaves=3, seed=303) - 0.5), 0, 1), "T_Apron_R")
save(normal_from_height(grav, 2.0), "T_Apron_N")

prov = {
    "origin": "generated in-project by Tools/m3a_make_textures.py (numpy + Pillow, procedural)",
    "license": "project-owned original asset; no third-party image copied",
    "size": [N, N],
    "tiling": "seamless (integer-frequency sinusoid noise over the unit square)",
    "generated_at": "2026-09-27",
    "maps": {
        "T_Wood_D/R/N": "desk + crate: plank wood, albedo / roughness / normal",
        "T_Crate_D/R/N": "paler pine for placeholder crates",
        "T_Wall_D/R/N": "plaster, neutral so a per-scene tint can colour it",
        "T_Floor_D/R/N": "indoor concrete screed with one 100 cm grid cell per tile",
        "T_Apron_D/R/N": "outdoor gravel ground beyond the exit door",
    },
}
with open(os.path.join(OUT, "provenance.json"), "w", encoding="utf-8") as f:
    json.dump(prov, f, ensure_ascii=False, indent=1)
print("wrote", os.path.join(OUT, "provenance.json"))
