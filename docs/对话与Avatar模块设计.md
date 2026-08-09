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

## 文件布局

```text
src/modules/avatar/
  Avatar.h / Avatar.cpp           // Module + Live2D backend registry
  AvatarInstance.h / .cpp         // 公共实例 + Image / Live2D / Vroid
src/modules/dialogue/
  Dialogue.h / Dialogue.cpp       // Module + 舞台 / 打字机 / 选项
docs/对话与Avatar模块设计.md
examples/dialogue/
test/avatar.cpp
test/dialogue.cpp
```

## 验收清单

- [x] ImageAvatar 多层叠加、表情 spec、Renderable2D 同步
- [x] Live2D / VRoid 工厂与参数 API；Live2D 默认可探测为无后端
- [x] Dialogue 打字机、advance、选项、槽位、bindAvatar
- [x] 脚本仍为 Squirrel（示例用 generator）
- [x] C++ 单测（不依赖 GPU 窗口）
- [ ] 正式接入 Cubism Core（插件后端）
- [ ] VRM 表情 morph 真写入 Mesh（依赖 graphics 形态键）
- [ ] 与 `animation` Tween / 口型驱动联动
