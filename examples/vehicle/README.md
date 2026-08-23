# Vehicle Demo（通用载具系统示例）

一个 2D 顶视示例，演示 `eve.Vehicle()` + `eve.Weapon()` 的两种形态：

- **RTS 坦克**：`tank.rts` 用 `attackMove` 巡逻四个航点，炮塔自动瞄准目标。
- **FPS 吉普**：`car.fps` 的驾驶座由玩家控制（W/S 油门刹车、A/D 转向），
  炮手座自动跟踪坦克，按住空格开火。

运行：`make run/win32-debug GAME=examples/vehicle`（或对应平台）。

设计文档：[docs/dev/通用载具系统设计.md](../../docs/dev/通用载具系统设计.md)
