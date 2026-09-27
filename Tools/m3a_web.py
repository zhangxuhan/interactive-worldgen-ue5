#!/usr/bin/env python3
# M3a local web demo server for the UE5 generative-scene pipeline.
#
#   python Tools/m3a_web.py [--port 8765] [--no-browser]
#
# Design notes (why it looks like this):
#  * Binds 127.0.0.1 ONLY - never exposed to the LAN or the internet.
#  * It does NOT re-implement any SceneSpec parsing or geometry rule. Every
#    stage shells out to the EXISTING project entry points:
#       Tools/scene_planner.py            (DeepSeek -> SceneSpec, C++ gate loop)
#       UnrealEditor-Cmd.exe -M1cCheckSpec (schema + placement + plan corridor)
#       UnrealEditor-Cmd.exe -M1bGenerate  (plan JSON)
#       UnrealEditor.exe   -ExecutePythonScript Tools/m1b_build_level.py
#       Tools/m2bs_polish_level.py         (lighting/materials, visual only)
#       Tools/m2b_check_umap.py            (asset verification)
#       Tools/m3a_previews.py              (preview stills + showcase clip)
#       UnrealEditor-Cmd.exe -M1bE2E       (real playthrough acceptance)
#  * User text is NEVER interpolated into a shell command: the description is
#    written to a file and passed as an argument; every subprocess uses an
#    argument list, and the UE executables/scripts are fixed constants.
#  * The API key stays in this process' environment and is inherited by the
#    planner subprocess only. It is never sent to the browser, written to a
#    task artifact or included in any log (the planner never logs it either).
#  * The UE editor cannot build two levels at once, so generation is a single
#    worker thread with a queue; the web UI shows "running/queued".
import argparse
import json
import os
import queue
import re
import subprocess
import sys
import threading
import time
import traceback
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PROJECT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.join(PROJECT, "Tools")
DATA = os.path.join(PROJECT, "Data")
REPORTS = os.path.join(DATA, "Reports")
DEMO = os.path.join(DATA, "Demo")
TASKS_ROOT = os.path.join(DEMO, "tasks")
WEB_DIR = os.path.join(TOOLS, "web")
UPROJECT = os.path.join(PROJECT, "WorldGen.uproject")

UE_CMD = os.environ.get("WORLDGEN_UE_EDITOR",
                        "UnrealEditor-Cmd.exe")  # PATH or WORLDGEN_UE_EDITOR
UE_GUI = os.environ.get("WORLDGEN_UE_EDITOR_GUI",
                        "UnrealEditor.exe")  # PATH or WORLDGEN_UE_EDITOR_GUI
PYTHON = os.environ.get("WORLDGEN_PYTHON", sys.executable or "python")
# the OpenCV-capable interpreter used only to encode the showcase clip;
# must be provided via WORLDGEN_PY_CV (falls back to the current interpreter)
PY_CV = os.environ.get("WORLDGEN_PY_CV", PYTHON)

SAMPLES = ["A", "B", "C"]
SAMPLE_DIR = {"A": "scene_A", "B": "scene_B", "C": "scene_C"}

STAGES = [
    ("model", "理解描述"),
    ("validate", "检查布局"),
    ("plan", "规划搭建"),
    ("build", "构建场景"),
    ("assets", "核对资产"),
    ("preview", "生成预览"),
    ("e2e", "验证可玩"),
]

# One friendly sentence per stage for the main progress UI. Raw logs stay in
# the collapsed technical-details section only.
STAGE_FRIENDLY = {
    "model": "AI 正在把你的描述翻译成房间布局（尺寸、出入口、家具摆位）…",
    "validate": "检查布局是否可玩：门洞有没有超出墙面、家具有没有挡住出生点和走道…",
    "plan": "把布局转换成虚幻引擎的搭建清单…",
    "build": "在虚幻引擎里搭建房间：墙体、地面、门、家具、灯光…",
    "assets": "确认木桌用上了 Hunyuan3D 生成的 3D 模型…",
    "preview": "渲染场景预览图和展示镜头…",
    "e2e": "自动试玩一遍：捡起钥匙 → 开门 → 走出出口…",
}

REASON_HINTS = [
    (r"exceeds wall run", "门洞超出所在墙的长度：offset_cm 必须让整扇门完整落在墙面上"),
    (r"blocks the player spawn point", "家具压住了角色出生点，角色会卡在物件里"),
    (r"approach corridor", "挡住了出口门洞的通行走廊，需要把该物件挪开"),
    (r"must be different", "入口和出口不能开在同一面墙"),
    (r"on_top_of: unknown reference", "钥匙放在了不存在的家具上"),
    (r"has no position_cm", "钥匙的支撑家具缺少 position_cm 坐标"),
    (r"missing required field", "SceneSpec 缺少必需字段"),
    (r"must be a finite number|must be a string|must be a boolean|must be an object|must be an array",
     "字段类型错误"),
    (r"out of range", "数值超出允许范围"),
    (r"invalid wall", "墙面名称非法（只能是 north / south / east / west）"),
    (r"intrudes into", "物件侵入禁区"),
    (r"plan\.ops", "plan 构建阶段失败"),
    (r"internal error", "内部错误"),
]


def hint_for(line):
    for pat, h in REASON_HINTS:
        if re.search(pat, line, re.I):
            return h
    return ""


def iso():
    return time.strftime("%Y-%m-%dT%H:%M:%S")


def log(msg):
    print("[m3a_web] " + str(msg), flush=True)


def safe_child(root, rel):
    p = os.path.realpath(os.path.join(root, rel))
    r = os.path.realpath(root)
    if p == r or p.startswith(r + os.sep):
        return p
    return None


# ------------------------------------------------------------------ job model
class Job(object):
    def __init__(self, job_id, mode, description, sample=None, scene_tag="ai"):
        self.id = job_id
        self.mode = mode            # "generate" | "regen"
        self.description = description
        self.sample = sample
        self.scene_tag = scene_tag
        self.dir = os.path.join(TASKS_ROOT, job_id)
        self.logs = os.path.join(self.dir, "logs")
        os.makedirs(self.logs, exist_ok=True)
        self.state = "queued"
        self.created = iso()
        self.finished = None
        self.error = None
        self.failed_stage = None
        self.scene_id = None
        self.level = None
        self.results = {}
        self.rounds = []
        self.model = None
        self.stages = [{"key": k, "label": lb, "state": "pending", "detail": "", "lines": [],
                        "log": None} for k, lb in STAGES]
        self.lock = threading.Lock()
        self.dir_map = {k: (k, lb) for k, lb in STAGES}

    # ---- helpers used by the worker (all guarded by self.lock)
    def stage(self, key):
        for s in self.stages:
            if s["key"] == key:
                return s
        raise KeyError(key)

    def begin(self, key, detail=""):
        with self.lock:
            s = self.stage(key)
            s["state"] = "running"
            s["detail"] = detail
            s["started"] = iso()

    def ok(self, key, detail="", lines=None):
        with self.lock:
            s = self.stage(key)
            s["state"] = "done"
            s["detail"] = detail
            s["finished"] = iso()
            if lines:
                s["lines"] = lines[-12:]

    def fail(self, key, detail, lines=None):
        with self.lock:
            s = self.stage(key)
            s["state"] = "failed"
            s["detail"] = detail
            s["finished"] = iso()
            if lines:
                s["lines"] = lines[-12:]
            self.failed_stage = key

    def note(self, key, detail, lines=None):
        with self.lock:
            s = self.stage(key)
            s["detail"] = detail
            if lines:
                s["lines"] = lines[-12:]

    def to_dict(self, queue_pos=None):
        with self.lock:
            return {
                "id": self.id,
                "mode": self.mode,
                "state": self.state,
                "created": self.created,
                "finished": self.finished,
                "error": self.error,
                "failed_stage": self.failed_stage,
                "scene_id": self.scene_id,
                "level": self.level,
                "model": self.model,
                "rounds": self.rounds,
                "stages": [dict(s) for s in self.stages],
                "results": dict(self.results),
                "queue_position": queue_pos,
                "description": self.description,
                "sample": self.sample,
            }

    def write_summary(self, status):
        try:
            with open(os.path.join(self.dir, "task.json"), "w", encoding="utf-8") as f:
                json.dump(self.to_dict(), f, ensure_ascii=False, indent=1)
        except OSError as e:
            log("summary write failed: %s" % e)


# --------------------------------------------------------------- subprocess io
class Runner(object):
    """Runs one subprocess, streams stdout into a log file and into the job's
    live stage detail (so the web UI shows progress while it is still running)."""

    def __init__(self, job, key, log_name):
        self.job = job
        self.key = key
        self.log_path = os.path.join(job.logs, log_name)
        self.proc = None
        self.rc = None
        self.tail = []
        self.lines = []

    def run(self, cmd, timeout=1800, parser=None):
        self.safe_log_line("$ " + " ".join(
            ('"%s"' % c if " " in c else c) for c in cmd))
        env = dict(os.environ)
        env["PYTHONIOENCODING"] = "utf-8"
        env["PYTHONUNBUFFERED"] = "1"
        self.proc = subprocess.Popen(cmd, cwd=PROJECT, env=env, stdout=subprocess.PIPE,
                                     stderr=subprocess.STDOUT, text=True, encoding="utf-8",
                                     errors="replace", bufsize=1)
        t0 = time.time()
        for line in self.proc.stdout:
            line = line.rstrip("\r\n")
            self.lines.append(line)
            self.tail.append(line)
            if len(self.tail) > 200:
                self.tail.pop(0)
            self.safe_log_line(line)
            if parser:
                try:
                    parser(line, self)
                except Exception:  # noqa: BLE001
                    pass
            if time.time() - t0 > timeout:
                self.proc.kill()
                self.safe_log_line("*** TIMEOUT after %ds ***" % timeout)
                break
        self.rc = self.proc.wait()
        return self.rc

    def safe_log_line(self, line):
        try:
            with open(self.log_path, "a", encoding="utf-8") as f:
                f.write(line + "\n")
        except OSError:
            pass

    def read_file(self, path):
        try:
            with open(path, "r", encoding="utf-8", errors="replace") as f:
                return f.read()
        except OSError:
            return ""

    def set_stage_log(self, key):
        self.job.stage(key)["log"] = os.path.relpath(self.log_path, self.job.dir).replace("\\", "/")


def run_simple(job, key, log_name, cmd, timeout=1800, parser=None):
    r = Runner(job, key, log_name)
    r.set_stage_log(key)
    rc = r.run(cmd, timeout=timeout, parser=parser)
    return rc, r


# ------------------------------------------------------------------ pipeline
def parse_planner_line(line, runner):
    m = re.search(r"=== round (\d+)/(\d+): calling model ===", line)
    if m:
        runner.job.note("model", "第 %s/%s 轮：AI 重新生成布局…" % (m.group(1), m.group(2)),
                        runner.tail)
    m = re.search(r"=== spec VALID on round (\d+)", line)
    if m:
        runner.job.note("model", "第 %s 轮布局通过检查" % m.group(1), runner.tail)
    m = re.search(r"spec INVALID \((\d+) error", line)
    if m:
        # surface the concrete problem (e.g. "挡住了出口走道") instead of raw logs
        hint = ""
        for ln in reversed(runner.tail[-30:]):
            hint = hint_for(ln)
            if hint:
                break
        msg = "发现问题：%s。AI 正在调整布局（第 %s 轮被退回）…" % (hint or "布局细节不符合可玩规则", m.group(1))
        runner.job.note("model", msg, runner.tail)


def parse_ue_line(line, runner):
    for pat, label in ((r"M2B HIT", "资产命中"), (r"M2B FALLBACK", "资产回退占位盒"),
                       (r"M2B PLACEHOLDER", "显式占位资产"), (r"WGMat] materials applied", "材质已应用"),
                       (r"level saved", "关卡已保存")):
        if pat in line:
            runner.job.note(runner.key, label, runner.tail)


def pipeline(job):
    job.state = "running"
    spec_path = None
    plan_path = None

    with open(os.path.join(job.dir, "description.txt"), "w", encoding="utf-8") as f:
        f.write(job.description.strip() + "\n")

    # ---------------------------------------------------------------- 1 model
    job.begin("model", STAGE_FRIENDLY["model"])
    planner_out = os.path.join(job.dir, "planner")
    cmd = [PYTHON, os.path.join(TOOLS, "scene_planner.py"),
           "--description-file", os.path.join(job.dir, "description.txt"),
           "--out-dir", planner_out,
           "--scene-tag", job.scene_tag]
    rc, r = run_simple(job, "model", "01_model.log", cmd, timeout=1200, parser=parse_planner_line)
    if rc == 3:
        job.fail("model", "环境里没有 API key（WORLDGEN_LLM_API_KEY），实时生成不可用；"
                          "归档样例仍可查看。", r.tail)
        job.error = "no_api_key"
        return
    if rc == 4:
        job.fail("model", "模型调用失败（网络或额度问题），详见日志。", r.tail)
        job.error = "llm_call_failed"
        return
    if rc != 0:
        lines = [ln for ln in r.lines if ln.strip() and not ln.startswith("[scene_planner]")
                 and not ln.startswith("Log")]
        reasons = [{"raw": ln, "hint": hint_for(ln)} for ln in lines[-6:]]
        job.fail("model", "连续 %d 轮仍无法通过 C++ 校验。" % 3,
                 [ln if not hint_for(ln) else "%s  →  %s" % (ln, hint_for(ln)) for ln in lines[-6:]])
        job.rounds = [{"round": i + 1, "ok": False, "errors": [x["raw"] for x in reasons]}
                      for i in range(len(reasons))]
        job.error = "spec_invalid"
        return

    # locate planner artifacts
    out_dirs = sorted((os.path.join(planner_out, d) for d in os.listdir(planner_out)
                       if os.path.isdir(os.path.join(planner_out, d))), key=os.path.getmtime)
    if not out_dirs:
        job.fail("model", "规划器没有产出目录。", r.tail)
        job.error = "no_planner_output"
        return
    pdir = out_dirs[-1]
    spec_path = os.path.join(pdir, "scene_spec.json")
    summary_path = os.path.join(pdir, "summary.json")
    if not os.path.isfile(spec_path):
        job.fail("model", "规划器没有产出通过校验的 SceneSpec。", r.tail)
        job.error = "no_spec"
        return
    summary = {}
    if os.path.isfile(summary_path):
        with open(summary_path, "r", encoding="utf-8") as f:
            summary = json.load(f)
    job.model = summary.get("model")
    rounds = []
    for a in summary.get("attempts", []):
        rounds.append({"round": a.get("round"), "ok": bool(a.get("ok")),
                       "errors": [{"raw": e, "hint": hint_for(e)} for e in a.get("errors", [])]})
    job.rounds = rounds
    n_rounds = summary.get("rounds_used", len(rounds))
    job.results["planner_dir"] = os.path.relpath(pdir, PROJECT).replace("\\", "/")
    job.results["planner_log"] = os.path.relpath(r.log_path, PROJECT).replace("\\", "/")
    with open(spec_path, "r", encoding="utf-8") as f:
        spec = json.load(f)
    job.scene_id = spec.get("scene_id")
    job.level = "/Game/Generated/%s" % job.scene_id
    job.results["scene_id"] = job.scene_id
    job.results["level"] = job.level
    job.results["room"] = spec.get("room")
    job.results["entrance"] = spec.get("entrance")
    job.results["exit"] = spec.get("exit")
    job.results["objective"] = spec.get("objective")
    job.results["objects"] = spec.get("objects")
    note = "布局完成（模型 %s，第 %d 轮通过）" % (job.model or "?", n_rounds)
    if rounds and not rounds[-1]["ok"]:
        note += "最后一轮仍未通过"
    job.ok("model", note, [ln for ln in r.tail if "round" in ln or "INVALID" in ln][-8:])

    # copy the accepted spec into the task dir (immutable artifact)
    task_spec = os.path.join(job.dir, "scene_spec.json")
    with open(task_spec, "w", encoding="utf-8") as f:
        json.dump(spec, f, ensure_ascii=False, indent=2)
    job.results["spec"] = "scene_spec.json"
    job.results["planner_rounds"] = rounds

    # ------------------------------------------------------------- 2 validate
    job.begin("validate", STAGE_FRIENDLY["validate"])
    err_out = os.path.join(job.logs, "02_validate.txt")
    cmd = [UE_CMD, UPROJECT, "-game", "-M1cCheckSpec=" + task_spec, "-M1cErrorOut=" + err_out,
           "-unattended", "-nopause", "-nosplash", "-abslog=" + os.path.join(job.logs, "02_validate_ue.log")]
    rc, r = run_simple(job, "validate", "02_validate.log", cmd, timeout=600)
    report = r.read_file(err_out).strip().splitlines()
    if report and report[0].strip() == "VALID":
        job.ok("validate", "布局检查通过：门洞位置、出生点、走道都符合可玩规则")
    else:
        errs = [ln for ln in report[1:] if ln.strip()]
        job.fail("validate", "校验未通过：%s" % (hint_for(errs[0]) if errs and hint_for(errs[0]) else (errs[0] if errs else "无详细原因")),
                 ["%s  →  %s" % (e, hint_for(e)) if hint_for(e) else e for e in errs[:6]])
        job.error = "validate_failed"
        return

    # ----------------------------------------------------------------- 3 plan
    job.begin("plan", STAGE_FRIENDLY["plan"])
    cmd = [UE_CMD, UPROJECT, "-game", "-M1bGenerate", "-M1bSpec=" + task_spec,
           "-unattended", "-nopause", "-nosplash", "-abslog=" + os.path.join(job.logs, "03_plan_ue.log")]
    rc, r = run_simple(job, "plan", "03_plan.log", cmd, timeout=600, parser=parse_ue_line)
    gen_plan = os.path.join(REPORTS, "%s.plan.json" % job.scene_id)
    if rc != 0 or not os.path.isfile(gen_plan):
        job.fail("plan", "plan 生成失败（exit=%s）。" % rc, r.tail)
        job.error = "plan_failed"
        return
    plan_path = os.path.join(job.dir, "plan.json")
    with open(gen_plan, "r", encoding="utf-8") as src, open(plan_path, "w", encoding="utf-8") as dst:
        dst.write(src.read())
    with open(plan_path, "r", encoding="utf-8") as f:
        plan = json.load(f)
    ops = plan.get("ops", [])
    job.results["plan"] = "plan.json"
    job.results["plan_ops"] = len(ops)
    job.ok("plan", "plan 就绪：%d 个 op（墙体/地面/门/钥匙/灯光/家具）" % len(ops))
    job.note("plan", "plan 就绪：%d 个 op" % len(ops))

    # ---------------------------------------------------------------- 4 build
    job.begin("build", STAGE_FRIENDLY["build"])
    with open(os.path.join(REPORTS, "m1b_current_plan.txt"), "w", encoding="utf-8") as f:
        f.write(os.path.abspath(gen_plan))
    umap = os.path.join(PROJECT, "Content", "Generated", "%s.umap" % job.scene_id)
    umap_before = os.path.getmtime(umap) if os.path.isfile(umap) else 0.0
    cmd = [UE_GUI, UPROJECT, "-nosplash", "-unattended",
           "-abslog=" + os.path.join(job.logs, "04_build_ue.log"),
           "-ExecutePythonScript=" + os.path.join(TOOLS, "m1b_build_level.py").replace("/", "\\")]
    rc, r = run_simple(job, "build", "04_build.log", cmd, timeout=1800, parser=parse_ue_line)
    hit = [ln for ln in r.lines if "M2B HIT" in ln or "M2B FALLBACK" in ln or "M2B PLACEHOLDER" in ln]
    new_umap = os.path.isfile(umap) and os.path.getmtime(umap) > umap_before + 0.5
    if not new_umap:
        job.fail("build", "关卡文件没有更新（建关脚本失败），不会显示预览。", r.tail)
        job.error = "build_failed"
        return
    job.note("build", "关卡已重建：%s" % umap, hit[-6:])

    # lighting + materials are part of the repeatable flow (visual only)
    polish_cfg = {"level": job.level, "scene_id": job.scene_id}
    with open(os.path.join(TOOLS, "m2bs_polish_config.json"), "w", encoding="utf-8") as f:
        json.dump(polish_cfg, f)
    cmd = [UE_GUI, UPROJECT, "-nosplash", "-unattended",
           "-abslog=" + os.path.join(job.logs, "04b_polish_ue.log"),
           "-ExecutePythonScript=" + os.path.join(TOOLS, "m2bs_polish_level.py").replace("/", "\\")]
    rc, r2 = run_simple(job, "build", "04b_polish.log", cmd, timeout=1800, parser=parse_ue_line)
    mat = [ln.split("]")[-1].strip() for ln in r2.lines if "materials applied" in ln]
    job.ok("build", "关卡已重建（含材质/灯光）：%s" % job.level,
           ([mat[-1]] if mat else []) + hit[-5:])
    job.results["umap"] = "Content/Generated/%s.umap" % job.scene_id

    # --------------------------------------------------------------- 5 assets
    job.begin("assets", STAGE_FRIENDLY["assets"])
    with open(os.path.join(REPORTS, "m2b_umap_check_input.json"), "w", encoding="utf-8") as f:
        json.dump({"level_path": job.level}, f)
    cmd = [UE_GUI, UPROJECT, "-nosplash", "-unattended",
           "-abslog=" + os.path.join(job.logs, "05_assets_ue.log"),
           "-ExecutePythonScript=" + os.path.join(TOOLS, "m2b_check_umap.py").replace("/", "\\")]
    rc, r = run_simple(job, "assets", "05_assets.log", cmd, timeout=1200)
    res_path = os.path.join(REPORTS, "m2b_umap_check_result.json")
    dump = {}
    if os.path.isfile(res_path):
        with open(res_path, "r", encoding="utf-8") as f:
            dump = json.load(f)
    with open(os.path.join(job.dir, "umap_check.json"), "w", encoding="utf-8") as f:
        json.dump(dump, f, ensure_ascii=False, indent=1)
    hits = [a for a in dump.get("actors", []) if "M2A_desk_001" in (a.get("mesh") or "")]
    if not dump.get("loaded"):
        job.fail("assets", "关卡无法加载，无法核验资产。", r.tail)
        job.error = "umap_check_failed"
        return
    if not hits:
        job.fail("assets", "关卡里没有引用生成资产 M2A_desk_001（发生了回退占位盒）。",
                 [ln for ln in r.lines if "M2B" in ln][-6:])
        job.error = "asset_fallback"
        return
    job.results["umap_check"] = "umap_check.json"
    job.results["desk_asset"] = "M2A_desk_001"
    job.results["assets"] = asset_provenance()
    job.ok("assets", "木桌已确认使用 Hunyuan3D 生成的 3D 模型（M2A_desk_001）",
           ["%s -> %s" % (hits[0]["label"], hits[0]["mesh"])])

    # -------------------------------------------------------------- 6 preview
    job.begin("preview", STAGE_FRIENDLY["preview"])
    # The locked visual baseline (camera angles, sizes, clip length) lives in
    # Tools/m3a_preview_baseline.json and is applied by the same writer the
    # scripted chain uses, so a web run and a scripted rebuild render alike.
    out_dir = os.path.join(job.dir, "preview")
    cmd = [PYTHON, os.path.join(TOOLS, "m3a_write_preview_config.py"),
           job.scene_id, task_spec, out_dir, job.scene_tag[:1].upper()]
    rc, r = run_simple(job, "preview", "06_preview_cfg.log", cmd, timeout=120)
    if rc != 0:
        job.fail("preview", "写预览配置失败。", r.tail)
        job.error = "preview_config_failed"
        return
    cmd = [UE_GUI, UPROJECT, "-nosplash", "-unattended",
           "-abslog=" + os.path.join(job.logs, "06_preview_ue.log"),
           "-ExecutePythonScript=" + os.path.join(TOOLS, "m3a_previews.py").replace("/", "\\")]
    rc, r = run_simple(job, "preview", "06_preview.log", cmd, timeout=3600)
    man_path = os.path.join(out_dir, "preview_manifest.json")
    if not os.path.isfile(man_path):
        job.fail("preview", "预览渲染失败（没有产出 manifest），不会显示旧图。", r.tail)
        job.error = "preview_failed"
        return
    with open(man_path, "r", encoding="utf-8") as f:
        man = json.load(f)
    job.results["preview_manifest"] = "preview/preview_manifest.json"
    # normalize keys to what the UI expects ("overview", "desk_key", ...):
    # the manifest writes its stills under "preview_<name>" keys.
    job.results["preview"] = {
        (k[len("preview_"):] if k.startswith("preview_") else k): "preview/" + (v or {}).get("file", "")
        for k, v in man["camera"]["stills"].items()}
    job.results["preview_at"] = man.get("generated_at")
    job.results["preview_scene_id"] = man.get("scene_id")
    job.results["clip_frames"] = man["camera"]["clip_frames"]
    job.results["clip_seconds"] = man["camera"]["clip_seconds"]
    # encode the showcase frames (needs OpenCV, hence the separate interpreter)
    clip_rel = "preview/showcase_clip.mp4"
    clip_abs = os.path.join(job.dir, clip_rel)
    cmd = [PY_CV, os.path.join(TOOLS, "m3a_encode_clip.py"),
           os.path.join(out_dir, "showcase"), clip_abs, "20"]
    rcc, rc_ = run_simple(job, "preview", "06_clip.log", cmd, timeout=900)
    if rcc == 0 and os.path.isfile(clip_abs):
        job.results["clip"] = clip_rel
    else:
        job.note("preview", "展示镜头 MP4 编码失败（帧仍在 preview/showcase/）。", rc_.tail)
    job.ok("preview", "预览已生成：%d 张静态图 + %d 帧展示镜头（%s 秒）"
           % (len(job.results.get("preview", {})), man["camera"]["clip_frames"],
              man["camera"]["clip_seconds"]))

    # ------------------------------------------------------------------ 7 e2e
    job.begin("e2e", STAGE_FRIENDLY["e2e"])
    e2e_log = os.path.join(job.logs, "07_e2e_ue.log")
    cmd = [UE_CMD, UPROJECT, job.level, "-game", "-M1bE2E", "-unattended", "-nopause",
           "-nosplash", "-abslog=" + e2e_log]
    rc, r = run_simple(job, "e2e", "07_e2e.log", cmd, timeout=1800)
    text = r.read_file(e2e_log)
    m = re.search(r"E2E PLAYTHROUGH: (\d+) checks, (\d+) failures", text)
    checks, failures = (int(m.group(1)), int(m.group(2))) if m else (0, -1)
    od = re.findall(r"outside_dist=([\d.]+)", text)
    passes = re.findall(r"\[M1bTest (\d+)\].*-> (PASS|FAIL)", text)
    with open(os.path.join(job.dir, "e2e.log"), "w", encoding="utf-8") as f:
        f.write(text)
    job.results["e2e"] = {"checks": checks, "failures": failures, "outside_dist": od[-1] if od else None,
                          "log": "e2e.log", "results": [{"check": int(a), "verdict": b} for a, b in passes]}
    if failures == 0 and checks >= 5:
        job.ok("e2e", "自动试玩通过：捡起钥匙、开门、走出出口（%d 项检查全部通过）" % checks,
               [ln.split("Display: ")[-1] for ln in r.lines if "M1bTest" in ln][-6:])
        job.state = "done"
    else:
        job.fail("e2e", "游玩验收未通过（%s 项失败）。" % (failures if failures >= 0 else "?"),
                 [ln.split("Display: ")[-1] for ln in r.lines if "M1bTest" in ln][-8:])
        job.error = "e2e_failed"
    job.finish()


def job_finish(job):
    job.finished = iso()
    if job.state not in ("done", "failed"):
        job.state = "failed" if job.error else "done"
    job.write_summary(job.state)


Job.finish = lambda self: job_finish(self)


# ------------------------------------------------------------------ scheduler
JOBS = {}
JOB_ORDER = []
QUEUE = queue.Queue()
LOCK = threading.Lock()
CURRENT = {"job_id": None}


def worker_loop():
    while True:
        job = QUEUE.get()
        with LOCK:
            CURRENT["job_id"] = job.id
        try:
            pipeline(job)
        except Exception as e:  # noqa: BLE001
            log("pipeline crashed: %s\n%s" % (e, traceback.format_exc()))
            key = job.failed_stage or "model"
            job.fail(key, "内部错误：%s" % e)
            job.error = "internal"
            job_finish(job)
        finally:
            with LOCK:
                CURRENT["job_id"] = None
            QUEUE.task_done()


def new_job(mode, description, sample=None):
    stamp = time.strftime("%Y%m%d_%H%M%S")
    tag = (sample or "free").lower()
    job_id = "%s_%s" % (stamp, tag)
    n = 1
    while os.path.exists(os.path.join(TASKS_ROOT, job_id)):
        n += 1
        job_id = "%s_%s_%d" % (stamp, tag, n)
    job = Job(job_id, mode, description, sample=sample, scene_tag=tag)
    with LOCK:
        JOBS[job.id] = job
        JOB_ORDER.append(job.id)
        pos = QUEUE.qsize() + (1 if CURRENT["job_id"] else 0)
    QUEUE.put(job)
    log("job %s queued (mode=%s, %d chars)" % (job.id, mode, len(description)))
    return job, pos


def queue_position(job):
    with LOCK:
        if job.state != "queued":
            return 0
        pending = [j for j in JOB_ORDER if JOBS[j].state == "queued"]
        try:
            return pending.index(job.id) + (1 if CURRENT["job_id"] else 0)
        except ValueError:
            return 0


# ------------------------------------------------------------------- archives
def read_text(rel, limit=200000):
    p = safe_child(PROJECT, rel)
    if not p or not os.path.isfile(p):
        return None
    try:
        with open(p, "r", encoding="utf-8", errors="replace") as f:
            return f.read()[:limit]
    except OSError:
        return None


def e2e_summary(text):
    if not text:
        return {}
    m = re.search(r"E2E PLAYTHROUGH: (\d+) checks, (\d+) failures", text)
    od = re.findall(r"outside_dist=([\d.]+)", text)
    ts = re.findall(r"^\[(\d{4}\.\d{2}\.\d{2}-\d{2}\.\d{2}\.\d{2})", text, re.M)
    return {"checks": int(m.group(1)) if m else None,
            "failures": int(m.group(2)) if m else None,
            "outside_dist": od[-1] if od else None,
            "verdict": "ALL PASS" if m and m.group(2) == "0" else "FAIL",
            "log_start": ts[0] if ts else None,
            "log_end": ts[-1] if ts else None}


def archive_info(key):
    key = key.upper()
    if key not in SAMPLES:
        return None
    d = SAMPLE_DIR[key]
    base = os.path.join(DEMO, d)
    desc = read_text(os.path.join("Data", "Demo", d, "description.txt")) or ""
    spec_txt = read_text(os.path.join("Data", "Demo", d, "scene_spec.json")) or "{}"
    plan_txt = read_text(os.path.join("Data", "Demo", d, "plan.json")) or "{}"
    e2e_txt = read_text(os.path.join("Data", "Demo", d, "e2e.log"))
    umap_txt = read_text(os.path.join("Data", "Demo", d, "umap_check.json")) or "{}"
    spec = json.loads(spec_txt)
    plan = json.loads(plan_txt)
    umap = json.loads(umap_txt)
    prev = os.path.join(base, "preview")
    previews = {}
    for k, name in (("overview", "preview_overview.png"), ("desk_key", "preview_desk_key.png"),
                    ("key", "preview_key.png"), ("exit", "preview_exit.png")):
        p = os.path.join(prev, name)
        if os.path.isfile(p):
            previews[k] = "/archive_media/%s/preview/%s" % (key, name)
    clip = os.path.join(prev, "showcase_%s.mp4" % key)
    if os.path.isfile(clip):
        previews["clip"] = "/archive_media/%s/preview/showcase_%s.mp4" % (key, key)
    man = None
    mp = os.path.join(prev, "preview_manifest.json")
    if os.path.isfile(mp):
        with open(mp, "r", encoding="utf-8") as f:
            man = json.load(f)
    hits = [a for a in umap.get("actors", []) if "M2A_desk_001" in (a.get("mesh") or "")]
    return {
        "kind": "archive",
        "sample": key,
        "archive_rel": d,
        "label": "归档样例 %s（已验证的历史产物，未调用模型）" % key,
        "description": desc.strip(),
        "spec": spec, "plan_ops": len(plan.get("ops", [])),
        "scene_id": spec.get("scene_id"),
        "level": "/Game/Generated/%s" % spec.get("scene_id"),
        "room": spec.get("room"), "entrance": spec.get("entrance"), "exit": spec.get("exit"),
        "objects": spec.get("objects"), "objective": spec.get("objective"),
        "desk_asset": "M2A_desk_001" if hits else None,
        "desk_actor": hits[0]["label"] if hits else None,
        "assets": asset_provenance(),
        "e2e": e2e_summary(e2e_txt),
        "previews": previews,
        "preview_manifest": man,
        "preview_at": (man or {}).get("generated_at"),
        "clip_frames": ((man or {}).get("camera") or {}).get("clip_frames"),
        "clip_seconds": ((man or {}).get("camera") or {}).get("clip_seconds"),
        "files": {
            "description": "Data/Demo/%s/description.txt" % d,
            "spec": "Data/Demo/%s/scene_spec.json" % d,
            "plan": "Data/Demo/%s/plan.json" % d,
            "e2e": "Data/Demo/%s/e2e.log" % d,
            "umap_check": "Data/Demo/%s/umap_check.json" % d,
        },
        "archive_dir": "Data/Demo/%s" % d,
    }


# ---------------------------------------------------------------- http server
class Handler(BaseHTTPRequestHandler):
    server_version = "M3aWeb/1.0"

    def log_message(self, fmt, *args):
        pass  # keep the console readable

    # ---- helpers
    def _send(self, code, body, ctype="application/json; charset=utf-8", extra=None):
        if isinstance(body, str):
            body = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def _json(self, obj, code=200):
        self._send(code, json.dumps(obj, ensure_ascii=False))

    def _file(self, path, ctype=None, download_name=None):
        if not path or not os.path.isfile(path):
            self._json({"error": "not found"}, 404)
            return
        if ctype is None:
            ext = os.path.splitext(path)[1].lower()
            ctype = {".png": "image/png", ".jpg": "image/jpeg", ".jpeg": "image/jpeg",
                     ".mp4": "video/mp4", ".json": "application/json; charset=utf-8",
                     ".css": "text/css; charset=utf-8",
                     ".js": "text/javascript; charset=utf-8",
                     ".log": "text/plain; charset=utf-8", ".txt": "text/plain; charset=utf-8",
                     ".md": "text/markdown; charset=utf-8", ".html": "text/html; charset=utf-8"}.get(
                ext, "application/octet-stream")
        with open(path, "rb") as f:
            data = f.read()
        self._send(200, data, ctype)

    def _body(self):
        n = int(self.headers.get("Content-Length") or 0)
        if not n:
            return {}
        try:
            return json.loads(self.rfile.read(n).decode("utf-8"))
        except (ValueError, UnicodeDecodeError):
            return {}

    # ---- routing
    def do_GET(self):
        u = urllib.parse.urlparse(self.path)
        q = urllib.parse.parse_qs(u.query)
        p = u.path
        if p in ("/", "/index.html"):
            return self._file(os.path.join(WEB_DIR, "index.html"), "text/html; charset=utf-8")
        if p in ("/app.js", "/style.css", "/i18n.js"):
            return self._file(os.path.join(WEB_DIR, p.lstrip("/")))
        if p == "/api/state":
            with LOCK:
                cur = JOBS.get(CURRENT["job_id"]) if CURRENT["job_id"] else None
                cur_d = cur.to_dict() if cur else None
                queued = [j for j in JOB_ORDER if JOBS[j].state == "queued"]
            return self._json({"busy": cur is not None, "current": cur_d,
                               "queued": len(queued),
                               "api_key": bool(os.environ.get("WORLDGEN_LLM_API_KEY")
                                               or os.environ.get("ZHIPUAI_API_KEY")),
                               # config value only; the UI must show the model
                               # recorded in the actual run (job.model) instead
                               "model_config": read_planner_model()})
        if p == "/api/samples":
            out = []
            for k in SAMPLES:
                a = archive_info(k)
                out.append({"key": k, "scene_id": a["scene_id"], "description": a["description"],
                            "room": a["room"], "entrance": a["entrance"], "exit": a["exit"],
                            "has_preview": bool(a["previews"]), "e2e": a["e2e"],
                            "title": room_title(a)})
            return self._json(out)
        if p.startswith("/api/archive/"):
            key = p[len("/api/archive/"):].split("/")[0]
            a = archive_info(key)
            if not a:
                return self._json({"error": "unknown sample"}, 404)
            return self._json(a)
        if p.startswith("/api/task/"):
            rest = p[len("/api/task/"):]
            parts = rest.split("/")
            job = JOBS.get(parts[0])
            if not job:
                return self._json({"error": "unknown task"}, 404)
            if len(parts) == 1:
                d = job.to_dict(queue_position(job))
                if job.state == "done":
                    d["artifacts"] = task_artifacts(job)
                return self._json(d)
            if len(parts) >= 2 and parts[1] == "file":
                rel = q.get("path", [""])[0]
                path = safe_child(job.dir, rel)
                if not path:
                    return self._json({"error": "bad path"}, 400)
                return self._file(path)
            if len(parts) >= 2 and parts[1] == "log":
                rel = q.get("name", [""])[0]
                path = safe_child(job.logs, rel)
                if not path:
                    return self._json({"error": "bad path"}, 400)
                return self._file(path)
            return self._json({"error": "not found"}, 404)
        if p.startswith("/media/"):
            rel = p[len("/media/"):]
            path = safe_child(DEMO, urllib.parse.unquote(rel))
            return self._file(path)
        if p.startswith("/archive_media/"):
            rest = p[len("/archive_media/"):].split("/", 1)
            key = rest[0].upper()
            if key.startswith("SCENE_"):
                key = key[len("SCENE_"):]
            if len(rest) != 2 or key not in SAMPLES:
                return self._json({"error": "bad archive media path"}, 400)
            path = safe_child(os.path.join(DEMO, SAMPLE_DIR[key]), rest[1])
            return self._file(path)
        return self._json({"error": "not found"}, 404)

    def do_POST(self):
        u = urllib.parse.urlparse(self.path)
        p = u.path
        body = self._body()
        if p == "/api/generate":
            desc = (body.get("description") or "").strip()
            if len(desc) < 6:
                return self._json({"error": "描述太短（至少 6 个字符）"}, 400)
            if len(desc) > 4000:
                return self._json({"error": "描述太长（上限 4000 字符）"}, 400)
            job, pos = new_job("generate", desc)
            return self._json({"task_id": job.id, "queue_position": pos})
        if p == "/api/regenerate":
            key = str(body.get("sample") or "").upper()
            if key not in SAMPLES:
                return self._json({"error": "unknown sample"}, 400)
            desc = read_text(os.path.join("Data", "Demo", SAMPLE_DIR[key], "description.txt"))
            if not desc:
                return self._json({"error": "sample description missing"}, 500)
            job, pos = new_job("regen", desc.strip(), sample=key)
            return self._json({"task_id": job.id, "queue_position": pos})
        if p.startswith("/api/open"):
            return self._open_playable(body)
        return self._json({"error": "not found"}, 404)

    def _open_playable(self, body):
        """Launch the playable level window. Fixed exe + fixed arguments; the
        level path comes from the validated scene_id, never from user text."""
        level = None
        task_id = body.get("task_id")
        sample = str(body.get("sample") or "").upper()
        if task_id:
            job = JOBS.get(task_id)
            if not job or job.state != "done" or not job.scene_id:
                return self._json({"error": "该任务还没有完成建关，不能打开"}, 400)
            level = job.level
        elif sample in SAMPLES:
            a = archive_info(sample)
            level = a["level"]
        if not level:
            return self._json({"error": "缺少 level"}, 400)
        sid = level.rsplit("/", 1)[-1]
        if not re.match(r"^[A-Za-z_][A-Za-z0-9_]{0,63}$", sid):
            return self._json({"error": "非法 scene_id"}, 400)
        umap = os.path.join(PROJECT, "Content", "Generated", sid + ".umap")
        if not os.path.isfile(umap):
            return self._json({"error": "关卡文件不存在：%s" % umap}, 404)
        cmd = [UE_GUI, UPROJECT, level, "-game", "-windowed", "-ResX=1600", "-ResY=900", "-nosplash"]
        try:
            subprocess.Popen(cmd, cwd=PROJECT)
        except OSError as e:
            return self._json({"error": "启动失败：%s" % e}, 500)
        log("launched playable: %s" % level)
        return self._json({"ok": True, "level": level,
                           "hint": "已启动 UE 可玩窗口：W/A/S/D 移动，E 拾取钥匙 / 开门"})

    def do_OPTIONS(self):
        self._send(204, b"", "text/plain")


def read_planner_model():
    p = os.path.join(TOOLS, "scene_planner.json")
    try:
        with open(p, "r", encoding="utf-8") as f:
            return json.load(f).get("model")
    except OSError:
        return None


def asset_provenance():
    """Asset source/licence info for the technical-details section. Read from
    the on-disk manifests, never hardcoded in the UI."""
    desk = {}
    try:
        with open(os.path.join(DATA, "Assets", "asset_manifest.json"), "r", encoding="utf-8") as f:
            m = json.load(f)
        a = next((x for x in m.get("assets", []) if x.get("asset_id") == "m2a_desk_001"), None)
        if a:
            desk = {"asset_id": a.get("asset_id"), "source": a.get("source"),
                    "source_model": a.get("source_model"), "license": a.get("license"),
                    "target_size_cm": a.get("target_size_cm"),
                    "input_image_origin": a.get("input_image_origin")}
    except (OSError, ValueError):
        pass
    tex = {}
    try:
        with open(os.path.join(DATA, "Assets", "m3a", "tex", "provenance.json"), "r",
                  encoding="utf-8") as f:
            tex = json.load(f)
    except (OSError, ValueError):
        pass
    key_prop = {"asset_id": "SM_Key_Prop",
                "source": "procedural (Tools/m3a_make_key.py, trimesh primitives)",
                "license": "project-owned"}
    return {"desk": desk, "textures": tex, "key_prop": key_prop}


def room_title(a):
    r = a.get("room") or {}
    en = (a.get("entrance") or {}).get("wall")
    ex = (a.get("exit") or {}).get("wall")
    zh = {"north": "北", "south": "南", "east": "东", "west": "西"}
    return "%s×%s cm，%s入口/%s出口" % (r.get("width_cm"), r.get("length_cm"),
                                       zh.get(en, en), zh.get(ex, ex))


def task_artifacts(job):
    out = {}
    for name in ("scene_spec.json", "plan.json", "umap_check.json", "e2e.log", "task.json"):
        p = os.path.join(job.dir, name)
        if os.path.isfile(p):
            out[name] = "Data/Demo/tasks/%s/%s" % (job.id, name)
    pm = os.path.join(job.dir, "preview", "preview_manifest.json")
    if os.path.isfile(pm):
        out["preview_manifest.json"] = "Data/Demo/tasks/%s/preview/preview_manifest.json" % job.id
    logs = []
    if os.path.isdir(job.logs):
        logs = sorted(os.listdir(job.logs))
    out["logs"] = ["Data/Demo/tasks/%s/logs/%s" % (job.id, n) for n in logs]
    out["dir"] = "Data/Demo/tasks/%s" % job.id
    return out


def main():
    global UE_CMD, UE_GUI, PYTHON
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--no-browser", action="store_true")
    args = ap.parse_args()

    os.makedirs(TASKS_ROOT, exist_ok=True)
    for exe in (UE_CMD, UE_GUI):
        if not os.path.isfile(exe):
            log("WARNING: UE executable not found: %s" % exe)
    key = bool(os.environ.get("WORLDGEN_LLM_API_KEY") or os.environ.get("ZHIPUAI_API_KEY"))
    log("model=%s  api key in env: %s" % (read_planner_model(), "yes" if key else "NO"))

    t = threading.Thread(target=worker_loop, daemon=True)
    t.start()

    srv = ThreadingHTTPServer((args.host, args.port), Handler)
    url = "http://%s:%d/" % (args.host, args.port)
    log("serving %s  (local only; open %s)" % (WEB_DIR, url))
    if not args.no_browser:
        try:
            import webbrowser
            threading.Timer(0.8, lambda: webbrowser.open(url)).start()
        except Exception:  # noqa: BLE001
            pass
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        log("stopped")


if __name__ == "__main__":
    main()
