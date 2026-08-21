# Scene ECS — 场景节点与脚本 ECS 打通

演示场景树与脚本 ECS 的桥接：节点挂 `eve.SceneEntity` 行为（含数据组件槽）、
`eve.view` 批量查询、`scene.update(dt)` 统一驱动 update 与变换，reconcile 后绑定保留。

## 运行

```bash
make run/<platform>-debug GAME=examples/scene-ecs
```

场景中三个节点（player / enemy / prop）分别挂 `Bounce` / `Bounce` / `Spinner` 行为，
每帧由场景系统驱动，启动时打印 `eve.view` 的查询结果。
