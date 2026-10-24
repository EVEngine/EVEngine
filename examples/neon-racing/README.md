# NEON R / Circuit Run

把 NEON R OpenSCAD 概念跑车接入程序化公路和 `vehicle` 模组的 3D 绕圈赛车示例。

## 运行

在仓库根目录执行：

```bash
make run/linux-debug GAME=examples/neon-racing
# Windows: make run/win32-debug GAME=examples/neon-racing
```

需要 `gfx`、`procgen`、`model3d`、`vehicle`、`physics`、`font` 模块；完整桌面配置已包含。
裁剪配置可启用 `EVENGINE_MODULE_procgen`、`EVENGINE_MODULE_vehicle`、
`EVENGINE_MODULE_model3d`、`EVENGINE_MODULE_physics`、`EVENGINE_MODULE_font`。

## 操作

| 按键 | 操作 |
|---|---|
| W / 上 | 加速 |
| S / 下 | 倒车 |
| A、D / 左、右 | 转向 |
| Space | 刹车 |
| E | 回到上一个检查点，保留本圈计时 |
| R | 重新发车，保留本次运行最佳圈速 |
| C | 跟车 / 全景镜头 |
| P | 开关驾驶辅助，自动绕圈演示 |

启动有 3 秒倒计时。沿青色路标依次通过 12 个检查点，跨过起终点后记录圈速。
逆行和跳过检查点不会完成一圈。`E` 为手动脱困，恢复过程不免除计时。

## 模块组合

- `procgen.buildMesh("mesh.roadNetwork", params)` 生成 `roundabout` 路网，按材质组上传，整体放大 3 倍。
  主环半径约 48 米；四条引道保留在景观中，连续护栏将比赛限制在主环。
- `vehicle` 注册 `neon.r` 定义，使用 `wheel` mobility、`attachPhysics2D` 和 Box2D 碰撞。
  平面物理坐标 `(x,y)` 映射到渲染坐标 `(x,z)`；车辆速度和朝向完全来自模组。
  此例为平面街机驾驶，未模拟 3D 悬架、轮胎侧滑或动态轮胎转动。
- 以固定 60 Hz 先推进 `vehicle.update` 再推进 `world.update`；镜头只做表现层平滑。
- 驾驶辅助通过正常油门/转向输入控制同一车辆，不修改位置。
- `game.agent.json` 提供观察和驾驶动作，可通过 `eve_play` 实测驾驶、计圈和恢复。

## 文件

- `main.nut`：赛道、物理护栏、驾驶、检查点、镜头和 HUD。
- `assets/neon-r.glb`：从上一轮实际 SCAD 模型转换并减面的彩色跑车资源，坐标统一为 Y 向上、车头 +X，车长约 4.75 米。
- `assets/source/NEON_R.scad`：原始参数模型；游戏资源采用关闭车门的状态。
- `assets/DejaVuSans-Bold.ttf`、`assets/FONT-NOTICE.txt`：HUD 字体及授权。
- `captures/`、`VALIDATION.md`：引擎截图和实测记录。

运行检查：

```bash
python3 scripts/check_examples.py
MIN_RUN_SECONDS=2 RUN_SECONDS=8 bash scripts/smoke_examples.sh neon-racing
```

真实驾驶验证（先运行带调试接口的示例）：

```bash
cd examples/neon-racing
../../build/linux-debug/src/engine/eve run --mcp-port=7537
# 另一终端
python3 verify_runtime.py --port 7537
```

验证器仅通过 `eve_play` 声明的动作和观察执行，生成截图、遥测及证据会话记录。
