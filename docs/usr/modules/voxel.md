# 体素（Voxel）

**脚本入口：** `eve.Voxel()`

高性能 3D 体素世界：32³ chunk、贪婪矩形合并（面合并成矩形减少实例数）、32-bit 打包实例、
六向子缓冲、视锥/视距/朝向裁剪后实例化绘制。支持跨 chunk 接缝消隐、并行重建、
顶点环境光遮蔽（AO）、DDA 射线拾取、存档/流式卸载与**自动流式地形生成**，
适合建造/破坏玩法与可移动大世界。

## 基本用法

```squirrel
voxel <- eve.Voxel();

// 1. 方块类型注册表（可选；不传则类型 id 直接当纹理 id）
local types = voxel.newCubeTypes();
types.loadFromJson(
    '[{"name":"grass","faceTex":[1,1,2,3,1,1]},' +
    '{"name":"furnace","faceTex":[4,4,4,4,5,4],"directional":true}]');

// 2. 建世界（内部拷贝注册表）
local world = voxel.newWorldWithTypes(types);

// 3. 填方块：setVoxel(x,y,z,texId) 或按名字+朝向
world.setVoxelByName(0, 0, 0, "grass");
world.setVoxelByName(2, 0, 0, "furnace", 2);   // 绕 Y 旋转 180°
world.setVoxel(5, 0, 0, 7);                    // 直接纹理 id

// 4. 重建脏 chunk（可并行；0=自动，1=串行）
world.remeshDirty();

// 5. 每帧选择可见 chunk 并绘制
function eve_render() {
    gfx.clear();
    gfx.render3D();
    // viewProj 为列主序 16 浮点（proj*view），eye 为相机位置
    world.selectVisible(viewProj, eye.x, eye.y, eye.z, 128.0, true);
    world.drawVisible(gfx, atlasTex, 16);      // atlas 16x16 分格
}
```

## 流式大世界（每帧保持玩家周围 chunk）

```squirrel
// 一次性配置地形生成器（seed + 草/土/石纹理 id + 基准高度 + 幅度 + 缩放）
world.setTerrain(20260819, 1, 2, 3, 8.0, 14.0, 1.0 / 32.0);

// 或使用细粒度参数：完整 procgen TerrainSampler（岛屿衰减、大陆形状、
// 山脊/扭曲、八度）都能从脚本配置，低海拔柱子可选沙滩带。
world.setTerrainParam("seed", 20260819);
world.setTerrainParam("top", 1);        // 草
world.setTerrainParam("sub", 2);        // 土
world.setTerrainParam("stone", 3);      // 石
world.setTerrainParam("sand", 5);       // 沙（沙滩带纹理）
world.setTerrainParam("sandLevel", 0.30);  // 采样高度 ≤0.30 的柱子顶层用沙
world.setTerrainParam("base", 8.0);
world.setTerrainParam("amplitude", 18.0);
world.setTerrainParam("scale", 1.0 / 64.0);  // 频率别名
world.setTerrainParam("island", 0.45);       // 岛屿衰减强度（配 worldSize）
world.setTerrainParam("worldWidth", 512);    // 岛屿衰减世界框
world.setTerrainParam("worldHeight", 512);
world.setTerrainParam("ridge", 0.35);        // 山脊混合
world.setTerrainParam("enable", 1);          // 打开地形（默认关闭）

// 每帧（或玩家跨 chunk 边界时）调用：自动补建/卸载并重建网格
local playerChunkX = floor(player.x / 32.0);
local playerChunkY = floor(player.y / 32.0);
local playerChunkZ = floor(player.z / 32.0);
world.streamAround(playerChunkX, playerChunkY, playerChunkZ, 4);  // 半径 4 chunk
world.selectVisible(viewProj, player.x, player.y, player.z, 160.0, true);
world.drawVisible(gfx, atlasTex, 16);
```

`streamAround` 返回本次新建 chunk 数（卸载数可用 `getChunkCount` 差值计算）。
地形是确定性的：同一世界坐标 + 同一 seed 永远得到相同高度，跨 chunk 无缝衔接。

### 使用烘焙 EVTR 地形

需要侵蚀、河网和生态群落时，先由 Procgen 烘焙 `EVTR`，再交给体素世界。
`streamAround` 会自动预取覆盖体素半径的地形资产块，资产高度优先于噪声回退：

```squirrel
local p = procgen.newParams();
p.setSeed(42); p.setSize(512, 512);
local hm = procgen.generateHeightmap(p);
procgen.erodeTerrainHydraulic(hm, 60, 0.012, 0.08, 2.0, 0.18, 0.12);
local layers = procgen.analyzeTerrain(hm, 0.025, 0.25, 0.65);
local evtr = procgen.bakeTerrainAsset(hm, layers, 64);

// storedHeight * 24 + 6 映射到体素世界 Y。
world.loadTerrainAsset(evtr, 6.0, 24.0);
// vegetation, sand, snow, alpine rock, river bed 的 atlas/type id。
world.setTerrainAssetMaterials(1, 4, 5, 3, 6);
world.streamAround(playerChunkX, playerChunkY, playerChunkZ, 4);
```

也可用 `streamTerrainAssetAround(worldX, worldZ, radius, maxLoads)` 手动设置每帧解码
预算；返回本帧加载的资产块数。`getTerrainAssetResidentCount()` 返回当前解码缓存数。

## 射线拾取（放置/破坏/瞄准）

```squirrel
// world.raycast(ox,oy,oz, dx,dy,dz, maxDist) -> bool
if (world.raycast(eye.x, eye.y, eye.z, dir.x, dir.y, dir.z, 8.0)) {
    local hx = world.getRaycastHitX();
    local hy = world.getRaycastHitY();
    local hz = world.getRaycastHitZ();
    // 命中面法线：在 hit + face 处放置新方块
    world.setVoxelByName(hx + world.getRaycastFaceX(),
                         hy + world.getRaycastFaceY(),
                         hz + world.getRaycastFaceZ(), "grass");
    // 或破坏：world.setVoxel(hx, hy, hz, 0);
}
```

## API 快查

### `Voxel`（模块）

- `newCubeTypes()`：新建空方块类型注册表。
- `newWorld()` / `newWorldWithTypes(types)`：新建世界（可带注册表）。
- `getChunkSize()`：32（常量）。

### `VoxelCubeTypes`（注册表）

- `loadFromJson(json)`：批量注册，元素 `{"name","faceTex":[6],"directional","composeGroup","connects"}`。
- `count()` / `variantCount()` / `clear()`。

### `VoxelWorld`（世界）

- 编辑：`getVoxel(x,y,z)`、`setVoxel(x,y,z,texId)`（0=空气，不创建空 chunk）、
  `setVoxelByName(x,y,z,name,orientation)`、`getCubeTypeName` / `getCubeTypeTex`、
  `getRevision()`（内容真实变化时单调增长，供编辑器、存档和预览失效检测）。
- 网格：`remeshDirty()`（自动并行）、`getChunkCount()`、`hasChunk`、`removeChunk`、`clear`。
- 存档/流式：`saveWorld()`（返回 `data.newByteData` 可直接落盘）、`loadWorld(byteData)`、
  `unloadChunksOutside(cx,cy,cz,radiusChunks)`（卸载半径外的 chunk，返回卸载数）、
  `streamAround(cx,cy,cz,radiusChunks)`（自动补建+生成+卸载+重建，返回新建数）、
  `setTerrain(seed, top, sub, stone, base, amp, scale)`（快速配置）、
  `setTerrainParam(key, value)`（细粒度配置，见下方参数表）、
  `loadTerrainAsset(bytes, offset, scale)`、
  `streamTerrainAssetAround(worldX,worldZ,radius,maxLoads)`、
  `setTerrainAssetMaterials(vegetation,sand,snow,alpine,riverbed)` / `disableTerrain()`。
- 渲染：`selectVisible(viewProj16, eyeX,eyeY,eyeZ, viewRange, faceCull)`、
  `drawVisible(gfx, atlasTex, tilesPerRow)`、`getVisibleBatchCount` / `getVisibleChunkCount` /
  `getVisibleRectCount`。
- 拾取：`raycast(ox,oy,oz,dx,dy,dz,maxDist)` + `lastRaycastHit()`、
  `getRaycastHitX/Y/Z`、`getRaycastPrevX/Y/Z`（命中前一个空气方块）、
  `getRaycastFaceX/Y/Z`（进入面法线）。

## 生命周期

- 世界与注册表由脚本 VM 持有，不要直接构造 `VoxelWorld` / `VoxelCubeTypes`。
- 边界编辑会同时把相邻 chunk 标记 dirty；`remeshDirty()` 后接缝两侧一起重建，
  不会残留被遮挡的旧面。
- 视距外的脏 chunk 不会在 `selectVisible` 时重建，进入视距后自动补网格。
- 顶点 AO 在 remesh 时自动烘焙到矩形四角（0..3），无需额外配置；平坦表面全亮，
  台阶/凹角自然变暗。
- `setTerrainParam` 参数表：`seed`/`top`/`sub`/`stone`/`sand`（纹理 id）、
  `base`/`amplitude`（世界高度映射）、`scale` 或 `frequency`（噪声频率，
  默认 1/32）、`octaves`/`lacunarity`/`gain`（fBm）、`ridge`/`warp`/`exponent`
  （山脊/域扭曲/指数）、`continent`/`island`/`coast`（大陆-海岸形状）、
  `worldWidth`/`worldHeight`（岛屿衰减框，>0 才生效）、`sandLevel`
  （归一化采样高度阈值，>0 且 `sand` 非 0 时启用沙滩带）、`enable`（1 打开地形）。
  `setTerrainParam` 只覆盖传入的键，其余参数保持默认；地形默认关闭，需 `enable=1`。
- 存档格式为可移植小端二进制（`EVVX` + 版本 + chunk 坐标与体素），加载后网格按需重建。
