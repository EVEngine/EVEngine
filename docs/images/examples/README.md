# 示例渲染画廊（Example Render Gallery）

先看角色、地形和完整场景，再看各系统的可视化实验。本页主展示区收录 **39 个示例**，
另保留 2 个明确标注的历史截图。点击图片可打开原始 PNG；示例名链接到运行说明。

所有截图均来自 Windows + 原生 GPU 上的真实引擎渲染，由 `gfx.saveFramePng()`
读取呈现帧，未经后期处理。帧数、场景和取景不同，静态截图不能代表完整交互或动画。

## 精选场景

### 视觉小说《潮汐电台》 · [galgame](../../../examples/galgame/README.md)

原创背景、对话界面与 Avatar 角色立绘；通过正常 Start / Advance 动作进入剧情。

[<img src="galgame.png" width="720" alt="视觉小说《潮汐电台》">](galgame.png)

### HD2D 河畔 · [hd2d-riverside](../../../examples/hd2d-riverside/README.md)

像素角色与建筑放入正交 3D 场景，展示河道、桥梁与像素素材。

[<img src="hd2d-riverside.png" width="720" alt="HD2D 河畔">](hd2d-riverside.png)

### 角色分层动画 · [layered-animation](../../../examples/layered-animation/README.md)

三段 KayKit 动作独立播放，与走路基底 + 脊柱遮罩攻击 + 受击叠加的结果并排。

[<img src="layered-animation.png" width="720" alt="角色分层动画">](layered-animation.png)

### 程序化六边形岛屿 · [hex-terrain](../../../examples/hex-terrain/README.md)

无需源美术的高程、岸线、植被和六边形地形组合。

[<img src="hex-terrain.png" width="720" alt="程序化六边形岛屿">](hex-terrain.png)

### 自然材质近景 · [procgen-nature-quality](../../../examples/procgen-nature-quality/README.md)

程序化树叶、树干、灌木、苔藓与崖石材质的同场景展示。

[<img src="procgen-nature-quality.png" width="720" alt="自然材质近景">](procgen-nature-quality.png)

### 可交互积雪 · [snow](../../../examples/snow/README.md)

雪深驱动的真实地形形变；截图直接展示脚印凹陷。

[<img src="snow.png" width="720" alt="可交互积雪">](snow.png)

## 角色与 3D 玩法

| 示例 | 真实渲染帧 | 画面内容 |
|---|---|---|
| [character-motion-lab](../../../examples/character-motion-lab/README.md) | [<img src="character-motion-lab.png" width="420" alt="character-motion-lab">](character-motion-lab.png) | 三种角色的动作重定向与同步播放实验室 |
| [tactics](../../../examples/tactics/README.md) | [<img src="tactics.png" width="420" alt="tactics">](tactics.png) | KayKit 角色、敌人和固定镜头 3D 战棋棋盘 |
| [armored-command-3d](../../../examples/armored-command-3d/README.md) | [<img src="armored-command-3d.png" width="420" alt="armored-command-3d">](armored-command-3d.png) | 带骨骼的坦克、编队与战场；截图为默认全场镜头 |
| [venom-transform](../../../examples/venom-transform/README.md) | [<img src="venom-transform.png" width="420" alt="venom-transform">](venom-transform.png) | 近似变装演出：形变、材质过渡与演出几何 |

## 地形、地图与程序化生成

| 示例 | 真实渲染帧 | 画面内容 |
|---|---|---|
| [terrain-preview](../../../examples/terrain-preview/README.md) | [<img src="terrain-preview.png" width="420" alt="terrain-preview">](terrain-preview.png) | 单个侵蚀地形、河湖水面与生物群系 |
| [terrain-gallery](../../../examples/terrain-gallery/README.md) | [<img src="terrain-gallery.png" width="420" alt="terrain-gallery">](terrain-gallery.png) | 三个固定 seed 在相同参数和光照下并排对比 |
| [voxel-terrain](../../../examples/voxel-terrain/README.md) | [<img src="voxel-terrain.png" width="420" alt="voxel-terrain">](voxel-terrain.png) | 体素山地：chunk、块面与顶点 AO |
| [topdown-procmap](../../../examples/topdown-procmap/README.md) | [<img src="topdown-procmap.png" width="420" alt="topdown-procmap">](topdown-procmap.png) | 俯视地形、植被与岩石；截图为默认近景 |
| [castle-generator](../../../examples/castle-generator/README.md) | [<img src="castle-generator.png" width="420" alt="castle-generator">](castle-generator.png) | 程序化城堡的城墙、塔楼与同心结构 |
| [cave-generator](../../../examples/cave-generator/README.md) | [<img src="cave-generator.png" width="420" alt="cave-generator">](cave-generator.png) | 程序化洞穴内景与通道 |
| [bush-generator](../../../examples/bush-generator/README.md) | [<img src="bush-generator.png" width="420" alt="bush-generator">](bush-generator.png) | 可调灌木配方与植被预览 |
| [roguelike-generator](../../../examples/roguelike-generator/README.md) | [<img src="roguelike-generator.png" width="420" alt="roguelike-generator">](roguelike-generator.png) | 房间与走廊连接的地牢平面 |
| [procgen](../../../examples/procgen/README.md) | [<img src="procgen.png" width="420" alt="procgen">](procgen.png) | 地图算法与纹理配方基础演示 |
| [mesh-modifier-lab](../../../examples/mesh-modifier-lab/README.md) | [<img src="mesh-modifier-lab.png" width="420" alt="mesh-modifier-lab">](mesh-modifier-lab.png) | 13 种网格变形结果并排对照 |
| [spline-tube-lab](../../../examples/spline-tube-lab/README.md) | [<img src="spline-tube-lab.png" width="420" alt="spline-tube-lab">](spline-tube-lab.png) | 样条驱动的管道、带状体与截面挤出 |

## 天空、水体与特效

| 示例 | 真实渲染帧 | 画面内容 |
|---|---|---|
| [weather](../../../examples/weather/README.md) | [<img src="weather.png" width="420" alt="weather">](weather.png) <br> [<img src="weather-snow.png" width="420" alt="weather snow">](weather-snow.png) | 暴雨预设，密度 90%；附雪天预设对照 |
| [weather-daynight](../../../examples/weather-daynight/README.md) | [<img src="weather-daynight.png" width="420" alt="weather-daynight">](weather-daynight.png) | 统一天气与昼夜场景中的地形、瀑布与树木 |
| [daynight](../../../examples/daynight/README.md) | [<img src="daynight.png" width="420" alt="daynight">](daynight.png) | 程序化天空、体积云与太阳光照 |
| [waterfall-demo](../../../examples/waterfall-demo/README.md) | [<img src="waterfall-demo.png" width="420" alt="waterfall-demo">](waterfall-demo.png) | 瀑布水流着色器：条纹、湍流与泡沫 |
| [dynamic-water-grid](../../../examples/dynamic-water-grid/README.md) | [<img src="dynamic-water-grid.png" width="420" alt="dynamic-water-grid">](dynamic-water-grid.png) | 等距双网格岸线、连续水面与像素角色 |
| [fluid3d](../../../examples/fluid3d/README.md) | [<img src="fluid3d.png" width="420" alt="fluid3d">](fluid3d.png) | 运行中的默认水体仿真与表面重建；截图非暂停状态 |
| [particle-playback-lab](../../../examples/particle-playback-lab/README.md) | [<img src="particle-playback-lab.png" width="420" alt="particle-playback-lab">](particle-playback-lab.png) | 粒子轨迹与爆发的确定性回放场景 |
| [interior-mapping](../../../examples/interior-mapping/README.md) | [<img src="interior-mapping.png" width="420" alt="interior-mapping">](interior-mapping.png) | 窗格内的室内映射与伪透视空间 |
| [virtual-texture-blending](../../../examples/virtual-texture-blending/README.md) | [<img src="virtual-texture-blending.png" width="420" alt="virtual-texture-blending">](virtual-texture-blending.png) | 虚拟纹理页面与材质混合对照 |
| [triplanar-decal](../../../examples/triplanar-decal/README.md) | [<img src="triplanar-decal.png" width="420" alt="triplanar decal">](triplanar-decal.png) | 贴花跨越地面和两面墙的三平面投射 |
| [ink-arena](../../../examples/ink-arena/README.md) | [<img src="ink-arena.png" width="420" alt="ink arena">](ink-arena.png) | 喷墨覆盖地板、墙壁、方块和圆柱 |

## 2D、UI 与基础能力

| 示例 | 真实渲染帧 | 画面内容 |
|---|---|---|
| [swappable-tilesets](../../../examples/swappable-tilesets/README.md) | [<img src="swappable-tilesets.png" width="420" alt="swappable-tilesets">](swappable-tilesets.png) | 可替换的像素 tileset、道路自动拼接与树木精灵 |
| [editor-ui-gallery](../../../examples/editor-ui-gallery/README.md) | [<img src="editor-ui-gallery.png" width="420" alt="editor-ui-gallery">](editor-ui-gallery.png) | 菜单、工具栏、检查器、表单和游戏 HUD 共存 |
| [pixelworld](../../../examples/pixelworld/README.md) | [<img src="pixelworld.png" width="420" alt="pixelworld">](pixelworld.png) | 关闭目录编辑面板后的沙、水、木材与地形仿真 |
| [crowd](../../../examples/crowd/README.md) | [<img src="crowd.png" width="420" alt="crowd">](crowd.png) | 2000 个单位在障碍流场中行军 |
| [sprite-stack](../../../examples/sprite-stack/README.md) | [<img src="sprite-stack.png" width="420" alt="sprite-stack">](sprite-stack.png) | 3D 模型切片叠成伪 3D 精灵 |
| [camera-controllers](../../../examples/camera-controllers/README.md) | [<img src="camera-controllers.png" width="420" alt="camera-controllers">](camera-controllers.png) | 相机控制器的默认测试场景 |
| [basic](../../../examples/basic/README.md) | [<img src="basic.png" width="420" alt="basic">](basic.png) | 刚体、地面、粒子与调试绘制基础场景 |

## 历史截图

以下两张图不是本次运行的验收结果，当前本地构建未通过其抓帧；保留用于参考。

| 示例 | 历史画面 | 内容 |
|---|---|---|
| [rock-generator](../../../examples/rock-generator/README.md) | <img src="rock-generator.png" width="360" alt="historical rock"> | 程序化岩石 |
| [hex-levels](../../../examples/hex-levels/README.md) | <img src="hex-levels.png" width="360" alt="historical hex levels"> | 六边形关卡 |

另有之前保存的 [昼夜体积云画面](daynight-volumetric-clouds.png)，本次未重新抓取。

## 复现抓帧

```powershell
./scripts/capture_example.ps1 -Eve build/win32-debug/src/engine/eve.exe -Example layered-animation -Out docs/images/examples/layered-animation.png -Frames 120

# 选取示例已有的暴雨预设；-Setup 在正常初始化完成后运行
./scripts/capture_example.ps1 -Eve build/win32-debug/src/engine/eve.exe -Example weather -Out docs/images/examples/weather.png -Frames 150 -Setup scripts/gallery_presets/weather-storm.nut
```

抓帧脚本把 `scripts/capture_root.nut` 的帧后钩子组合进仓库标准 `src/scripts/load.nut`，
使用相同的模块筛选、输入辅助、更新、渲染与退出流程；不另行复制启动流程。
只有出现 `CAPTURED` 标记、生成新的 PNG 且日志无初始化/帧错误时才替换目标文件。
运行失败会保留原图，不回退到桌面截图。SHARED 构建的引擎 DLL 目录会加入子进程
搜索路径，其他依赖 DLL 目录需在 `PATH` 中。

场景预设位于 `scripts/gallery_presets/`：暴雨/雪天、关闭 PixelWorld 编辑面板、
恢复 Fluid3D 默认水体仿真，以及通过正常对话推进进入 Galgame 剧情。
主展示使用 120 帧，天气使用 150 帧、Fluid3D 使用 180 帧；更早收录的基础示例
使用 180 帧。实时 dt 会影响动画相位和动态场景，帧数相同不保证像素级一致。

## 更新来源与未收录项目（2026-10-11）

本次使用可运行的本地 Debug 构建：`v0.5.1-dev`，`git=0182af046d0e+dirty`，
构建时间 `2026-09-21 23:26:58 +0800`。新增图片经过引擎抓帧日志和实际画面检查。
这是当前示例脚本在该本地构建上的展示，**不代表当前仓库 HEAD 的完整渲染验收**。
精选内容覆盖角色动画、3D 玩法、地形生成、2D 场景、仿真与 UI；更多示例见
[示例总览](../../../examples/README.md)。

未采用的画面：`anime-character-lab`、`vrm-avatar`、`softbody3d`、`iron-chain-bridge`
没有显示预期主体；`procedural-textures`、`skyscraper-generator`、`urban-generator`
缺少预期纹理或场景；`rendering-chain-lab` 有明显画面异常；`material-editor`、
`hex-terrain-3d` 当前画面过亮；`linear-structures` 主体取景不完整；`uds-sky` 仅展示星空。
`gpu-agents`、`lighting-showcase`、`atmospheric-fog`、`realtime-fog`、`attack-vfx`、
`housegen`、`procedural-road`、`hex-planet`、
`combat-action-editor` 的本地运行/API 不匹配，未作为成功画面收录。
抓帧工具使用独立临时配置，不覆盖示例自己的 `capture_settings.nut`；
`ink-arena` 和 `triplanar-decal` 已通过该方式重新抓帧并收录。

前次画廊中的 `rpg`（文字重叠）、`metroidvania`（纹理可疑）、`tree-generator`
（画面含不明色块）和 `building-3d`（默认空地面）已退出主展示区；对应文件仍保留，
待修复或取得有代表性的画面后再收录。

使用发布 SDK `v0.5.2`（`git=352f4f4adce6`，Release）复查后，
`mesh-contact-fusion` 仍缺少 `MeshMergePlan`，`destruction-clusters` 仍缺少
`Destruction`，`commandery-rts` 在更新时缺少 `steeringArrive2`；
`terrain-smooth-mountain` 的地形材质 shader 创建返回空。
`dungeon-generator-3d` 与 `pcg-biome` 虽能抓帧，但分别只有散落方块和近乎空屏，
未作为代表性画面收录。此次复查没有替换上面的 Debug 构建截图。
