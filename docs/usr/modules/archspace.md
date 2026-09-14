# ArchSpace 建筑空间编辑器

**脚本入口：** `eve.ArchSpace()` / `eve.archspaceEditor`

ArchSpace 是对标 [Pascal Editor](https://github.com/pascalorg/editor) 的**建筑空间文档域**，与 RTS
`building` 放置、以及 `housegen` 程序化拼装完全分开。

引擎提供 UI 无关的：

- 文档模型：`site → building → level → wall/slab/zone/item`，`opening` 挂在墙上；
- 可逆 DomainOperation（`archspace.document.replace.v1`）；
- 高层命令：`archspace.bootstrap.v1`、`archspace.room.create.v1`、
  `archspace.opening.create.v1`、`archspace.item.place.v1`、`archspace.node.delete.v1`；
- Gizmo 叠加与 CPU mesh bake（墙盒 + 楼板扇三角），供任意视口消费。

项目自己组合工具栏、视口与 MCP host；不要把 ArchSpace 写进 `building`。

## 快速用法（C++）

```cpp
eve::archspace_editing::ArchSpaceDocumentTarget target("apartment");
auto boot = target.makeBootstrap("site", "building", "level0");
target.applyDomainOperation(boot.value());
auto room = target.makeCreateRectRoom("level0", "living", "Living", 0, 0, 6, 4);
target.applyDomainOperation(room.value());
auto mesh = target.bakeMesh();  // positions + indices
```

## Agent / MCP

1. `eve_editor_target_create`，`type=archspace`，可选 `siteId` 自动 bootstrap；
2. `eve_editor_execute` 调用上表命令；
3. `eve_editor_inspect` / observe / `eve_screenshot` 验收。

设计说明见 [`docs/dev/2026-09-14-archspace-editor.md`](../dev/2026-09-14-archspace-editor.md)。
