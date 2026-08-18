# 建筑放置 — `building`

面向策略 / RTS / 经营模拟（ETS）的建筑放置框架：数据驱动定义、格子占用世界、鬼影预览、
可插拔校验与吸附规则。设计见 [建筑放置系统设计](../../dev/建筑放置系统设计.md)。

可运行示例：[`examples/building`](../../../examples/building/)（`make building`）。

## 入口

```squirrel
building <- eve.Building();
building.registerBuildingsFromJson(jsonText);
local world = building.newWorld(64, 64, 32.0); // 宽、高、格子像素/世界单位
local ghost = building.newGhost();
```

## 最小示例

```squirrel
building <- eve.Building();
building.registerBuildingsFromJson(@"
[
  {\"id\":\"house\",\"displayName\":\"House\",\"footprintW\":2,\"footprintH\":2,
   \"tags\":[\"house\"],\"cost\":{\"wood\":20}},
  {\"id\":\"road\",\"footprintW\":1,\"footprintH\":1,\"tags\":[\"road\"]}
]
");

world <- building.newWorld(32, 32, 32.0);
world.setId("town");
world.fillTerrain(1);

ghost <- building.newGhost();
ghost.setBuildingId("house");
ghost.setFromWorld(world, mouseX, mouseY); // 按定义 snapMode 吸附
if (ghost.validate(world)) {
    local id = world.placeGhost(ghost);
    print("placed " + id + "\n");
} else {
    print("cannot place: " + ghost.getReason() + "\n");
}
```

## 任务导向示例

### 1. 沿道路摆摊（邻接标签）

定义里设 `"requireAdjacentTag":"road"`，先铺 `road`，再放摊位；失败原因为 `adjacency_tag`。

### 2. 码头只能建在水域

地形语义由 `world.setTerrain(x,y,semantic)` 写入；定义 `"requireTerrain":[2]`。
陆地格会得到 `terrain_mismatch`。

### 3. 自定义“金币不足”校验（C++）

```cpp
PlacementSystem::registerValidateRule("needGold",
    [](const PlacementWorld&, const PlacementQuery&, std::string* reason) {
        if (playerGold < 10) { if (reason) *reason = "not_enough_gold"; return false; }
        return true;
    });
```

建筑定义 `"validateRule":"needGold"`；脚本侧只需选用该规则名。

## API 快查

### `Building`（模块）

| 方法 | 说明 |
|------|------|
| `registerBuildingsFromJson(json)` | 批量注册定义，返回成功数 |
| `clearBuildingDefinitions()` | 清空定义表 |
| `getBuildingDefinitionCount()` / `hasBuildingDefinition(id)` | 查询 |
| `getBuildingDisplayName` / `Category` / `FootprintW` / `FootprintH` | 定义字段 |
| `getBuildingSnapMode` / `RotationMode` / `ValidateRule` | 策略名 |
| `buildingHasTag` / `getBuildingExtra` / `getBuildingCost` | 标签与扩展 |
| `newWorld(w,h,cellSize)` / `newGhost()` | 工厂 |
| `hasValidateRule` / `hasSnapRule` | 内置+已注册规则 |
| `clearChangeEvents` / `getChangeEvent*` | 变更事件 poll |

### `PlacementWorld`

| 方法 | 说明 |
|------|------|
| `setOrigin` / `setCellSize` / `worldToCell*` / `cellToWorld*` | 坐标 |
| `fillTerrain` / `setTerrain` / `getTerrain` | 地形语义 |
| `canPlace` / `canPlaceReason` / `placeAt` / `placeAtWorld` / `placeGhost` | 放置 |
| `removeBuilding` / `moveBuilding` / `clearBuildings` | 拆除与移动 |
| `getOccupant` / `isCellEmpty` / `getBuilding*` | 查询 |

### `Ghost`

| 方法 | 说明 |
|------|------|
| `setBuildingId` / `setCell` / `setFromWorld` / `setRotationDeg` / `rotateBy` | 姿态 |
| `validate(world)` / `isValid` / `getReason` | 校验结果 |

## 生命周期注意

- 将 `PlacementWorld` / `Ghost` 保存在全局或实体组件中；不要每帧 `newWorld`。
- 变更事件不会自动清空；批量操作后调用 `clearChangeEvents` 或按帧 poll。
- 自定义 validate/snap/hook 只能在 C++ 注册；脚本通过字符串策略名选用。
- 模块**不**扣资源、不画鬼影；`cost` 仅作数据提示，由游戏在 hook 或脚本中处理。

## 多样式网格 / 3D / Tilemap（2026-08-18 起）

坐标换算统一走 `grid` 模块（纯数学），`PlacementWorld` 缺省为正交 2D，旧脚本零改动。

### 网格布局

```squirrel
world.setGridLayout("isometric");          // rectangle | hexagon | isometric | staggered | isometric-z-as-y
world.setGridPlane("xz");                  // xy（默认）| xz（3D：第二轴 -> 世界 Z）
world.setCellGap(2.0, 2.0);
world.setStagger("y", "odd");
world.setHexSideLength(14.0);
```

### 绑定 Tilemap（复用投影 + GID 地形）

```squirrel
world.bindTileLayer(layer);                // 同时复用 layer 的 orientation/stagger/hex
world.setTerrainGid(2, 2);                 // GID 2 -> 地形语义 2（水域）
world.setTerrainGidMapJson("{\"1\":1,\"2\":2}");  // 或整表
```

绑定后 `getTerrain` 从 GID 懒解析；`setTerrain` 手动覆盖优先。

### 3D 放置（XZ 平面 + 表面接口）

```squirrel
world.setGridPlane("xz");
ghost.setFromSurface(world, "plane", hitX, hitZ);   // 内置平面表面；高度 setPlaneSurfaceHeight
ghost.setFromWorld3D(world, worldX, worldY, worldZ); // 直接喂真实世界坐标
world.placeAtWorld3D("house", x, y, z, rot);
```

物理射线 / 高度场等表面由游戏在 C++ 侧 `registerSurface` 实现；引擎只提供接口 + `plane`。

### 通道（同格叠放）

定义 `"channel":"floor"` / `"channel":"furniture"`，同一格允许跨通道建筑并存；
`getOccupantInChannel(channel, x, y)` / `isCellEmptyInChannel` 按通道查询。

### 视觉双形态与渲染桥（`eve.BuildingFx`）

```squirrel
fx <- eve.BuildingFx();
fx.attach(world);
fx.sync(world);                        // 按定义 renderMode/visual2d/visual3d 生成/同步/销毁视觉
fx.updateGhost(world, ghost);          // 每帧：鬼影 + 占地光标（绿/红）
fx.setGridVisible(world, true);
fx.drawGrid2D(world, gfx);             // 2D 网格叠加（渲染循环中调用）
fx.drawGrid3D(world, gfx, 0.01);       // 3D 平面线框
```

### 放置会话（`eve.Building.newSession`）

```squirrel
session <- building.newSession();
session.startPlacement(world, "house");
session.updateFromSurface(world, "plane", hitX, hitZ);  // 或 updateFromWorld / updateFromWorld3D
session.setMode("remove");
session.execute();                     // place 放置 / remove 拆除
```
