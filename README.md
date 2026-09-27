# WorldGen — AI-Generated Playable UE5 Scenes from Natural Language
# WorldGen — 用自然语言生成可玩 UE5 场景的 AI 管线
# WorldGen — 自然言語からプレイ可能な UE5 シーンを生成する AI パイプライン

## 概要 / Overview / 概要

**中文**：WorldGen 是一条「全程 AI 生成」的场景构建管线——**世界与布局由 AI 规划**（LLM 把一句话描述变成结构化场景规格），**3D 模型由 AI 生成**（Hunyuan3D image-to-mesh 预生成资产），再由 C++ 校验器把关布局可玩性、C++ 确定性生成搭建清单、编辑器机械建关，最后由自动化试玩（走到钥匙 → 拾取 → 开门 → 离开房间）验收通过才算完成。

**English**: WorldGen is a fully AI-generated scene-building pipeline — **the world and its layout are planned by AI** (an LLM turns a one-sentence description into a structured scene spec) and **the 3D models are AI-generated** (pre-generated Hunyuan3D image-to-mesh assets). A C++ validator gates playability, a deterministic C++ builder produces the construction plan, the editor builds the level mechanically, and an automated playthrough (walk to key → pick up → unlock door → exit) must pass before a scene is accepted.

**日本語**：WorldGen は「完全 AI 生成」のシーン構築パイプラインです。**世界とレイアウトは AI が計画**し（LLM が一文の説明を構造化されたシーン仕様に変換）、**3D モデルも AI が生成**します（Hunyuan3D の image-to-mesh で事前生成したアセット）。C++ バリデータが遊びやすさを検証し、決定論的な C++ ビルダーが構築計画を生成、エディタが機械的にレベルを組み立て、最後に自動プレイスルー（鍵へ移動 → 拾う → ドア解錠 → 脱出）に合格して初めて完成とみなされます。

> **中文**：这是**代码展示版（portfolio showcase）**，不是可直接运行的完整项目。仓库只含源代码与少量说明数据格式的 JSON 样例；**不含** UE 二进制资产（网格/材质/关卡 umap）、Hunyuan3D 模型权重与生成产物、演示视频/截图。克隆并编译后可得可构建的 UE 工程（骨架房间+占位图形），但**不能直接得到文档/视频里展示的完整可玩 Demo**。
>
> **English**: This is a **code showcase**, not a directly runnable project. The repo contains source code and a few JSON samples documenting data formats only; it does **not** include UE binary assets (meshes/materials/level umaps), Hunyuan3D weights or generated outputs, or demo videos/screenshots. After cloning and compiling you get a buildable UE project (skeleton rooms + placeholder graphics), but **not the full playable demo shown in the write-ups/videos**.
>
> **日本語**：これは**コードショーケース**であり、そのまま実行できる完全なプロジェクトではありません。リポジトリにはソースコードとデータ形式を説明する JSON サンプルのみを含み、UE のバイナリアセット（メッシュ/マテリアル/レベル umap）、Hunyuan3D のモデル重みと生成物、デモ動画/スクリーンショットは**含みません**。クローンしてコンパイルすればビルド可能な UE プロジェクト（骨組みの部屋＋プレースホルダー表示）は得られますが、**資料や動画で見せる完全なプレイ可能デモは得られません**。

## 管线架构 / Pipeline Architecture / パイプライン構成

```
natural language (zh/en/ja)
     |
     v  Tools/scene_planner.py          LLM planning (DeepSeek / GLM, key via env var only)
SceneSpec 0.1 JSON <-+
     |               |  validation failure: coordinate-specific errors fed back (<= 2 rounds)
     v               |
UnrealEditor-Cmd -M1cCheckSpec          C++ validation (FSceneSpecValidator, 12 rule sets
     |                                  + placement geometry: spawn blocking / doorway clearance)
SceneSpec VALID
     |
     v  UnrealEditor-Cmd -M1bGenerate   deterministic build plan (FLevelPlanBuilder, C++)
plan.json (ops with position/size/rotation/asset_id)
     |                                  determinism gate: same spec twice -> byte-identical plan
     v  UnrealEditor -ExecutePythonScript Tools/m1b_build_level.py
level umap (walls/floor/door/key/furniture) + Tools/m2bs_polish_level.py (lights/materials/labels)
     |
     v  Tools/m2b_check_umap.py         asset binding audit (real mesh vs fallback cube)
     v  Tools/m3a_previews.py           preview stills + camera frames (Tools/m3a_encode_clip.py -> MP4)
UnrealEditor-Cmd <level> -game -M1bE2E  automated playthrough (real input injection, no teleport):
     |                                  pick key -> open door -> fully exit past outer wall >= 34cm, 5 assertions
     v
acceptance must pass; failures report the exact blocker, never fake success
```

### 运行时组件 / Runtime Components / ランタイム構成（Source/WorldGenRuntime）

| Component | 中文职责 | Role (EN) | 役割 (JA) |
| --- | --- | --- | --- |
| `SceneSpec` / `FSceneSpecValidator` | 数据契约 0.1（厘米/度）+ 12 组校验规则，错误带字段路径 | Data contract 0.1 (cm/degrees) + 12 validation rule sets, field-path errors | データ契約 0.1（cm/度）＋12 種の検証ルール、エラーはフィールドパス付き |
| `LevelPlanBuilder` | 校验通过的 spec → 确定性 plan（墙/门/家具/钥匙/灯光 op 序列） | Valid spec -> deterministic plan (wall/door/furniture/key/light op sequence) | 検証済み spec → 決定論的 plan（壁/扉/家具/鍵/ライト op 列） |
| `WorldGenCharacter` | 步行角色（WASD + E 交互） | Walking character (WASD + E interact) | 歩行キャラクター（WASD＋E で操作） |
| `WorldGenDoor` / `WorldGenExitTrigger` | 上锁门（拾钥匙解锁）与出口判定触发区 | Locked door (unlocked by key) + exit trigger volume | 鍵のかかるドア（鍵で解錠）＋脱出判定トリガー |
| `WorldGenPickup` | 钥匙拾取（程序化金钥匙网格，E 键边沿触发 + 几何兜底） | Key pickup (procedural gold key mesh, edge-triggered E + geometric fallback) | 鍵の取得（プロシージャルな金の鍵メッシュ、E キーで取得＋幾何フォールバック） |
| `WorldGenGameMode` / `WorldGenGameState` | 关卡状态机与 E2E 自动试玩验收（逐帧输入注入） | Level state machine + E2E playthrough acceptance (per-frame input injection) | レベル状態機械＋E2E 自動プレイ验收（フレーム毎の入力注入） |

### 资产管线 / Asset Pipeline / アセットパイプライン（Hunyuan3D）

**中文**：场景中的木桌是 Hunyuan3D-2.1 形状生成（image-to-mesh）**预先生成**的 AI 资产（70 万面，简单盒碰撞）。仓库只保留资产处理脚本：`m2a_normalize.py`（网格归一化：Y→Z-up、目标尺寸、底面 pivot）、`m2a_import_mesh.py`（导入 UE + 盒碰撞）、`Data/Assets/asset_manifest.json`（绑定表：plan 的 `asset_id` 查不到就回退占位盒并记录 `M2B FALLBACK`，绝不冒充成功）。模型权重、推理环境与原始网格**不在本仓库**（体积与许可原因）。

**English**: The wooden desk in the scenes is an **AI-generated asset**, pre-generated with Hunyuan3D-2.1 shape generation (image-to-mesh; ~700k faces, simple box collision). Only the asset-processing scripts are kept: `m2a_normalize.py` (mesh normalization: Y→Z-up, target size, bottom pivot), `m2a_import_mesh.py` (UE import + box collision), and `Data/Assets/asset_manifest.json` (binding table: an unknown plan `asset_id` falls back to a placeholder cube and is logged as `M2B FALLBACK` — never passed off as success). Model weights, the inference environment and raw meshes are **not in this repo** (size and licensing reasons).

**日本語**：シーン内の木の机は、Hunyuan3D-2.1 の形状生成（image-to-mesh）で**事前生成された AI アセット**です（約 70 万面、単純なボックス衝突）。リポジトリにはアセット処理スクリプトのみを含めます：`m2a_normalize.py`（メッシュ正規化：Y→Z-up、目標サイズ、底面ピボット）、`m2a_import_mesh.py`（UE 取り込み＋ボックス衝突）、`Data/Assets/asset_manifest.json`（バインディング表：plan の `asset_id` が見つからなければプレースホルダー立方体にフォールバックし `M2B FALLBACK` を記録、成功と偽りません）。モデル重み・推論環境・生メッシュは**本リポジトリには含みません**（容量とライセンスの理由）。

### 本地 Web Demo / Local Web Demo / ローカル Web デモ（可选 / optional / オプション）

**中文**：`Tools/m3a_web.py` + `Tools/web/`：单页三语（中/EN/日）界面，输入描述 → 七阶段进度 → 结果页（预览四视角 + 打开可玩关卡）。只绑定 `127.0.0.1`；单工作线程队列；用户描述写文件传参、从不拼进命令行；API key 只存在于服务进程环境变量。

**English**: `Tools/m3a_web.py` + `Tools/web/`: single-page trilingual (zh/EN/JA) UI — enter a description, watch seven pipeline stages, get a result page (4 preview angles + open the playable level). Binds to `127.0.0.1` only; single worker queue; user descriptions pass via files, never concatenated into command lines; API keys exist only in the server process environment.

**日本語**：`Tools/m3a_web.py` + `Tools/web/`：シングルページの三言語（中/EN/日）UI。説明文を入力すると 7 段階の進行表示の後、結果ページ（4 角度のプレビュー＋プレイ可能レベルを開く）を表示します。バインドは `127.0.0.1` のみ、ワーカーは単一キュー、ユーザー記述はファイル経由で渡しコマンドラインに連結せず、API キーはサーバープロセスの環境変数にのみ存在します。

## 代码阅读路线 / Suggested Reading Order / コードリーディング順

1. `samples/scene_spec.example.json` / `samples/plan.example.json` — 核心数据格式 / core data formats / 中核となるデータ形式
2. `Source/WorldGenRuntime/Public/SceneSpec.h` — 数据契约 / data contract / データ契約
3. `Source/WorldGenRuntime/Private/SceneSpec.cpp` — 校验规则（什么布局算“可玩”）/ validation rules (what counts as "playable") / 検証ルール（何が「プレイ可能」か）
4. `Source/WorldGenRuntime/Private/LevelPlanBuilder.cpp` — spec → plan 的确定性翻译 / deterministic spec→plan translation / 決定論的な spec→plan 変換
5. `Tools/m1b_build_level.py` — plan 在编辑器里的机械执行 / mechanical plan execution in the editor / エディタでの機械的な plan 実行
6. `Source/WorldGenRuntime/Private/WorldGenGameMode.cpp` — E2E 自动试玩验收 / E2E playthrough acceptance / E2E 自動プレイ验收
7. `Tools/scene_planner.py` — LLM 规划 + 校验反馈环 / LLM planning + validation feedback loop / LLM 計画＋検証フィードバックループ
8. `Tools/m3a_web.py` — 把上述全部串成本地网页 Demo / ties it all into the local web demo / 全部をつなぐローカル Web デモ

## 环境要求 / Requirements / 必要環境

- **中文**：Unreal Engine **5.7**（C++ 工程需源码版或含编译工具的 Epic 版）；Web Demo 需 Python 3.x（标准库即可，MP4 编码另需 OpenCV）；LLM 规划用任何 OpenAI 兼容接口（本项目用 DeepSeek），key 经 `WORLDGEN_LLM_API_KEY` 环境变量传入，**不写入任何文件**。
- **English**: Unreal Engine **5.7** (source build or Epic install with compile tools for the C++ project); web demo needs Python 3.x (stdlib only; OpenCV additionally for MP4 encoding); LLM planning works with any OpenAI-compatible endpoint (this project used DeepSeek) — the key is passed via the `WORLDGEN_LLM_API_KEY` environment variable, **never written to any file**.
- **日本語**：Unreal Engine **5.7**（C++ プロジェクトにはソースビルドまたはコンパイルツール付き Epic 版が必要）；Web デモには Python 3.x（標準ライブラリのみ、MP4 エンコードには OpenCV が追加で必要）；LLM 計画は OpenAI 互換エンドポイント全般で動作（本プロジェクトは DeepSeek を使用）。キーは `WORLDGEN_LLM_API_KEY` 環境変数で渡し、**ファイルには書き込みません**。

## 许可 / License / ライセンス

- **中文**：仓库代码为项目自有；场景使用的 Hunyuan3D 生成资产遵循 `tencent-hunyuan-community` 许可（来源与参数记录见 `Data/Assets/asset_manifest.json`），资产文件本身不随仓库分发。
- **English**: Repository code is the project's own; the Hunyuan3D-generated assets used in scenes follow the `tencent-hunyuan-community` license (provenance and parameters recorded in `Data/Assets/asset_manifest.json`); the asset files themselves are not distributed with this repo.
- **日本語**：リポジトリのコードは本プロジェクト独自のものです。シーンで使用する Hunyuan3D 生成アセットは `tencent-hunyuan-community` ライセンスに従います（出所とパラメータは `Data/Assets/asset_manifest.json` に記録）。アセットファイル自体は本リポジトリでは配布しません。
