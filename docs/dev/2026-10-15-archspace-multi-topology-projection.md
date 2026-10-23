# ArchSpace 多拓扑与多投影

日期：2026-10-15  
状态：落地中（SpaceQuery + Portal + Grid/Mesh + map/hd2d 投影卫星 + 格子导入）  
前置：[`2026-09-14-archspace-editor.md`](./2026-09-14-archspace-editor.md)

## 1. 决策

名称保持 **ArchSpace**。权威文档从「仅户型 CAD」扩展为**通用室内/地下空间语义**，覆盖：

| `TopologyKind` | 典型内容 | Boundary 形态 |
|----------------|----------|---------------|
| `architectural` | 住宅、宫殿 | 米制墙线段 + 墙上 `opening` |
| `dungeon` | 地牢、监牢 | Region（zone）+ 层内 `portal` |
| `cave` | 山洞、溶洞 | Region（腔室）+ 通道 `portal` |

`procgen`（`dungeon.bsp` / `cave.cellular` / `CaveGeneration`）只负责生成；写入 ArchSpace
后，玩法与编辑走同一套 `SpaceQuery`。表现层（2D tile / 斜 45° / HD2D / 3D）只是可重建投影。

明确 **不是**：

- 把洞窟语义塞进 RTS `building`；
- 让 `map` / `hd2d` / `scene` 反向拥有户型真相；
- 用一份体素网格当所有拓扑的唯一存储。

## 2. 共同语义（SpaceQuery）

```text
Region   ← NodeKind::Zone（房间 / 走廊 / 腔室）
Portal   ← NodeKind::Opening（挂墙）或 NodeKind::Portal（层内连通）
Boundary ← NodeKind::Wall / 投影栅格 solid
Anchor   ← NodeKind::Item
Stratum  ← NodeKind::Level
```

查询面（UI / 寻路 / Agent 共用，不依赖 graphics）：

- `topology()` / `strata()` / `regions()` / `regionAt(xz, levelId)`
- `portals()` / `portalsFrom(regionId)` / `regionsConnected(a, b)`
- `anchorsIn(regionId)`

## 3. 投影契约

```text
Document (权威)
  ├── SpaceQuery
  ├── MeshBake / MeshSceneProjector     → 3D / scene
  └── GridProjection bake               → ortho / iso / HD2D 宿主
```

`IArchSpaceProjector`：`kind()` / `sync(doc, revision)` / `clear()` / 可选 `pickNode`。  
`archspace` 模块本身只依赖 `data`；真正绑定 `map` / `hd2d` 的适配器在卫星模块：

| 模块 | `kind()` | 依赖 | 产出 |
|------|----------|------|------|
| `archspace` | `scene3d` + `bakeGridProjection` | `data` | MeshBake / GridProjection |
| `archspace_map` | `tile.ortho` / `tile.iso` | archspace + map | `TileLayer` GIDs + nav |
| `archspace_hd2d` | `hd2d` | archspace_map + hd2d | TileLayer + 可选 `TileMap3D` bake |

`importCellGrid`（core）把 procgen 风格的 Floor/Wall/Portal 栅格写成 Region + Portal，
不依赖 `procgen` / `map`。

## 4. 持久化

`eve.archspace.document` schemaVersion **仍为 1**：新增 `topology`（缺省 `architectural`）与
`portal` 节点字段均为可选；旧快照继续可读。

## 5. 落地切片

1. 已落地：`TopologyKind`、`NodeKind::Portal`、`SpaceQuery`、`IArchSpaceProjector`、
   `MeshSceneProjector`、`bakeGridProjection`、文档/脚本入口、单测。
2. 本切片：`archspace_map`（ortho/iso → TileLayer）、`archspace_hd2d`（TileLayer +
   延迟 `TileMap3D` bake）、`importCellGrid`（Floor 洪水填充 + Portal 桥）。
3. 后续：垂直 portal（楼梯/竖井）、脚本/editor 入口、与 `procgen` 示例对接。

## 6. 验收

- architectural 房间：`regionAt` 命中 zone；墙上 door 进入 `portalsFrom`。
- dungeon/cave：两 region + `Portal` 连通；`regionsConnected` 为真。
- `MeshSceneProjector::sync` 与 `bakeGridProjection` 在空文档与有房间文档上均成功。
- `TileMapProjector` ortho/iso 写出非零 Floor GID；`Hd2dSpaceProjector::sync` 不依赖 Graphics。
- `importCellGrid`：Portal 单元格不合并两侧 Floor 区域，且 `regionsConnected` 为真。
- `make check`；相关 `archspace.*` 单测通过。
