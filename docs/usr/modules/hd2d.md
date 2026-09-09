# HD-2D 场景渲染

HD-2D 模块把 2D 内容叠加到现有 3D 渲染管线，实现「2D tilemap 拼 3D 场景、2D 角色在 3D 世界里作为立体广告牌」的效果。它包含两类对象：

- `TileMap3D`：把 2D tilemap（`map` 模块的 `TileLayer`）挤成 3D 地形网格。
- `Sprite3D`：把 2D 精灵图/角色帧作为始终正对相机的 billboard 渲染进 3D 场景，支持精灵表帧动画。

模块绑定在 `eve.Hd2D`，脚本槽位为 `hd2d`。

可运行的真实素材示例在 `examples/hd2d-riverside`：两层 tilemap、河岸与桥、
凸起平台，以及三个四方向行走角色。用 WASD/方向键移动，1/2/3 切换角色，
Q/E 转动相机。运行 `make run/win32-debug GAME=examples/hd2d-riverside`。

## TileMap3D：2D tilemap → 3D 场景

`TileMap3D.buildRenderable(gfx, layer)` 遍历 tile layer，把每个非空格子生成一个方块：顶面采样该格子的图集区域，侧壁向下挤出到配置深度。逐格高度从 tile 的 `"height"` 自定义数据读取。返回的 `Renderable3D` 自动接入深度、阴影与 GBuffer。

```squirrel
local tile = hd2d.newTileMap3D();
tile.setSideDepth(6.0);      // 侧壁挤出深度（世界单位）
tile.setHeightScale(8.0);    // 每 1 单位 "height" 元数据对应的高度
tile.setWallUV(0.0, 0.0, 0.05, 0.05);  // 侧壁采样的图集区域
tile.setTint(1.0, 1.0, 1.0, 1.0);

local layer = map.newLayerFromFile("assets/field.json"); // 现有 2D tilemap
local ground = tile.buildRenderable(gfx, layer);
ground.setPosition(0.0, 0.0, 0.0);

local count = tile.getTileCount();      // 已烘焙的格子数
local depth = tile.getSideDepth();      // 读取当前侧壁深度
local scale = tile.getHeightScale();    // 读取当前高度缩放
```

也可以只生成网格、自行接管：`tile.buildMesh(gfx, layer)` 返回 `Mesh`（图集纹理来自 layer 的 tileset）。空的 layer 返回 `null`。

正交地图的格子起点与 `TileLayer.tileToWorldX/Y` 一致，覆盖起点到起点加格子尺寸的区域，
因此碰撞查询可以直接使用 map 模块的坐标。Tiled 导入的 H/V/对角翻转标记作用于顶面，
不翻转侧壁。`setSideDepth(0)` 只生成顶面，适合叠加图层；调用方应提供适当高度避免共面。
这是静态烘焙：修改源 tile 数据后需要重新构建。高度元数据属于 GID，不能表达同一 GID 的逐格不同高度。

## Sprite3D：2D 角色/动画作为 3D billboard

`Sprite3D` 是一张在 3D 世界里与相机图像平面平行的四边形（screen-aligned billboard），同时跟随相机的偏航、俯仰和滚转，承载一张 2D 纹理。它通过 `Renderable3D` 与地形进行深度合成；画面边缘的角色也不会因单独朝向相机位置而倾斜。

`setPosition` 默认定位图像中心。角色站立时使用 `setPivot(0.5, 1.0)`，此时位置就是脚底。
图像脚底若有 4/64 的透明边距，可用 `setPivot(0.5, 60.0/64.0)`，再把位置设在地面。
Pivot 使用可见图像左上角为 (0,0)、右下角为 (1,1)，旋转、缩放及 UV 翻转不改变世界锚点。
相机必须在解除绑定前保持有效；退化相机基向量保留最后一次有效朝向。

```squirrel
local hero = hd2d.newSprite(gfx);
hero.setTexture(gfx.newTextureFromFile("assets/hero.png"));
hero.setPosition(192.0, 16.0, 128.0);   // 世界坐标
hero.setSize(28.0, 56.0);               // billboard 尺寸
hero.setTint(1.0, 1.0, 1.0, 1.0);       // 颜色倍率（alpha 参与裁切）
hero.setVisible(true);
hero.setCamera(cam);                    // 绑定相机后 update() 每帧转向相机
gfx.setTextureSampler(hero.getTexture(), "nearest", "none", 1.0, 0.0); // 清晰像素采样

local tx = hero.getPositionX(); local ty = hero.getPositionY(); local tz = hero.getPositionZ();
local w = hero.getWidth();  local h = hero.getHeight();
local vis = hero.getVisible();
local tex = hero.getTexture();
```

### 精灵表帧动画

把一张精灵表按网格切帧，用 `play`/`update` 播放，2D 动画就能在 3D 里动起来：

```squirrel
hero.setFrameGrid(4, 2);              // 4 列 × 2 行的精灵表
hero.setFrameIndex(0);                // 直接跳到某个帧（0 起）
hero.play(0, 5, 12.0);                // 播放第 0..5 帧，每秒 12 帧
hero.setFlipX(true);                  // 水平翻转当前帧
hero.setFlipY(false);                 // 垂直翻转

function eve_update(dt) {
    hero.update(dt);                  // 推进动画时钟 + 转向相机
    if (!hero.isPlaying()) hero.play(0, 5, 12.0);
}
```

```squirrel
local cols = hero.getFrameGridColumns();   // 精灵表列数
local rows = hero.getFrameGridRows();      // 精灵表行数
local total = hero.getFrameCount();        // 总帧数（列 × 行）
local idx = hero.getFrameIndex();          // 当前帧序号
hero.stop();                               // 暂停动画
```

直接指定任意图集子矩形：`hero.setFrame(u0, v0, u1, v1)`。

图像左上角对应 billboard 的可见左上角。精灵默认使用 masked 材质，透明像素不写入颜色、
深度和阴影；角色仍保持默认自发光风格。`setTint` 同步作用于该材质。
更换网格会停止旧动画；`setFrameIndex` 将索引限制在网格内。`update` 忽略非正数和非有限
时间增量，较大的有限增量按循环周期推进。`play` 的 fps 必须为有限正数。
Sprite、Camera 与纹理的调用在渲染线程进行，Graphics 及相关 ECS 对象必须比 Sprite 活得更久。

角色控制、碰撞和相机控制由场景组合已有模块完成；HD2D 负责渲染和帧动画。示例使用 60 Hz
固定步长、无随机数，足底锚点由图集透明边距确定。示例验收说明和素材
生成记录见该示例的 README。

## 模块方法与对象

- `hd2d.getName()`、`hd2d.newTileMap3D()`、`hd2d.newSprite(gfx)`
- `TileMap3D`：`setSideDepth`/`getSideDepth`、`setHeightScale`/`getHeightScale`、`setWallUV`、`setTint`、`buildMesh`、`buildRenderable`、`getTileCount`
- `Sprite3D`：`setTexture`/`getTexture`、`setFrame`、`setFlipX`/`setFlipY`、`setFrameGrid`、`getFrameGridColumns`/`getFrameGridRows`、`setFrameIndex`/`getFrameIndex`/`getFrameCount`、`play`/`stop`/`isPlaying`/`update`、`setPosition`/`getPositionX`/`getPositionY`/`getPositionZ`、`setSize`/`getWidth`/`getHeight`、`setTint`、`setVisible`/`getVisible`
