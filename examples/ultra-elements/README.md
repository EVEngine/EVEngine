# Ultra Elements / 参数化法术 VFX 实验室

把 [Humpalumps/ultra-elements](https://github.com/Humpalumps/ultra-elements) 的 **39 个法术参数目录**
（params / ranges / presets / timing）移植到 EVEngine 的可运行示例。表现层用引擎原生能力：

- 池化 `Renderable3D`（弹体、束流、尖刺、环、柱、冲击）
- `weather.prepareLightning`（雷系 / 混合闪电）
- `eve.Particles()` 冲击 burst（`particles/burst.effect.json`，`intensity` 参数可调）
- Eve `ui` 滑条实时写回 `spell.params`

这是**表现实验室**，不是玩法 `effects` buff 模块，也不发伤害。源项目里的 WebGL2
体积流体 / Metaballs / GroundField 求解器未逐行搬迁；对应观感用网格道具 + 粒子 + 闪电近似，
但**旋钮语义与预设与上游一致**，便于继续加深单个法术。

## 运行

```sh
make run/linux-debug GAME=examples/ultra-elements
# 或已有 eve：eve run examples/ultra-elements
```

Headless 冒烟：

```sh
MIN_RUN_SECONDS=2 bash scripts/smoke_examples.sh ultra-elements
```

## 操作

- `Space` / Cast：施放当前法术（按住可维持 beam）
- `Q` / `E`：上一个 / 下一个法术（受元素过滤）
- `1`–`0` / `H`：过滤 Lightning…Arcane / Hybrid（`All` 看全部 39）
- `F`：自动轮播开关；`R`：恢复当前法术默认参数
- 右侧滑条：改的是 live `spell.params`（与源项目 GUI folder 对应）
- Preset 按钮：套用上游预设（hellfire、supercell、glacier…）

成功标记：控制台打印 `ULTRA_ELEMENTS_READY spells=39 …`，施放时打印 `ULTRA_ELEMENTS_CAST …`。

## 复用

```squirrel
local catalog = loadfile("fx/catalog.nut")();
local runtime = loadfile("fx/runtime.nut")();
local presenter = loadfile("fx/presenter.nut")();
local fx = presenter.create(gfx, eve.Particles(), weather);
local casts = runtime.create(fx);
local spell = catalog.spells[0];
spell.params.intensity = 1.4; // 任意参数
casts.cast(spell, spell.forms[0], {x=0,y=1.2,z=0}, {x=3,y=1.1,z=0});
// eve_update:
casts.update(dt); fx.updateBolts(dt); fx.updateParticles(dt); fx.drawAll(casts.list);
```

`fx/catalog.nut` 由上游 spell 模块的 params/ranges/presets/timing 生成；改滑条范围或默认值时编辑该文件（或重新跑生成脚本）。

## 架构说明

- 仅新增 `examples/ultra-elements/`，无新引擎模块、无 ECS System、无持久化格式。
- 注入 `dt`；施放种子来自单调计数，不读墙钟驱动模拟。
- 池化道具在 `prepare`/`create` 一次分配，施放路径不 `new` 网格。
- 规则适用：唯一状态 owner（runtime cast 记录）、注入时间、示例可冒烟；无架构例外。
