# Flight & Vehicle Controllers

可复用的两个游戏侧控制器，以及可切换演示场景：

- `FlyingController` — 天空飞行：`Body3D` + 推力/力矩 + `sampleWingForce` 升阻力
- `VehicleController` — 地面汽车：封装 `eve.Vehicle()` 的 `wheel` mobility + `attachPhysics2D`

两者都把键盘输入映射成油门/转向（或俯仰/滚转/偏航），固定 60 Hz 步进；物理权威仍在引擎模块里。

## 运行

```bash
make run/linux-debug GAME=examples/flight-vehicle-controllers
# Windows: make run/win32-debug GAME=examples/flight-vehicle-controllers
```

需要桌面配置里的 `gfx`、`physics`、`vehicle`、`font`。

## 操作

| 按键 | 作用 |
|---|---|
| Tab | 在汽车 / 飞机之间切换主动控制器 |
| R | 重置当前主动载具 |
| **汽车模式** | |
| W / S / 上 / 下 | 油门 / 倒车 |
| A / D / 左 / 右 | 转向 |
| Space | 刹车 |
| **飞机模式** | |
| W / S | 增大 / 减小油门（保持） |
| 上 / 下 | 俯仰 |
| A / D / 左 / 右 | 滚转 |
| Q / E | 偏航 |

未接管的一方继续仿真：停车时汽车制动；开车时飞机保持缓速巡航。

## 模块组合

- `vehicle_controller.nut`：注册 wheel 定义、`setInput` → `vehicle.update` → `world.update`（与 neon-racing 相同顺序）；平面物理 `(x,y)` 映射渲染 `(x,z)`。
- `flying_controller.nut`：在刚体局部前向施力、按姿态施加控制力矩，并用机体朝向更新 `WingModel.chord/normal` 后采样升阻力；世界重力由 `World3D` 提供。
- `main.nut`：程序化赛道/跑道场景、双载具可视化、跟随镜头与 HUD。

## 文件

- `flying_controller.nut` / `vehicle_controller.nut` — 可 `dofile` 复用的控制器类
- `main.nut` / `config.nut` — 演示入口
- `assets/DejaVuSans-Bold.ttf` — HUD 字体（见 `assets/FONT-NOTICE.txt`）

运行检查：

```bash
python3 scripts/check_examples.py
MIN_RUN_SECONDS=2 RUN_SECONDS=8 bash scripts/smoke_examples.sh flight-vehicle-controllers
```

## 2026-11-13 画面复查

修复每帧同步覆盖部件偏移的问题，按车体/飞机姿态变换局部部件；补齐车轮、玻璃、灯组、机翼、尾翼与座舱，改善追踪镜头、连续道路和 HUD 对比度。

[前后截图与运行验证](../../docs/images/examples/review-2026-11-13/nine-improved/README.md)。

### 圆形车轮修正

四轮改为圆形轮胎、轮毂和五辐条；轮辐随实际车速转动。按 C 切换近景。[最新实帧与验证](../../docs/images/examples/review-2026-11-13/round-wheels/README.md)。
