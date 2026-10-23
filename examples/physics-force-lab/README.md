# 物理力模型实验室

运行：`make run/linux-debug GAME=examples/physics-force-lab`。

四个视图区依次是：左上 2D 吸引/斥力场（每四秒切换），右上 2D 排水浮力，
左下 3D 机翼升阻力投影，右下 3D 轮胎驱动力。固定步长 1/120 秒，模型统一使用 SI。

浮力示例使用简化浸没比例；轮胎示例由调用者给定法向载荷，没有模拟悬架或路面检测。
机翼是预设姿态的单翼力模型，不含完整飞机操纵系统。示例到边界重置，方便反复观察。
代码通过 Result 检查后才施力；轮速由 `TireInput.angularVelocity` 单一保存。
完整参数与限制见 [物理模块文档](../../docs/usr/modules/physics.md#场效果空气动力学和轮胎)。
