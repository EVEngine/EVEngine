# Autotile Production — 岸线、墙体与瀑布的自动拼接生产示例

本示例在不使用任何美术资源的前提下，用 `map.newLayer` 建立三层瓦片层，分别演示岸线、
墙体与瀑布三类自动拼接（autotile family）语义。它同时覆盖邻接掩码规则登记、矩形批量
绘制与擦除、以及循环瓦片动画，可作为自动拼接落地到关卡数据前的最小生产参考。

## 运行

```bash
make run/<platform>-debug GAME=examples/autotile-production
```

也可以进入示例目录后直接启动引擎：

```bash
cd examples/autotile-production && ../../build/linux-debug/src/engine/eve run
```

Windows 下引擎可执行文件为 `build/win32-debug/src/engine/eve.exe`。

## 演示内容

- 初始化：`eve_init` 先调 `gfx.setBackgroundColor(0.04, 0.06, 0.09, 1.0)`，再在
  `map == null` 时创建 `eve.Map()`；三层分别由 `persist` 变量 `shore`、`walls`、
  `falls` 持有，已存在时不会重复建立。
- 岸线层：`map.newLayer(14, 10, 32.0, 32.0)` 建出 14×10、32 像素瓦片的层并
  `setOrigin(32.0, 48.0)` 定位；`defineAutotileFamily(1, "shore", 1337)` 注册地形 1，
  随后用 `mask` 从 0 到 255 的循环为全部邻接掩码各登记一条规则
  `setAutotileRule(1 + mask % 12, 1, mask, 1)`（gid 在 1–12 之间循环复用）。
- 矩形批量编辑：`paintTerrainRect(1, 1, 9, 7, 1)` 一次铺满整块地形，再用
  `eraseTerrainRect(7, 1, 3, 2)` 与 `eraseTerrainRect(1, 6, 2, 2)` 各挖一个缺口，
  让岸线掩码随邻接关系变化。
- 墙体层：`map.newLayer(8, 10, 32.0, 32.0)`、`setOrigin(520.0, 48.0)` 与
  `defineAutotileFamily(2, "wall", 7)` 之后，按掩码分别绑定顶部（gid 21 / 掩码 32）、
  墙体主体（gid 22 / 掩码 34）与墙脚（gid 23 / 掩码 2），两段 `paintTerrainRect`
  画出高度不同的两堵墙。
- 瀑布层：`map.newLayer(6, 10, 32.0, 32.0)`、`setOrigin(720.0, 48.0)` 与
  `defineAutotileFamily(3, "waterfall", 11)` 绑定水唇（掩码 32）、循环主体（掩码 34）
  和溅落 / 墙脚（掩码 2）；再用 `addTileAnimationFrame(32, 32, 140)`、
  `addTileAnimationFrame(32, 34, 140)`、`addTileAnimationFrame(32, 35, 140)` 让主体
  瓦片以 140 毫秒换帧循环播放。
- 每帧驱动：`eve_update(dt)` 调用 `map.update(dt)` 推进瓦片动画，`eve_render` 依次
  调用 `gfx.clear()` 与 `map.render(gfx)` 重绘三层。

## 操作

`main.nut` 不轮询任何键盘或鼠标输入，示例里没有任何按键或鼠标绑定：三层地形与全部
自动拼接规则都在 `eve_init` 中一次性建立，运行时只按帧推进动画与重绘。

## 相关文件

- `main.nut`：示例全部逻辑——三层创建、family 与邻接掩码规则登记、矩形绘制 / 擦除、
  瓦片动画，以及 `eve_update` / `eve_render` 的每帧更新与渲染。
- `config.nut`：窗口配置，标题为 `EVEngine Autotile Production`，尺寸 960×540，
  `debug = false`。
- `README.md`：本文件。
