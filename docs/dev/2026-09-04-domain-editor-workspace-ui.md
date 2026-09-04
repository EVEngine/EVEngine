# 领域编辑器 Workspace UI 契约

日期：2026-09-04  
状态：已冻结产品面；实现按此倒推，禁止先扩 automation 再补界面  
存放：`docs/dev/`

相关：[`编辑器模块设计.md`](编辑器模块设计.md)、[`2026-08-29-editing-editor-layering.md`](2026-08-29-editing-editor-layering.md)、[`editor-module-gap-analysis.md`](editor-module-gap-analysis.md)、[`docs/usr/modules/editor.md`](../usr/modules/editor.md)

## 1. 决策

EVEngine 不交付固定 Editor App。每个领域编辑器是一套 **UI 无关的 Workspace 组合**：C++ 拥有文档、selection、事务、命中测试和预览数据；项目用 `ui` / HUD / MCP host 按 descriptor 画面板。

五个领域（Action / Animation Clip / Audio / Avatar / Biome）共用同一壳，但 **capability、selection type、预览契约不同**。`*EditorModule` 的 command / `target-create` 不是产品；它们只能覆盖本文件已经规定的用户交互。没有对应面板的操作不得先注册 automation 命令。

参考完成度：`ActionTimelineEditor::configureWorkspace` + [`examples/combat-action-editor`](../../examples/combat-action-editor)。其余四个必须达到同等「打开就能看见并改对」的程度，才算该领域编辑器存在。

## 2. 共用壳

每个领域 Workspace：

| 区域 | 角色 | 始终可见的状态 |
|------|------|----------------|
| left | 资源或结构列表 | 当前文档/条目稳定 ID |
| center | 预览 | 绑定文档 `revision`；过期请求丢弃 |
| right | Inspector | 由当前 selection 的 `PropertySchema` 驱动 |
| bottom | 主编辑面 | 时间轴、波形或规则栈 |

所有面板共享同一条 selection/focus channel。头部或状态条至少显示：selection 摘要、revision、`canUndo`/`canRedo`、最近一次拒绝诊断。失败不得静默；预览不得展示过期 generation。

C++ 只安装 descriptor（`id` / `title` / `region` / `order` / `capability` / `context`），不创建窗口、不提交 GPU。`configureWorkspace` 不得保留传入的 `EditorWorkspace` 指针（与 Action 相同）。

Presenter 用 `capability` 选择画法，用 `context` 区分同一 capability 下的槽位（例如 `list` / `preview` / `inspector` / `timeline`）。

## 3. Action（已实现，作为模板）

**用户任务：** 编一条可打的技能时间轴，并在角色上看到同一时刻的动画、命中窗和通知。

| Panel id | Title | Region | Capability | Context |
|----------|-------|--------|------------|---------|
| `action.assets` | Actions | left | `action.timeline` | `list` |
| `action.preview` | Action Preview | center | `action.timeline` | `preview` |
| `action.inspector` | Action Inspector | right | `action.timeline` | `inspector` |
| `action.timeline` | Action Timeline | bottom | `action.timeline` | `timeline` |

现有 `configureWorkspace` 把四个面板的 capability 都写成 `action.timeline`、context 都写成 `timeline`。后续实现应对齐上表的 context；行为保持兼容。

**Selection：** `action.timeline` 文档；条目为 notify / state（`LogicalId`）。

**预览：** `ActionTimelineEditor` 的确定性 cursor（`previewTime` / `play` / `pause` / `update`）；3D 视口把同一时间交给 animation player。指针 Down…Up 合成一个撤销事务。

**v1 完成标准：** `examples/combat-action-editor` 已满足。新命令只能映射已有交互（seek、拖条目、改 Inspector、undo）。

## 4. Animation Clip

**用户任务：** 对着骨骼改 clip：时长/循环、骨骼轨道关键帧、事件标记、per-bone mask，并立刻看到姿态。

| Panel id | Title | Region | Capability | Context |
|----------|-------|--------|------------|---------|
| `animation.skeleton` | Skeleton | left | `animation.clip` | `list` |
| `animation.preview` | Pose Preview | center | `animation.clip` | `preview` |
| `animation.inspector` | Clip Inspector | right | `animation.clip` | `inspector` |
| `animation.timeline` | Dope / Curve | bottom | `animation.clip` | `timeline` |

**Selection type**

| type | Inspector schema |
|------|------------------|
| `animation.clip` | duration / sampleRate / loop |
| `animation.bone` | 当前骨 mask weight；选中后时间轴只强调该轨道 |
| `animation.track` | 轨道元数据 |
| `animation.key` | 关键帧时间 + TRS |
| `animation.event` | time / name / payload |

**预览契约（已有类型，不得另造镜像）**

- Scrub 输出 `animation_editing::AnimationClipPreview`：`documentRevision`、`time`、每骨局部 TRS、`maskWeight`、diagnostics。
- 视口骨骼线用 `animation_editing::SkeletonOverlayBone`（选中、mask 强度着色）。请求必须带 clip revision；不匹配则 `Conflict`，画面停在上一成功 generation。

**主交互（v1）**

- 时间轴 scrub ↔ 预览姿态。
- 点骨骼 ↔ 选轨道。
- 插入/删除/拖动关键帧；插入/删除事件标记。
- 改 mask 时该骨在 overlay 上立刻变淡。

**v1 不做：** retarget 映射编辑（`AnimationRetargetPreview` 可只读显示未匹配骨，不当作主路径）。

**由此倒推的命令（有交互才有命令）**

- `animation.clip.settings.set.v1`
- `animation.clip.track.set.v1` / `animation.clip.track.delete.v1`
- `animation.clip.event.set.v1` / `animation.clip.event.delete.v1`
- `animation.clip.mask.set.v1`

## 5. Audio

**用户任务：** 看见波形、听见试听，再改音量/循环/空间/路由；导入质量问题要看得见。

| Panel id | Title | Region | Capability | Context |
|----------|-------|--------|------------|---------|
| `audio.sources` | Sources | left | `audio.source` | `list` |
| `audio.waveform` | Waveform | center | `audio.source` | `preview` |
| `audio.inspector` | Source Inspector | right | `audio.source` | `inspector` |
| `audio.transport` | Transport | bottom | `audio.source` | `transport` |

第一版 **center 是波形而不是 3D 衰减 gizmo**。空间矢量仍在 Inspector。Mixer bus 树与效果链是 v2 工作区（`audio.mixer` / `audio.effects`），v1 只通过 Inspector 的 `mixer.bus` ObjectRef 选择已有总线。

**Selection type**

| type | Inspector |
|------|-----------|
| `audio.source` | `AudioSourceTarget::sourceSchema()`（clip、mode、playback、spatial、bus） |

**预览契约**

- 波形：`AudioWaveformRequest` → `AudioWaveformResult`（按像素桶的 min/max/RMS、loop 标记、`sourceRevision`）。
- 试听：`AudioAuditionTransport` → `AudioTransportSnapshot`（state、position、duration、loop 区间）。文档 revision 变化必须 `stop` 并 `unbind`，禁止继续播放旧 PCM。
- 诊断（可折叠，挂在 Inspector 或 waveform 角标）：`AudioImportDiagnostics` 的 peak/RMS/DC/clipping/silence。

**主交互（v1）**

- 点波形 seek；Play/Pause/Stop。
- 拖 loop-start/end，标记与试听循环一起变。
- 改 `play.volume` / `play.pitch` 时正在播放的 source 立即反映（经 publishing sink；失败保留旧 generation）。

**v1 不做：** 效果链排序 UI、live mixer 表。工厂可以创建 `audio-mixer` / `audio-effects` target，但没有本表面板前不得宣称音频编辑器已支持它们。

**由此倒推的命令**

- v1：`audio.source.property.set.v1`（覆盖 schema 全路径即可，不必为每个字段单独命令）
- v2：mixer create/delete、effect set/delete/reorder

## 6. Avatar

**用户任务：** 对着合成预览叠图层、滑参数看表情；删除仍被表达式引用的对象必须被拒绝并保持旧画面。

| Panel id | Title | Region | Capability | Context |
|----------|-------|--------|------------|---------|
| `avatar.layers` | Layers | left | `avatar.document` | `list` |
| `avatar.preview` | Avatar Preview | center | `avatar.document` | `preview` |
| `avatar.inspector` | Avatar Inspector | right | `avatar.document` | `inspector` |
| `avatar.parameters` | Parameters | bottom | `avatar.document` | `parameters` |

Expressions 在 v1 必须有入口：参数面板旁的折叠列表或 Inspector 第二页，capability 仍为 `avatar.document`，context `expressions`。没有表达式入口就无法展示「引用拒绝」。

**Selection type**

| type | Inspector schema（已有） |
|------|--------------------------|
| `avatar.layer` | texture / z / visible / offset / size / color |
| `avatar.parameter` | default / minimum / maximum / value |
| `avatar.expression` | channel 绑定（v1 只读列表 + 创建/删除；细调可随后） |

**预览契约**

- `AvatarDocumentRuntime` 候选构建完整 `AvatarInstance`；任一层纹理或 channel 失败则保留旧 generation，预览继续显示上一成功结果，并给出 diagnostics。
- 拖 `parameter.value` 更新预览；写入文档走事务。若产品要把「试滑」与「提交」分开，必须在 UI 上区分 scrub vs commit，不得 silently 丢掉 revision。

**主交互（v1）**

- 图层列表：可见开关、排序（z）、选中高亮。
- 创建图层（id/name/texture）；删除图层（引用检查）。
- 创建参数；拖 current 看脸；删除参数（引用检查）。
- 源 kind/asset 在 Inspector 文档级字段（非 selection item）编辑。

**由此倒推的命令**

- `avatar.property.set.v1`
- `avatar.layer.create.v1` / `avatar.layer.delete.v1`
- `avatar.parameter.create.v1` / `avatar.parameter.delete.v1`
- `avatar.expression.create.v1` / `avatar.expression.delete.v1`（有表达式列表就要有）
- 文档级 source：走 property 或单独 `avatar.source.set.v1`，与 Inspector 同源，禁止只存在于 `target-create` seed

## 7. Biome

**用户任务：** 配层（空间域 × 密度 × 优先级）和加权模型，在世界上看见固定 seed 的铺点；排除区生效。

| Panel id | Title | Region | Capability | Context |
|----------|-------|--------|------------|---------|
| `biome.layers` | Layers | left | `biome.rules` | `list` |
| `biome.preview` | World Preview | center | `biome.rules` | `preview` |
| `biome.inspector` | Biome Inspector | right | `biome.rules` | `inspector` |
| `biome.assets` | Layer Assets | bottom | `biome.rules` | `assets` |

Exclusions 在 v1 作为 Inspector 文档级列表或 layers 面板底部的 `context = exclusions` 槽，不单开第五个 dock。没有排除 UI 就不要暴露 `makeSetExclusions` automation。

**Selection type**

| type | Inspector schema（已有） |
|------|--------------------------|
| `biome.layer` | spatial / priority / density |
| `biome.asset` | ref / weight / scale / randomYaw |

**预览契约**

- `BiomeDocumentRuntime::preview(domain, spacing, seed, jitter)` → owning `procgen::PointSet`。
- 请求必须携带文档 revision；过期返回 `Conflict`，视口保留上一 PointSet。
- seed 由 Workspace/工具注入，同一文档同一 seed 必须得到同一点集（与 runtime 一致）。

**主交互（v1）**

- 层栈排序（priority）；改 density / spatial 后预览重建。
- 当前层的加权资产列表：增删、改 weight/scale/yaw。
- 排除空间 AssetRef 增删后点从排除区消失。

**由此倒推的命令**

- `biome.layer.create.v1` / `biome.layer.delete.v1`
- `biome.asset.create.v1` / `biome.asset.delete.v1`
- `biome.property.set.v1`
- `biome.exclusions.set.v1`（有排除槽才注册）

## 8. 实现顺序与验收

领域文档（`*_editing` Target/Runtime）已存在的，**先做 Workspace `configureWorkspace` + 可运行 example presenter**，再补与面板同构的命令和测试。

建议顺序：

1. **Audio** — 波形 + transport 类型已齐，不依赖 3D 骨骼；最快证明「预览即编辑」。Example：`examples/audio-source-editor`（对标 combat-action-editor）。
2. **Animation Clip** — 复用 Action 的 3D 视口模式 + `AnimationClipPreview` / skeleton overlay。Example：`examples/animation-clip-editor`。
3. **Avatar** — 需要合成预览后端；图层/参数 UI 相对直接。
4. **Biome** — 需要空间域与 PointSet 视口；可挂在已有地形/scene 预览上。

Audio v1（2026-09-04）：`AudioSourceEditor` + `examples/audio-source-editor` 已按上表安装四面板、波形、clock/live 试听与属性事务。

Animation Clip v1（2026-09-04）：`AnimationClipEditor` + `examples/animation-clip-editor` 已按上表安装四面板、两骨预览 clip、`AnimationClipPreview` / skeleton overlay、dope-sheet 与 mask/settings 事务。v1 视口是 overlay 原语的 2D 投影，不绑定 KayKit 网格。

Avatar v1（2026-09-04）：`AvatarDocumentEditor` + `examples/avatar-document-editor` 已安装 Layers / Preview / Inspector / Parameters / Expressions（capability `avatar.document`）。CPU 合成预览来自图层矩形；引用中的参数删除被拒绝且 preview revision 不变。Live `AvatarInstance` 发布仍需 texture resolver，本 example 不挂接。

Biome v1（2026-09-04）：`BiomeRulesEditor` + `examples/biome-rules-editor` 已安装四面板（capability `biome.rules`），固定 seed 的 PointSet 预览，Inspector 排除区，密度拒绝时保留上一 generation。

每个领域的完成门禁（与 gap analysis 对齐，且加上 UI）：

1. `configureWorkspace` 安装上表四个面板（id/capability/context 稳定）。
2. Example 能选、能改、能看见/听见预览、能 undo，拒绝路径有诊断。
3. 命令表 ⊆ 本文件交互；测试覆盖 apply / reject / undo / stale preview / close。
4. 无对应面板的 target 类型不得出现在用户文档或脚本教程里。

## 9. 明确非目标

- 不把这五个编辑器做成引擎内置窗口或 ImGui 固定布局。
- 不在 `editor` 核心枚举领域 panel 类型。
- 不把 MCP `target-create` 当作编辑器交付。
- 不为「命令覆盖率」补用户看不见的结构操作。
