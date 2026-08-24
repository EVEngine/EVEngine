# Vehicle Demo（通用载具系统示例）

一个 2D 顶视示例，演示 `eve.Vehicle()` + `eve.Weapon()` 的完整框架链路：

- **RTS 坦克**（黄色）：左键点地 `moveTo`（到达即停），右键点敌车 `attack`
  （炮塔自动瞄准 + 射程内自动开火 + 自动装填），`S/H/T` 发出停止/待命/巡逻命令。
- **FPS 吉普**（蓝色，玩家）：`W/A/S/D` 驾驶，`E` 进出驾驶座；炮手座自动跟踪敌车，
  机枪自动射击；白色角标 = 玩家车。
- **敌车**（红色）：物理驱动 AI 绕场行驶，火炮自动瞄准玩家。
- **伤害管线**：按命中方位判定 front/side/rear 装甲倍率；被击毁后自动重生。
- **ECS 渲染**：`eve.view(eve.VehicleEntity)` 统一遍历，车体按航向旋转、
  炮塔按挂点 yaw 独立旋转，贴图缺失时回退色块，击毁后变灰。
- **HUD**：左上角 ImGui 窗口显示操作说明与事件日志。

运行：`make run/win32-debug GAME=examples/vehicle`（或对应平台）。

操作：

```
左键点地      = 坦克移动到该点（到达后停下）
右键点敌车    = 坦克攻击敌车
W / A / S / D = 驾驶蓝车（S 倒车）
Space         = 机枪
E             = 进出驾驶座
S             = 坦克停止
H             = 坦克待命
T             = 坦克开始/停止巡逻
```

## 素材来源

载具与武器贴图来自 Kenney（CC0 公共领域，可自由使用、修改、商用）：

- [Kenney — Top-down Tanks Remastered](https://kenney.nl/assets/top-down-tanks-remastered)
- [Kenney — Racing Pack](https://kenney.nl/assets/racing-pack)

设计文档：[docs/dev/通用载具系统设计.md](../../docs/dev/通用载具系统设计.md)
