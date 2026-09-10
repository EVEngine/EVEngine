# 可组合场景编辑示例

运行：`make run/win32-debug GAME=examples/scene-editor`（其他平台替换构建配置）。
需要 scene、scene_editing、scene_editor，以及 graphics、ui、editor 及其依赖。

这个例子提供层级选择、世界拾取、创建/删除叶节点、名称/父级/TRS 编辑、移动/旋转/缩放 Gizmo、
撤销重做和 JSON 保存恢复。右键旋转相机；旋转数值使用弧度。
Gizmo 拖动是预览，释放时提交一次事务。Resync 从运行时重新创建会话并清空历史。

移动用箭头和 XY/YZ/XZ 平面手柄；缩放用轴端方块、双轴平面与中心方块。
中心缩放保留原有长宽高比例；双轴移动吸附不会改变锁定轴。
`scene_visuals.nut` 提供受光网格、方向光、环境光和接收阴影的地面。
显示网格按世界空间顶点更新，保留父级非均匀缩放产生的形变；网格和显示实体复用。

所有面板、选择、输入、相机、图元绘制和文件路径都在 `scene_components.nut` 中，开发者可以替换。
权威修改统一使用以下组件，不依赖本示例 UI：

```squirrel
local opened = eve.SceneEditorModule().createLiveSession("my-scene", "my-host");
if (!opened.ok) throw opened.status.summary;
local session = opened.value;
local created = session.execute("scene.object.create.v1",
    {object="crate", name="Crate", parent="", position=[0.0,0.0,0.0]});
if (!created.ok) throw created.status.summary;
local undone = session.undo();
if (!undone.ok) throw undone.status.summary;
```

不绑定运行时的文档编辑使用 `createSession(id)`；C++ 使用 `SceneEditorSession`，
也可以直接组合底层 Scene Target、CommandRegistry 和 Coordinator。

`scene.object.update.v1` 将 name、parent、position、rotation、scale 一起原子提交；
还可以分别使用 create/delete/rename/reparent 和 `scene.transform.set.v1`。
存档使用 `saveJson()` / `restoreJson(text)`，只包含层级、名称和 TRS，资源与组件由各领域负责。
示例写入用户保存目录的 `scene-components.json`，不会覆盖项目源文件。

运行时被其他系统改动后，旧会话拒绝冲突提交；由宿主选择 Resync 时机。
已有 Link/行为会在编辑中保留；本组件拒绝删除仍带关联的节点。
会话在 owner thread 使用；Host 可先销毁，但 ECS table 必须比会话长寿。

参见 `../scene-builder-game`：同一组件只开放游戏内需要的命令和 UI。

![实际运行截图](scene-editor.png)

![多轴缩放](scene-editor-scale.png)
