# Write Tools/m3a_preview_config.json = locked baseline + this scene's identity.
# Used by Tools/m3a_chain.sh AND by the local web demo (Tools/m3a_web.py) so both
# routes render with exactly the same camera angles and exposure.
#   python m3a_write_preview_config.py <scene_id> <spec_path> <out_dir> <prefix>
import json
import os
import sys

TOOLS = os.path.dirname(os.path.abspath(__file__))
BASELINE = os.path.join(TOOLS, "m3a_preview_baseline.json")
TARGET = os.path.join(TOOLS, "m3a_preview_config.json")


def build(scene_id, spec_path, out_dir, prefix):
    with open(BASELINE, "r", encoding="utf-8") as f:
        cfg = json.load(f)
    for k in list(cfg):
        if k.startswith("_"):
            del cfg[k]
    cfg.update({
        "level": "/Game/Generated/%s" % scene_id,
        "scene_id": scene_id,
        "spec": spec_path,
        "out_dir": out_dir,
        "prefix": prefix,
    })
    return cfg


def main():
    scene_id, spec_path, out_dir, prefix = sys.argv[1:5]
    cfg = build(scene_id, spec_path, out_dir, prefix)
    os.makedirs(out_dir, exist_ok=True)
    with open(TARGET, "w", encoding="utf-8") as f:
        json.dump(cfg, f, ensure_ascii=False, indent=1)
    print("preview config -> %s" % TARGET)
    print(json.dumps({k: cfg[k] for k in ("level", "scene_id", "out_dir", "prefix", "exposure")},
                     ensure_ascii=False))


if __name__ == "__main__":
    main()
