# M1c Scene Planner: turn a natural-language room description into a
# SceneSpec 0.1 JSON using an LLM, validated by the REAL C++ validator.
#
# Pipeline per round:
#   1. call the LLM (OpenAI-compatible /chat/completions) with the schema +
#      generator constraints in the system prompt
#   2. extract JSON, save the raw output
#   3. validate with FSceneSpecValidator via the standalone editor
#      (-M1cCheckSpec: schema + plan realization + plan-coordinate placement);
#   4. repeat at most 1 + max_retries rounds; never substitute a template
#
# Configuration:
#   - model / base_url: Tools/scene_planner.json, overridden by env
#     WORLDGEN_LLM_MODEL / WORLDGEN_LLM_BASE_URL
#   - API key: ONLY from env WORLDGEN_LLM_API_KEY (fallback ZHIPUAI_API_KEY);
#     it is never written to disk, logs or reports by this script
#
# Exit codes: 0 ok | 2 usage | 3 no API key | 4 LLM call failed | 5 spec still
# invalid after all rounds | 6 unexpected validator harness failure.
import argparse
import json
import os
import re
import subprocess
import sys
import time
import urllib.error
import urllib.request

PROJECT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONFIG_PATH = os.path.join(PROJECT_DIR, "Tools", "scene_planner.json")
UE_EDITOR_CMD = os.environ.get(
    "WORLDGEN_UE_EDITOR",
    "UnrealEditor-Cmd.exe")  # on PATH, or set WORLDGEN_UE_EDITOR
UPROJECT = os.path.join(PROJECT_DIR, "WorldGen.uproject")

SYSTEM_PROMPT = """You are a level-design data generator for an Unreal Engine pipeline.
You convert a natural-language room description into ONE JSON document that
conforms to the SceneSpec 0.1 schema below. Output ONLY the JSON document -
no markdown fences, no commentary.

SceneSpec 0.1 schema (every field required unless marked optional):
{
  "schema_version": "0.1",
  "scene_id": "<ascii identifier [A-Za-z_][A-Za-z0-9_]{0,63}>",
  "seed": 42,
  "room": { "width_cm": <X span>, "length_cm": <Y span>, "height_cm": <Z span> },
  "entrance": { "wall": "north|south|east|west", "offset_cm": <number>, "width_cm": 120 },
  "exit":     { "wall": "north|south|east|west", "offset_cm": <number>, "width_cm": 120, "locked": true },
  "agent": { "radius_cm": 35, "height_cm": 180 },
  "objects": [
    { "id": "<ascii id>", "semantic_type": "desk|shelf|crate|pickup|...",
      "asset_id": "placeholder",
      "target_size_cm": [sx, sy, sz],
      "placement": { "position_cm": [x, y] , "rotation_deg": 0 },
      "collision_policy": "simple", "nav_obstacle": true, "priority": "required" },
    { "id": "<ascii id>", "semantic_type": "pickup", "asset_id": "key_placeholder",
      "target_size_cm": [30, 30, 30],
      "placement": { "on_top_of": "<support object id>" },
      "priority": "required" }
  ],
  "objective": { "type": "collect_then_exit", "item_id": "<the pickup id>" }
}

HARD CONSTRAINTS (violations are rejected by the downstream validator):
1. room.width_cm and room.length_cm in [400, 1200]; room.height_cm in [250, 400].
2. entrance.width_cm and exit.width_cm must be EXACTLY 120 (fixed placeholder door panel).
3. Coordinate convention: the room center is the origin; the NORTH wall is the
   +X edge, south is -X, east is +Y, west is -Y. The RUN LENGTH of the
   north/south walls equals room.length_cm; the run of east/west walls equals
   room.width_cm.
4. offset_cm is measured along the wall run from its NEGATIVE end:
   offset_cm must be in [width_cm/2 + 40, run_length - width_cm/2 - 40].
5. exit.wall and entrance.wall must be DIFFERENT walls (recommended: exit on
   "north", entrance on "south").
6. exit.locked must be true and objective.type must be "collect_then_exit".
7. agent.radius_cm in [30, 40], agent.height_cm in [170, 190].
8. objects: 2 to 5 items. EXACTLY ONE object has semantic_type "pickup"
   (the quest key, size about [30,30,30]) and it MUST use
   placement.on_top_of referencing a support object that has an explicit
   placement.position_cm. Every non-pickup object MUST have an explicit
   placement.position_cm [x, y] in room coordinates (center origin).
9. Every object footprint must stay INSIDE the room: |position| + half-size
   (accounting for rotation_deg) <= half the room span on that axis.
10. Keep the walk path clear: place large objects at least 150 cm away from
    every wall, NOT in front of the doorway of either door, and not on the
    straight line between the entrance and the support object.
11. target_size_cm values in [30, 300] per axis for furniture; rotation_deg in [0, 360).
12. All numbers are plain JSON numbers (no units, no strings).
13. Registered asset catalogue (asset_id whitelist - the ONLY allowed values):
    - "m2a_desk_001": the ONLY real generated asset - a wooden work desk
      (140 x 70 x 75 cm, bottom-center pivot). Use it ONLY for an object the
      description describes as a wooden table/desk whose target_size_cm is
      compatible (roughly 140 x 70 x 75).
    - "placeholder": every other furniture or prop (built as a placeholder box).
    - "key_placeholder": the pickup/key object.
    NEVER invent other asset ids (e.g. "generated_shelf_b", "my_chair_01"):
    no other generated assets exist, and a fabricated id would be rejected
    or silently fall back to a placeholder box."""

JSON_CODE_FENCE_RE = re.compile(r"```(?:json)?\s*(.*?)```", re.S)


def log(msg):
    print("[scene_planner] " + msg, flush=True)


def load_config():
    with open(CONFIG_PATH, "r", encoding="utf-8") as f:
        cfg = json.load(f)
    model = os.environ.get("WORLDGEN_LLM_MODEL", cfg.get("model", "glm-5.3-flash"))
    base_url = os.environ.get("WORLDGEN_LLM_BASE_URL", cfg.get("base_url", "")).rstrip("/")
    retries = int(os.environ.get("WORLDGEN_LLM_MAX_RETRIES", cfg.get("max_retries", 2)))
    timeout = int(cfg.get("timeout_seconds", 120))
    return model, base_url, retries, timeout


def resolve_api_key():
    for var in ("WORLDGEN_LLM_API_KEY", "ZHIPUAI_API_KEY"):
        val = os.environ.get(var)
        if val:
            return var, val
    return None, None


def call_llm(base_url, model, api_key, messages, timeout):
    """OpenAI-compatible chat completion. Returns (content, error)."""
    url = base_url + "/chat/completions"
    payload = json.dumps({
        "model": model,
        "messages": messages,
        "temperature": 0.2,
    }).encode("utf-8")
    req = urllib.request.Request(
        url, data=payload, method="POST",
        headers={"Content-Type": "application/json",
                 "Authorization": "Bearer " + api_key})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            body = json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        detail = e.read().decode("utf-8", "replace")[:500]
        # never include the key; the request headers are not in detail
        return None, "HTTP %d: %s" % (e.code, detail)
    except Exception as e:  # noqa: BLE001
        return None, "%s: %s" % (type(e).__name__, e)
    try:
        content = body["choices"][0]["message"]["content"]
    except (KeyError, IndexError, TypeError):
        return None, "unexpected response shape: %s" % json.dumps(body)[:300]
    if not content:
        return None, "empty completion"
    return content, None


def extract_json(text):
    m = JSON_CODE_FENCE_RE.search(text)
    candidate = m.group(1) if m else text
    start, end = candidate.find("{"), candidate.rfind("}")
    if start == -1 or end == -1 or end <= start:
        raise ValueError("no JSON object found in model output")
    return json.loads(candidate[start:end + 1])


def run_validator(spec_path, err_out_path, log_path):
    """M2b multi-scene: ONE C++ launch runs the full gate - schema validation,
    plan realization (FLevelPlanBuilder) and placement checks against the
    SERIALIZED plan coordinates (-M1cCheckSpec). The plan is the single source
    of truth for geometry, so this can never drift from the C++ coordinate
    math (the former Python mirror in this file is gone).
    Returns (valid, error_lines, exit_code)."""
    cmd = [UE_EDITOR_CMD, UPROJECT, "-game",
           "-M1cCheckSpec=" + spec_path,
           "-M1cErrorOut=" + err_out_path,
           "-unattended", "-nopause", "-nosplash",
           "-abslog=" + log_path]
    proc = subprocess.run(cmd, capture_output=True, text=True,
                          encoding="utf-8", errors="replace", timeout=300)
    try:
        with open(err_out_path, "r", encoding="utf-8") as f:
            report = f.read()
    except OSError:
        return False, ["validator did not produce a report (exit %d)" % proc.returncode], proc.returncode
    lines = [ln for ln in report.splitlines() if ln.strip()]
    if lines and lines[0].strip() == "VALID":
        return True, [], proc.returncode
    return False, lines[1:] or ["INVALID (no details)"], proc.returncode


def main():
    ap = argparse.ArgumentParser(description="LLM room description -> SceneSpec 0.1")
    ap.add_argument("--description", help="natural-language room description")
    ap.add_argument("--description-file", help="read the description from a UTF-8 file")
    ap.add_argument("--out-dir", default=os.path.join(PROJECT_DIR, "Data", "Reports", "m1c"))
    ap.add_argument("--scene-tag", default="ai", help="short tag used in the output dir name")
    args = ap.parse_args()

    if args.description_file:
        with open(args.description_file, "r", encoding="utf-8") as f:
            description = f.read().strip()
    elif args.description:
        description = args.description.strip()
    else:
        ap.error("either --description or --description-file is required")
    if not description:
        print("empty description", file=sys.stderr)
        return 2

    model, base_url, max_retries, timeout = load_config()
    key_var, api_key = resolve_api_key()
    if not api_key:
        log("ERROR: no API key found. Set WORLDGEN_LLM_API_KEY (or ZHIPUAI_API_KEY) "
            "in the environment. The key is intentionally NOT read from any file.")
        return 3
    if not base_url:
        log("ERROR: no base_url configured (Tools/scene_planner.json or WORLDGEN_LLM_BASE_URL).")
        return 2

    out_dir = os.path.join(args.out_dir, "%s_%s" % (args.scene_tag, time.strftime("%Y%m%d_%H%M%S")))
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, "user_description.txt"), "w", encoding="utf-8") as f:
        f.write(description + "\n")
    log("artifacts dir: " + out_dir)
    log("model=%s base_url=%s (key from env %s, value never logged)" % (model, base_url, key_var))

    messages = [
        {"role": "system", "content": SYSTEM_PROMPT},
        {"role": "user", "content": "Room description:\n" + description},
    ]

    attempts = []
    final_spec_path = None
    for round_idx in range(1, max_retries + 2):  # initial + retries
        log("=== round %d/%d: calling model ===" % (round_idx, max_retries + 1))
        content, err = call_llm(base_url, model, api_key, messages, timeout)
        if err:
            log("LLM call FAILED: " + err)
            with open(os.path.join(out_dir, "round%d_llm_error.txt" % round_idx), "w", encoding="utf-8") as f:
                f.write(err + "\n")
            return 4
        with open(os.path.join(out_dir, "round%d_raw.txt" % round_idx), "w", encoding="utf-8") as f:
            f.write(content)
        log("model raw output saved (%d chars)" % len(content))

        try:
            spec = extract_json(content)
        except (ValueError, json.JSONDecodeError) as e:
            log("JSON extraction failed: %s" % e)
            messages.append({"role": "assistant", "content": content})
            messages.append({"role": "user", "content":
                "Your previous answer could not be parsed as JSON (%s). "
                "Reply again with ONLY the raw JSON document." % e})
            attempts.append({"round": round_idx, "stage": "parse", "ok": False, "error": str(e)})
            continue

        candidate_path = os.path.abspath(os.path.join(out_dir, "round%d_candidate_spec.json" % round_idx))
        with open(candidate_path, "w", encoding="utf-8") as f:
            json.dump(spec, f, ensure_ascii=False, indent=2)

        valid, errors, vexit = run_validator(
            candidate_path,
            os.path.abspath(os.path.join(out_dir, "round%d_validator.txt" % round_idx)),
            os.path.abspath(os.path.join(out_dir, "round%d_validator.log" % round_idx)))
        with open(os.path.join(out_dir, "round%d_validator.txt" % round_idx), "a", encoding="utf-8") as f:
            f.write("validator_exit=%d\n" % vexit)
        attempts.append({"round": round_idx, "stage": "validate", "ok": valid, "errors": errors})

        if valid:
            # The C++ gate already covers schema + plan realization + placement
            # (spawn-blocking footprints) against the serialized plan.
            final_spec_path = os.path.join(out_dir, "scene_spec.json")
            with open(final_spec_path, "w", encoding="utf-8") as f:
                json.dump(spec, f, ensure_ascii=False, indent=2)
            log("=== spec VALID on round %d (schema + plan placement) ===" % round_idx)
            break

        log("spec INVALID (%d error(s)); feeding back to the model" % len(errors))
        for e in errors:
            log("  - " + e)
        messages.append({"role": "assistant", "content": content})
        messages.append({"role": "user", "content":
            "Your SceneSpec was rejected with these errors:\n" +
            "\n".join("- " + e for e in errors) +
            "\nFix ALL of them and reply again with ONLY the corrected JSON document."})

    summary = {
        "timestamp": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "model": model,
        "base_url": base_url,
        "api_key_source": key_var,
        "description": description,
        "attempts": attempts,
        "rounds_used": len(attempts),
        "max_retries_allowed": max_retries,
        "success": final_spec_path is not None,
        "final_spec": os.path.basename(final_spec_path) if final_spec_path else None,
        "note": "api key value is intentionally absent from this report",
    }
    with open(os.path.join(out_dir, "summary.json"), "w", encoding="utf-8") as f:
        json.dump(summary, f, ensure_ascii=False, indent=2)

    if final_spec_path is None:
        log("FAILED: spec still invalid after %d round(s); no template fallback was used" % len(attempts))
        return 5
    log("SUCCESS: final spec at " + final_spec_path)
    print(final_spec_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
