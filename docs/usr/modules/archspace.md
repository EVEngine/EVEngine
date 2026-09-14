# ArchSpace 建筑空间编辑器

**脚本入口：** `eve.ArchSpace()` / `eve.ArchSpaceDocument()` / 模块槽 `archspace`、
`archspaceEditor`

ArchSpace 是对标 [Pascal Editor](https://github.com/pascalorg/editor) 的**建筑空间文档域**，与 RTS
`building` 放置、以及 `housegen` 程序化拼装完全分开。

引擎提供 UI 无关的：

- 文档模型：`site → building → level → wall/slab/zone/item`，`opening` 挂在墙上；
- 可逆 DomainOperation（`archspace.document.replace.v1`）；
- 高层编辑命令：`archspace.bootstrap.v1`、`archspace.room.create.v1`、
  `archspace.wall.create.v1`、`archspace.opening.create.v1`、`archspace.item.place.v1`、
  `archspace.property.set.v1`、`archspace.node.delete.v1`；
- Gizmo 叠加与 CPU mesh bake（墙盒含门窗挖洞 + 楼板扇三角 + 物品标记），供任意视口消费。

项目自己组合工具栏、视口与 MCP host；不要把 ArchSpace 写进 `building`。

## 快速用法（脚本）

```squirrel
local doc = eve.ArchSpaceDocument();
local boot = doc.bootstrap("site", "building", "level0", 3.0);
if (!boot.ok) throw boot.status.summary;

doc.createRectRoom("level0", "living", "Living", 0.0, 0.0, 6.0, 4.5, 3.0, 0.2, 0.2);
doc.createWall("level0", "partition", "Partition", 3.0, 0.0, 3.0, 4.5, 3.0, 0.15);
doc.createOpening("living.wall.0", "living.door", "Entry", "door", 0.45, 1.0, 2.1, 0.0);
doc.placeItem("level0", "desk", "Desk", "furniture.desk", 2.0, 0.0, 2.0, 90.0);

if (!doc.hasNode("living.zone")) throw "missing zone";
print(doc.summary() + " nodes=" + doc.nodeCount() + " root=" + doc.rootId() + "\n");

local arrays = doc.bakeMeshArrays(); // positions / normals / uvs / indices
local mesh = gfx.newMeshFromArrays(arrays.positions, arrays.normals, arrays.uvs,
                                   arrays.vertexCount, arrays.indices, arrays.indexCount);

doc.deleteNode("desk"); // cascading delete when needed
```

完整示例见 `examples/archspace-editor/`。

## 快速用法（C++）

```cpp
eve::archspace_editing::ArchSpaceDocumentTarget target("apartment");
auto boot = target.makeBootstrap("site", "building", "level0");
target.applyDomainOperation(boot.value());
auto room = target.makeCreateRectRoom("level0", "living", "Living", 0, 0, 6, 4);
target.applyDomainOperation(room.value());
auto mesh = target.bakeMesh();  // positions + indices
```

## API 快查

### `ArchSpaceDocument`（脚本类）

- `nodeCount()` — 当前节点数。
- `rootId()` — 站点根 id；空文档返回空串。
- `hasNode(id)` — 节点是否存在。
- `bootstrap(siteId, buildingId, levelId, levelHeight)` → Result；空文档上创建
  site→building→level。
- `createRectRoom(levelId, roomId, name, originX, originZ, sizeX, sizeZ, wallHeight,
  wallThickness, slabThickness)` → Result；矩形房间（四墙 + 楼板 + zone）。
- `createWall(levelId, wallId, name, x0, z0, x1, z1, height, thickness)` → Result。
- `createOpening(wallId, openingId, name, kind, t, width, height, sill)` → Result；
  `kind` 为 `"door"` / `"window"`。
- `placeItem(levelId, itemId, name, catalogId, x, y, z, yawDegrees)` → Result。
- `deleteNode(id)` → Result；级联删除子树。
- `bakeMeshArrays()` — 返回表：`positions`/`normals`/`uvs`/`indices` 以及
  `vertexCount`/`indexCount`/`triangleCount`（门窗挖洞后的上传数组）。
- `summary()` — 人类可读摘要字符串（nodes/root/triangles/vertices）。

所有可能失败的脚本调用返回统一 Result 投影：检查 `ok`，再读 `status.summary` /
`diagnostics`；不要依赖旁路错误字符串。数值参数在脚本侧为 `float`。

## Agent / MCP

1. `eve_editor_target_create`，`type=archspace`，可选 `siteId` 自动 bootstrap；
2. `eve_editor_execute` 调用上表命令；
3. `eve_editor_inspect` / observe / `eve_screenshot` 验收。

设计说明见 [`docs/dev/2026-09-14-archspace-editor.md`](../dev/2026-09-14-archspace-editor.md)。
