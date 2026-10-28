# ECS — 脚本 ECS 与 GPU 计算

演示用 Squirrel 定义 `eve.Component` / `eve.Entity` / `eve.System`：
`Moveable` 组合 `Position` + `Velocity`，`MoveSystem` 批量更新并弹跳。

## 运行

```bash
make ecs
# 等价于：make run/<platform>-debug GAME=examples/ecs
```

## GPU 变体

`gpu_main.nut` 用 compute shader（`eve.ShaderSystem`）取代 CPU `foreach` 移动实体：
把 `gpu_main.nut` 复制为 `main.nut` 再运行即可（需要 `glslc`，见仓库 Vulkan 环境要求）。
