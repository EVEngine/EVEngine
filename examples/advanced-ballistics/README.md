# 高级弹道运动实验室

基于 `weapon::CombatCarrierRuntime` 的 12 格实时演示。每条轨迹都由原生运行时计算，脚本仅提交配方、目标位置和固定步长，并绘制 `pool.states()` 的状态副本。

```sh
make run/win32-debug GAME=examples/advanced-ballistics
# Linux/macOS: 将 win32 换成 linux/macosx
```

需要包含本次扩展的引擎构建；旧 SDK 不认识新增算子。字体随示例附带（DejaVu Sans，许可见 `assets/FONT-NOTICE.txt`），无网络依赖；启动后自动轮流发射，无需操作。

展示：带符号加速度、重力与顶点事件、有限幅正弦/螺旋、限转速追踪、目标预瞄、环绕目标、固定轴转弯、种子随机游走、周期变速（含停止后重启）、扫掠接触反弹、路程与周期触发器。

白点为载体，彩点为轨迹，红点为追踪目标，橙色闪光为触发事件。每格显示存活数与累计事件数。模拟固定为 60 Hz，保留渲染帧剩余时间，每渲染帧最多补算 4 步以保持调试器响应；慢帧积欠时间保留至后续帧，不丢弃模拟时间，调试模式可能慢于实时时钟。启动标记：`ADVANCED_BALLISTICS_READY cases=12 fixedHz=60`。

参数和配方位于 `main.nut` 的 `eve_init`。每格独立池，避免不同实验互相影响；正常发射时逐代变化的 handle 参与随机流，因此同一实验的不同投射物具有不同轨迹。精确回放需要相同初始池、发射顺序、目标样本和步长。

详细契约见 [weapon 模块手册](../../docs/usr/modules/weapon.md)。反弹现在会继续消费命中后的剩余时间，支持同一更新多次接触。示例以 60 Hz 外层更新、最大 1/120 秒求解段运行；RICOCHET 格在左右两个球形障碍之间反弹。示例默认不申请物理模块，以脚本内球体探针展示反弹。


### 自定义函数

`main.nut` 的 `customTrajectory(state, proposed, dt)` 和 `customSpeed(state, proposed, dt)`
可以直接修改。CUSTOM PATH / CUSTOM SPEED 两格通过 `updateWithMotion` 使用这些函数，
其余格继续使用内置算子。返回位置、速度或完整运动；返回 `null` 保留内置结果。
详见 `docs/usr/modules/weapon.md` 的“自己写函数计算弹道与速度”。


回归脚本可用当前构建运行 `eve run test/integration/projectile-behavior/script`。
成功打印 `CUSTOM_MOTION_SCRIPT_CONTRACT_PASS` 和 `CONTINUOUS_SOLVER_SCRIPT_CONTRACT_PASS`；
该专用测试会故意抛出一次 `EXPECTED_CUSTOM_MOTION_EXCEPTION` 验证回滚，不能当作无错误标记的示例 smoke。
