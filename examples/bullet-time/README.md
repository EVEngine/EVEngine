# Bullet Time / 子弹时间 Killcam

狙击精英式命中过场：正常对峙被打断 → 子弹慢放跟随弹道 → 穿墙 X-ray 命中高亮 → 恢复流程。

```sh
make run/<platform>-debug GAME=examples/bullet-time
```

需要 graphics、`stylize`、`keyboard`、`font`。GBuffer 由示例在启动时打开（X-ray 采样场景深度）。字体为附带许可的 DejaVu Sans（见 `assets/fonts/NOTICE.txt`）。

## 演示内容

1. **idle**：射手与掩体后目标对峙；目标轻微晃动；相机在侧后观察位。
2. **flight**（Space 或自动循环）：世界时间尺度骤降至约 `0.06`，相机贴弹飞行，弹道拖尾与暗角叠层。
3. **impact**：子弹几乎冻结；目标开启 `setXRayHighlight`，被掩体挡住的轮廓以红色透视高亮；冲击碎片与脉冲环。
4. **aftermath**：时间尺度缓升回 `1`，关闭 X-ray，回到 idle。

脚本侧用双时钟：`realDt` 驱动过场/镜头插值，`simDt = realDt * timeScale` 驱动世界与子弹。游戏逻辑在 `flight`/`impact` 期间不推进（可据此挂起 AI、输入、胜负判定）。没有第二套引擎时钟 API——时间膨胀是可复制的编排模式。

## 控制

| 输入 | 行为 |
| --- | --- |
| Space | 立即触发一次子弹时间过场（进行中则忽略） |
| （无操作） | 约每 5 秒自动开火演示 |

## 成功标记

- `BULLET_TIME_READY` — 场景与 X-ray shader 就绪
- `BULLET_TIME_FIRE` — 进入飞行慢放
- `BULLET_TIME_IMPACT` — 命中并打开 X-ray
- `BULLET_TIME_PASS cycles=N` — 至少完成一轮完整过场

```sh
MIN_RUN_SECONDS=2 bash scripts/smoke_examples.sh bullet-time
```

## 复用要点

```squirrel
local xray = eve.Stylize().newMeshShader(gfx, "xray");
target.setXRayShader(xray);
// …命中时
timeScale = 0.05;
target.setXRayHighlight(true);
// …过场结束
target.setXRayHighlight(false);
timeScale = 1.0;
```

X-ray 绑定见 `Renderable3D.setXRayShader` / `setXRayHighlight`；风格说明见 [stylize 手册](../../docs/usr/modules/stylize.md)。
