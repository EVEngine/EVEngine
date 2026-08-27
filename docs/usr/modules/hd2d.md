# HD-2D 场景渲染

HD-2D 模块把 2D 内容叠加到现有 3D 渲染管线，实现「2D tilemap 拼 3D 场景、2D 角色在 3D 世界里作为立体广告牌」的效果。它包含两类对象：

- `TileMap3D`：把 2D tilemap（`map` 模块的 `TileLayer`）挤成 3D 地形网格。
- `Sprite3D`：把 2D 精灵图/角色帧作为始终正对相机的 billboard 渲染进 3D 场景，支持精灵表帧动画。

模块绑定在 `eve.Hd2D`，脚本槽位为 `hd2d`。

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

## Sprite3D：2D 角色/动画作为 3D billboard

`Sprite3D` 是一张在 3D 世界里始终正对相机的四边形（圆柱 billboard：只绕世界 Y 轴转向相机、保持竖直），承载一张 2D 纹理（角色立绘或动画帧）。它通过 `eve` 的 `Renderable3D` 进入前向 pass，因此能和地形正确合成并投射阴影。

```squirrel
local hero = hd2d.newSprite(gfx);
hero.setTexture(gfx.newTextureFromFile("assets/hero.png"));
hero.setPosition(192.0, 16.0, 128.0);   // 世界坐标
hero.setSize(28.0, 56.0);               // billboard 尺寸
hero.setTint(1.0, 1.0, 1.0, 1.0);       // 颜色倍率（alpha 参与裁切）
hero.setVisible(true);
hero.setCamera(cam);                    // 绑定相机后 update() 每帧转向相机

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

## 模块方法与对象

- `hd2d.getName()`、`hd2d.newTileMap3D()`、`hd2d.newSprite(gfx)`
- `TileMap3D`：`setSideDepth`/`getSideDepth`、`setHeightScale`/`getHeightScale`、`setWallUV`、`setTint`、`buildMesh`、`buildRenderable`、`getTileCount`
- `Sprite3D`：`setTexture`/`getTexture`、`setFrame`、`setFlipX`/`setFlipY`、`setFrameGrid`、`getFrameGridColumns`/`getFrameGridRows`、`setFrameIndex`/`getFrameIndex`/`getFrameCount`、`play`/`stop`/`isPlaying`/`update`、`setPosition`/`getPositionX`/`getPositionY`/`getPositionZ`、`setSize`/`getWidth`/`getHeight`、`setTint`、`setVisible`/`getVisible`