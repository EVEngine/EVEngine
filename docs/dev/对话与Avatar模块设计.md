# 对话（Dialogue）与 Avatar 分层渲染模块设计

日期：2026-08-09  
状态：已实现（首版，`src/modules/avatar/`、`src/modules/dialogue/`）

## 目标

补齐引擎 Readme 中列出的两项高级能力：

1. **对话框及脚本** — 视觉小说 / Galgame 风格的对话、神态、动作与选项。
2. **Avatar 分层渲染** — 人物模型的 2D/3D 分层表现，供对话舞台与其它系统复用。

**刻意不做的事：** 不发明第二套脚本 DSL。对话流程继续写在 **Squirrel** 里（函数 / generator / 状态机均可），C++ 只提供状态机与渲染后端。

## 设计原则

与 [模块设计.md](./模块设计.md)、[RPG系统设计.md](./RPG系统设计.md) 对齐：

- Module facade + 明确类型；API **不重载**；枚举用 **string**。
- 逻辑与呈现分离：`Dialogue` 管台词 / 选项 / 舞台槽位；文本框 UI 由脚本用 `eve.UI` 自己搭。
- Avatar 可独立使用，不依赖 Dialogue；Dialogue 通过 `bindAvatar` 可选挂接。
- Live2D Cubism SDK、完整 VRM 运行时为可选后端；首版提供统一接口 + Image 层实现 + Live2D/VRoid 可插拔骨架。

## 模块一览

| 模块 | 脚本入口 | 职责 |
|------|----------|------|
| `avatar` | `avatar <- eve.Avatar()` | 分层人物：`image` / `live2d` / `vroid` |
| `dialogue` | `dlg <- eve.Dialogue()` | 角色注册、台词打字机、选项、舞台槽位、与 Avatar 同步 |

## Avatar

### 统一外观 API（三种 kind 共用）

```text
Avatar
  getKind() -> "image" | "live2d" | "vroid"
  setPosition / getX / getY
  setScale / getScaleX / getScaleY
  setVisible / isVisible
  setLayer (int, 参与 2D 深度排序)
  setExpression(name)
  setMotion(name)          // 动作 / 动画剪辑名
  setParameter(name, value) / getParameter(name)
  update(dt)
  release()
```

工厂（无重载）：

- `newImageAvatar()` — **基础实现**：纯图片层叠加。
- `newLive2DAvatar()` — Live2D 后端（默认 stub；可用 C++ `registerLive2DBackend` 注入）。
- `newVroidAvatar()` — VRoid / glTF 路径（经 `Model3D` 解码 + `Renderable3D` 占位）。

### ImageAvatar（基础层）

把角色拆成命名图层，每层一张纹理 + zIndex + 显隐 + 偏移 + 着色：

```text
addLayer(name, texture, zIndex)
setLayerTexture / setLayerVisible / setLayerOffset / setLayerColor / setLayerZ
defineExpression(name, spec)
  // spec 例："eyes=happy;mouth=smile;blush=1"
  // 规则：name=value
  //   value 为 "0"/"1"/"true"/"false" → 显隐
  //   其它 → 保留供扩展（首版当可见=true 的标记名，纹理需事先 setLayerTexture）
applyExpression / setExpression
```

渲染：每层维护一颗 `Renderable2D`，`Avatar::update` / 模块 `render` 前 `sync` 到 ECS，走现有 `RenderSystem` 批次。

### Live2DAvatar

- `loadModel(path)`：若未注册后端则返回 `false`，`getBackendName()` → `"none"`。
- 参数 / 表情 / 动作转发到 `ILive2DBackend`。
- 插件或带 Cubism 的构建可 `Avatar::registerLive2DBackend(factory)` 注入真实实现，无需改脚本 API。

### VroidAvatar

- `loadModelFromData(ModelData*)` / `loadModelPath` 标记资源路径（解码走 `eve.Model3D`）。
- `setMesh` / `setTexture` 接到 `Renderable3D`；表情名映射为 float 参数（morph 权重），供后续蒙皮/形态键管线消费。
- `setPosition3D` / `setRotation3D` / `setScale3D` 控制 3D 变换。

## Dialogue

### 状态机

```text
Idle → (say/narrate) → Typing → WaitingAdvance → Idle
                                         ↘ (presentChoices) → WaitingChoice → Idle
```

脚本每帧：

1. `dlg.update(dt)` — 推进打字机。
2. 输入：WaitingAdvance 时 `advance()`；WaitingChoice 时 `selectChoice(i)`。
3. 用 `getVisibleText()` / `getSpeakerName()` / `getChoice*` 刷新 UI。
4. `dlg.syncStage(width, height)` — 按槽位摆 Avatar。

### 角色与舞台

```text
registerCharacter(id, displayName)
bindAvatar(id, Avatar*)
show(id, slot) / hide(id)     // slot: "left"|"center"|"right" 或自定义名
setSlotX(slot, xNorm)         // 0..1，相对舞台宽度
setExpression / setMotion     // 转发到已绑定 Avatar
```

### 台词与选项

```text
say(speakerId, text) / narrate(text)
setTypeSpeed(charsPerSecond)  // <=0 表示瞬间显示
skipTyping() / isTyping() / isWaitingAdvance() / advance()
clearChoices() / addChoice(id, label) / presentChoices()
isWaitingChoice() / selectChoice(index) / getSelectedChoiceId()
```

## 脚本写法（继续用 Squirrel）

推荐 **generator + yield**，语义像 VN 脚本，语法仍是 Squirrel：

```squirrel
function scene_intro() {
    dlg.show("alice", "left")
    dlg.setExpression("alice", "happy")
    dlg.say("alice", "你好！")
    yield "wait"
    dlg.clearChoices()
    dlg.addChoice("yes", "开始")
    dlg.addChoice("no", "离开")
    dlg.presentChoices()
    yield "choice"
    if (dlg.getSelectedChoiceId() == "yes") {
        dlg.say("alice", "出发吧。")
        yield "wait"
    }
}
```

`examples/dialogue/` 内有 runner：在 WaitingAdvance / WaitingChoice 解除后 `resume` generator。也可不用 generator，改成显式状态机调用同一套 C++ API。

## 补充：Morph / Live2D 插件 / 口型 / Tween

### Mesh morph

- `Mesh::initMorphBase` / `addMorphTarget` / `addMorphTargetAbsolute` / `setMorphWeight`
- `Graphics::newMeshFromAssimp` 自动收录 Assimp `aiAnimMesh`
- `Graphics::bakeMeshMorph` 将权重混合后的顶点写回 host-visible VBO
- Vroid Avatar：`setMesh` / `setParameter` / `sync` → `bakeMorphs()`

### Live2D 后端

- 内置 `NullLive2DBackend`（`"null"`），保证无 Cubism 也能跑脚本
- `Avatar::registerLive2DBackend(factory)` 供插件替换（见 `examples/live2d-backend-plugin`）

### Dialogue 口型

- `setLipSyncEnabled` / `setLipSyncParameter` / `setLipSyncAmplitude`
- 打字阶段用正弦包络驱动 speaker avatar 参数；Image 层同名图层改 alpha

### Tween

- `AvatarInstance::bindTween(tween)`：每帧读取 `x`/`y`/`sx`/`sy`（及 3D/`参数` 同名轨）

## 文件布局

```text
src/modules/avatar/
  Avatar.h / Avatar.cpp
  AvatarInstance.h / .cpp
  Live2DNullBackend.h
src/modules/dialogue/
  Dialogue.h / Dialogue.cpp
src/modules/graphics/Mesh.h / Mesh.cpp   // morph CPU 数据
examples/dialogue/
examples/live2d-backend-plugin/
test/avatar.cpp
test/dialogue.cpp
```

## 验收清单

- [x] ImageAvatar 多层叠加、表情 spec、Renderable2D 同步
- [x] Live2D / VRoid 工厂与参数 API；内置 `NullLive2DBackend`（`"null"`）
- [x] Dialogue 打字机、advance、选项、槽位、bindAvatar
- [x] 脚本仍为 Squirrel（示例用 generator）
- [x] C++ 单测（不依赖 GPU 窗口）
- [x] Live2D 插件后端接入路径（`Avatar::registerLive2DBackend` + `examples/live2d-backend-plugin`）
- [x] Mesh morph 目标（Assimp `aiAnimMesh` → CPU bake → `Graphics::bakeMeshMorph`）
- [x] VRoid / Avatar 表情权重写入 Mesh morph 并在 `sync` 时 bake
- [x] Avatar `bindTween`（x/y/sx/sy + 同名参数）
- [x] Dialogue 口型：`setLipSync*` 在打字阶段驱动 speaker 参数 / Image 图层 alpha
- [ ] 正式 Cubism Core 渲染实现（需自带 SDK，放在用户插件中）
- [ ] 音频波形口型（当前为打字机正弦包络，不依赖 audio）

## 程序化对话 v1：内容模型 / 变量 / 条件 / 台词池（2026-08-18）

在保持「剧情仍由 Squirrel 驱动」的前提下，为程序化生成补齐数据与运行时。

### 变量（双区）
- `setVar(name, value, scope)`：value 支持 int / float / bool / string；scope 为 `"global" | "scene"`。
- `getVarType / getVarInt / getVarFloat / getVarBool / getVarString / hasVar / clearVar / clearVars(scope)`（scope 也支持 `"all"`）。
- scene 区在 `Dialogue::update` 内感知 `eve.Scene` 当前选中 host 名，切换时自动清空；global 区不动。

### 条件
- 结构化表：`{ var, op, value }`（op：eq/ne/gt/ge/lt/le/has/missing）、`{ all=[...] }` / `{ any=[...] }` / `{ not=... }`、`{ script="name" }`。
- `registerCondition(name, fn)` 注册脚本谓词，`fn(ctx)` 返回 bool，ctx = `{ vars, params, lineId }`。
- `evalCondition(table)` 可在脚本里直接复用。

### 台词池
- `loadPoolsFromTable(table)`：根表 `{ pools = { <poolId> = { noRepeat = N, lines = [ ... ] } } }`；追加注册，同名池整体替换。
- 行字段：`id`（缺省 `poolId.序号`）、`speaker`（空=旁白）、`text`（字面量，支持 `{var}` 替换）或 `i18n`（翻译键，参数=变量+pick 参数）、`weight`（缺省 1，≤0 不参与随机）、`when`、`meta`（字符串表；`expression` / `motion` 播放时自动应用到绑定 Avatar，其余字段经 `getCurrentLineMeta` 暴露）、`tags`。
- `pickLine(poolId, params)`：when 过滤 + 加权随机 + 每池最近 N 条去重（`noRepeat` 缺省 3，无候选时回退全池）。
- `playLine(lineId, params)` / `playPool(poolId, params)`：解析文本 → `say/narrate` → 应用 meta。
- `setRandomSeed(seed)` / `getRandomSeed()`：选择器内部 xorshift32，可复现。
- `getLastPoolsError()` 返回首次错误；`clearPools / getPoolCount / getPoolId / hasPool` 用于池管理。

### .dnut 内容格式（增强 nut）
- C++ 解析器 `src/modules/dialogue/DnutParser.cpp`：`dlg.loadPoolsFromDnut(source, path)` / `dlg.loadPoolsFromDnutFile(path)` 直接解析为池表并注册；错误经 `getLastPoolsError()` 读取（形如 `path:行: 信息`）。
- 语法：`pool` 块、`when <条件> { ... }` 分组、`speaker: "文本"` / `- "文本"`（旁白）、属性 `weight / i18n / id / meta(...) / tags=[...]`（属性与台词同行）。
- 条件语法糖 `mood == "happy" && hour >= 18` 编译为结构化 when 表；不引入第二套运行时。
- 示例：`examples/dialogue/pools.dnut`；热重载走 `eve_asset_reload`（.dnut 变更时重新 runFile）。
