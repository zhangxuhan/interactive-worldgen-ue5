# WorldGen — 自然语言生成可玩 UE5 场景的管线

**Abstract**: WorldGen turns a one-sentence natural-language room description into a
playable Unreal Engine 5 level: an LLM plans the layout as a strict JSON spec, C++
validators gate it, a deterministic C++-generated build plan is executed inside the
editor, and an automated playthrough (walk to key → pick up → unlock door → exit)
must pass before a scene is accepted.

> **这是代码展示版（portfolio showcase），不是可直接运行的完整项目。**
> 仓库只包含源代码与少量说明数据格式的 JSON 样例；**不包含** UE 二进制资产
> （静态网格/材质/关卡 umap）、Hunyuan3D 模型权重与生成产物、演示视频/截图。
> 克隆本仓库并编译后可以得到可构建的 UE 工程（骨架房间+占位图形），但
> **不能直接得到文档/视频里展示的完整可玩 Demo**。

## 管线架构

```
自然语言描述（中/英）
     │
     ▼  Tools/scene_planner.py          LLM 规划（DeepSeek / GLM，key 只走环境变量）
SceneSpec 0.1 JSON ◄─┐
     │               │  校验失败：带具体坐标的错误信息回喂模型重试（≤2 轮）
     ▼               │
UnrealEditor-Cmd -M1cCheckSpec          C++ 校验（FSceneSpecValidator 12 组规则
     │                                  + 摆位几何：出生点阻挡 / 门洞走道净空）
SceneSpec VALID
     │
     ▼  UnrealEditor-Cmd -M1bGenerate   C++ 生成搭建清单（FLevelPlanBuilder，确定性）
plan.json（每个 op 含坐标/尺寸/旋转/asset_id）
     │                                   确定性验收：同一 spec 跑两次 plan 逐字节 diff 必须为空
     ▼  UnrealEditor -ExecutePythonScript Tools/m1b_build_level.py
关卡 umap（墙体/地面/门/钥匙/家具）+ Tools/m2bs_polish_level.py（灯光标定/材质/演示标签）
     │
     ▼  Tools/m2b_check_umap.py         资产绑定核验（真实模型 vs 回退占位盒）
     ▼  Tools/m3a_previews.py           预览静帧 + 运镜帧（Tools/m3a_encode_clip.py 编码 MP4）
UnrealEditor-Cmd <level> -game -M1bE2E  自动试玩验收（真实输入注入，非传送）：
     │                                  捡起钥匙 → 开门 → 完全越过出口墙外 ≥34cm，5 项断言
     ▼
验收通过才算完成；失败输出具体卡点，不假报成功
```

### 运行时组件（Source/WorldGenRuntime）

| 组件 | 职责 |
| --- | --- |
| `SceneSpec` / `FSceneSpecValidator` | 数据契约 0.1（尺寸厘米、角度度）+ 12 组校验规则，错误带字段路径 |
| `LevelPlanBuilder` | 校验通过的 SceneSpec → 确定性 plan（墙体/门洞/家具/钥匙/灯光 op 序列） |
| `WorldGenCharacter` | 步行角色（WASD + E 交互） |
| `WorldGenDoor` / `WorldGenExitTrigger` | 上锁门（拾钥匙解锁）与出口判定触发区 |
| `WorldGenPickup` | 钥匙拾取（程序化金钥匙网格，E 键边沿触发 + 几何兜底） |
| `WorldGenGameMode` / `WorldGenGameState` | 关卡状态机与 E2E 自动试玩验收（`-M1bE2E`，逐帧输入注入） |

### 资产管线（Hunyuan3D，展示用途）

场景里的木桌使用 Hunyuan3D-2.1 形状生成（image-to-mesh）**预先生成**的资产
（70 万面，简单盒碰撞）。相关脚本只保留资产处理部分：

- `Tools/m2a_normalize.py` — 网格归一化（Y→Z-up、目标尺寸、底面 pivot）
- `Tools/m2a_import_mesh.py` — 导入 UE + 盒碰撞
- `Data/Assets/asset_manifest.json` — 资产绑定表：SceneSpec 物件的 `asset_id`
  查不到就回退占位盒并记录（`M2B FALLBACK`），绝不冒充成功

模型权重、推理环境与原始网格**不在本仓库**（体积与许可原因）。

### 本地 Web Demo（可选）

`Tools/m3a_web.py` + `Tools/web/`：单页三语（中/EN/日）界面，输入描述 → 七阶段进度 →
结果页（预览四视角 + 打开可玩关卡）。只绑定 `127.0.0.1`；单工作线程队列；
用户描述写文件传参、从不拼进命令行；API key 只存在于服务进程环境变量。

## 代码阅读路线

1. `samples/scene_spec.example.json` / `samples/plan.example.json` — 先看两个核心数据格式
2. `Source/WorldGenRuntime/Public/SceneSpec.h` — 数据契约
3. `Source/WorldGenRuntime/Private/SceneSpec.cpp` — 校验规则（什么布局算"可玩"）
4. `Source/WorldGenRuntime/Private/LevelPlanBuilder.cpp` — spec → plan 的确定性翻译
5. `Tools/m1b_build_level.py` — plan 在编辑器里的机械执行（无解释逻辑）
6. `Source/WorldGenRuntime/Private/WorldGenGameMode.cpp` — E2E 自动试玩验收
7. `Tools/scene_planner.py` — LLM 规划 + 校验反馈环
8. `Tools/m3a_web.py` — 把上述全部串成本地网页 Demo

## 环境要求

- Unreal Engine **5.7**（C++ 工程需要源码版或安装了编译工具的 Epic 版）
- Web Demo：Python 3.x（标准库即可，MP4 编码另需 OpenCV）
- LLM 规划：任何 OpenAI 兼容接口（本项目用 DeepSeek），key 通过
  `WORLDGEN_LLM_API_KEY` 环境变量传入，**不写入任何文件**

## 与内部版本（原文档/视频所示项目）的差异

- 排除了全部运行产物：`Content/`（umap/静态网格/材质资产）、`Data/Demo/`、
  `Data/Reports/`、模型权重与推理缓存、演示视频/截图/日志、诊断脚本与一次性验收脚本。
- UE-Python 脚本（`m1b_build_level.py` 等 7 个）原为绝对盘符路径，公开版改为
  `unreal.Paths.project_dir()` 相对推导；UE 安装路径默认值改为 PATH 上的
  `UnrealEditor-Cmd.exe`，并保留 `WORLDGEN_UE_EDITOR` 环境变量覆盖。
- **公开版仅做了语法级校验（py_compile）与路径残留扫描，未在本机重新端到端运行**；
  文档与视频所展示的运行结果来自内部版本。

## 许可说明

- 本仓库代码：项目自有。
- 场景中使用的 Hunyuan3D 生成资产遵循 `tencent-hunyuan-community` 许可
  （见 `Data/Assets/asset_manifest.json` 的来源与参数记录），资产文件本身不随仓库分发。
