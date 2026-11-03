# Ultra Elements / 参数化法术 VFX 实验室

把 [Humpalumps/ultra-elements](https://github.com/Humpalumps/ultra-elements) 的 **39 个法术参数目录**
（params / ranges / presets / timing）移植到 EVEngine 的可运行示例。表现层用引擎原生能力：

- **Fire（Fireball / Inferno）高保真体积光**：`shaders/fireball.frag` 移植上游 `FIREBALL_GLSL` /
  blackbody / fireL / 噪声流，tornado-lab 式 fullscreen raymarch；滑条与
  `canonical` / hellfire / blue flame / soulfire 预设实时驱动体积参数，默认组与原版视觉对齐
- 其余法术：池化 `Renderable3D` + `weather.prepareLightning` + `eve.Particles()` 近似
- Eve `ui` 滑条实时写回 `spell.params`

这是**表现实验室**，不是玩法 `effects` buff 模块，也不发伤害。

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
- `2`：直达 Fire（启用 HD 体积路径，标题显示 `[HD VOLUME ~ original]`）
- `F`：自动轮播开关；`R`：恢复当前法术默认参数
- 右侧滑条：改的是 live `spell.params`（与源项目 GUI folder 对应）
- Fire preset：`canonical`（原版默认）/ hellfire / blue flame / soulfire

成功标记：控制台打印 `ULTRA_ELEMENTS_READY … fireHd=1`，施放时打印 `ULTRA_ELEMENTS_CAST …`。

## 复用

```squirrel
local catalog = loadfile("fx/catalog.nut")();
local runtime = loadfile("fx/runtime.nut")();
local presenter = loadfile("fx/presenter.nut")();
local fireHd = loadfile("fx/fire_hd.nut")();
local fx = presenter.create(gfx, eve.Particles(), weather);
local hd = fireHd.create(gfx);
local casts = runtime.create(fx);
// Fire 选中时：hd.pickAndSync(casts.list, spell, form, orbit, time); hd.render(gfx);
```

`fx/catalog.nut` 由上游 spell 模块的 params/ranges/presets/timing 生成；改滑条范围或默认值时编辑该文件（或重新跑生成脚本）。

## 架构说明

- 仅新增 `examples/ultra-elements/`，无新引擎模块、无 ECS System、无持久化格式。
- 注入 `dt`；施放种子来自单调计数，不读墙钟驱动模拟。
- 池化道具在 `prepare`/`create` 一次分配，施放路径不 `new` 网格。
- Fire HD 用 push_constant `float p[32]`（与 tornado-lab 同契约）；SPIR-V 由
  `glslc -fshader-stage=frag shaders/fireball.frag -o shaders/fireball.frag.spv` 生成。
- 规则适用：唯一状态 owner（runtime cast 记录）、注入时间、示例可冒烟；无架构例外。
