# M2bs/M3a: turn the preview frames rendered by Tools/m3a_previews.py into a
# short showcase clip. Must run with a Python that has OpenCV (the project venv
# Tools/hy3d_env). One rendered frame == one video frame: this clip is an editor
# camera path over the CURRENT map, NOT gameplay and NOT time-compressed.
#   python Tools/m3a_encode_clip.py <frames_dir> <out_mp4> [fps] [prefix]
import os
import sys

import cv2

frames_dir = sys.argv[1]
out_mp4 = sys.argv[2]
fps = float(sys.argv[3]) if len(sys.argv) > 3 else 20.0
prefix = sys.argv[4] if len(sys.argv) > 4 else ""

names = sorted(f for f in os.listdir(frames_dir)
               if f.lower().endswith(".png") and (not prefix or f.startswith(prefix)))
if not names:
    print("no frames in", frames_dir)
    sys.exit(2)

first = cv2.imread(os.path.join(frames_dir, names[0]))
h, w = first.shape[:2]
writer = cv2.VideoWriter(out_mp4, cv2.VideoWriter_fourcc(*"mp4v"), fps, (w, h))
for n in names:
    img = cv2.imread(os.path.join(frames_dir, n))
    if img is None:
        continue
    if img.shape[1] != w or img.shape[0] != h:
        img = cv2.resize(img, (w, h))
    writer.write(img)
writer.release()
print("clip: %s (%d frames, %.1f s @ %.0f fps, %dx%d)"
      % (out_mp4, len(names), len(names) / fps, fps, w, h))
