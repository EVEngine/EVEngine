# Windworks 3D 综合物理实验室

真正的三维米制场景：透视相机、立体龙门架、地台、灯光，以及三组同时运行的原生物理对象。

- **悬索**：32 粒子 Rope3D、XPBD 距离/弯曲约束、两端固定、自碰撞和 World3D 碰撞；48 段立体线段从平滑采样位置绘制。
- **布帆**：18×15 Cloth3D 三角网格，顶边固定，空气速度、阻力/升力、自碰撞、刚体碰撞；不是屏幕空间网格。
- **滑轮**：两个不同密度的动态 Body3D，原生 PulleyCoupling3D 保持 `lA + lB = 6m`，允许松绳。配重受重力、风和地面接触影响。轮盘为固定导向轮的外观，本示例不模拟轮轴惯量或绳轮接触摩擦；配重间传力来自原生滑轮耦合。
- **共享阵风**：布帆采样三维空气速度，悬索和配重接受对应外力。空间流线仅显示风向，不是流体求解。

## 运行

```sh
make run/linux-debug GAME=examples/physics-workshop
# Windows: make run/win32-debug GAME=examples/physics-workshop
```

顶部面板：Wind 调节风速和反向风；Pause/resume 暂停/继续；Reset physics 重置；Orbit camera 开关相机环绕。暂停时仍可环绕检查空间关系。

若使用裁剪引擎，需要启用 `physics`、`physics_cloth`、`physics_rope`、`ui`、`font`；MCP 截图验证另需 `devtools`。`config.nut` 声明所需脚本模块，`main.nut` 拥有场景逻辑。无需外部美术资源。

## 时间与生命周期

统一以 1/120 秒推进刚体、悬索和布帆；单帧累计上限 0.05 秒，低帧率时减速。悬索和布料内部子步由原生求解器管理。时变风仅使用模拟时间，没有墙钟或随机数，不承诺跨平台位级一致。

全部逻辑在主线程运行。World3D 在整个场景运行期间持续存活，Rope3D/Cloth3D 的借用提供者不会在重置中失效。重置销毁并重建 owning coupling，原位恢复两刚体的位置、旋转与速度，并复位绳布粒子。世界、刚体、绳索、布料和绘制实体均复用，避免重置累积对象；引擎关闭时由模块依赖顺序释放提供者与消费者。布帆与悬索通过刚体世界发生碰撞，没有跨求解器的绳布双向缝合约束。未新增引擎公共 API、ECS 模型或持久化格式，无架构规则例外。

## 验证

```sh
python3 scripts/check_examples.py
# 使用仓库 AGENTS.md 的 Lavapipe / Xvfb / null audio 环境：
MIN_RUN_SECONDS=2 bash scripts/smoke_examples.sh physics-workshop
```

运行日志在完成 240 个固定子步后输出 `PHYSICS_WORKSHOP_3D_RUNNING`、布料/绳索粒子数和滑轮误差。截图必须使用引擎 `eve_screenshot`，不能使用 Xvfb 帧缓冲。
