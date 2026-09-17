# ArchSpace 建筑空间编辑器

日期：2026-09-14  
状态：v1 落地（文档模型 + editing target + 自动化命令 + 预览网格）  
参考：[pascalorg/editor](https://github.com/pascalorg/editor)

## 1. 决策

EVEngine **不**把 RTS `building` 放置域扩成建筑 CAD，也不把 `housegen` 程序化拼装当成交互式楼层编辑器。

新增独立领域 **`archspace`（建筑空间）**，对标 Pascal 的局部优先建筑场景模型：

| Pascal | ArchSpace |
|--------|-----------|
| Site / Building / Level | `site` / `building` / `level` |
| Wall / Slab / Zone / Item | `wall` / `slab` / `zone` / `item` |
| Door / Window on wall | `opening`（挂在 `wall` 下） |
| Select / Wall / Zone / Item / Slab tools | `IEditorTool` + 领域命令；宿主自绘工具栏 |
| MCP scene tools | `archspace.*.v1` EditingCommand + `eve_editor_*` |
| Plugin node kinds | v1 内置 kinds；后续用 registry 扩展 |

产品边界与现有编辑器策略一致：引擎提供 **UI 无关的文档、事务、选择、预览与自动化命令**；项目用 `editor` / HUD / MCP host 组合自己的壳。

## 2. 模块切分

```text
archspace            运行时文档与几何（无 UI）
archspace_editing    ArchSpaceDocumentTarget、校验、DomainOperation、命令规划
archspace_editor     Automation target factory、命令注册、脚本 facade
```

依赖方向：`archspace` ← `archspace_editing` ← `archspace_editor` ← 项目宿主。  
`runtime-3d` 可只留 `archspace`；authoring profile 再拉 editing/editor。

明确 **不是**：

- `building`：格子占用 / 鬼影 / 校验策略的经营放置；
- `housegen`：组件库 + 种子请求 → 布局实例；
- `scene`：通用场景树 TRS。

## 3. 文档模型

扁平 `nodes: id → node`，父子用 `parentId` + `children[]`（与 Pascal 相同）。

```text
site
 └── building
      └── level
           ├── wall → opening*
           ├── slab
           ├── zone
           └── item
```

坐标：右手系，**XZ 为楼层平面，Y 向上**，单位米。墙用 level 局部 `start/end`；slab/zone 用多边形；item 用位置+yaw；opening 用墙参数 `t∈[0,1]`、宽高、底标高。

权威状态在 `ArchSpaceDocument` / `ArchSpaceDocumentTarget`。渲染网格是可重建投影（`ArchSpaceMeshBake`），不是第二真相。

## 4. 编辑契约

- 稳定 target id + revision；
- 变更一律 `DomainOperation`（`archspace.document.replace.v1`），带 inverse；
- `createRoom` 等高层 API 一次事务写入墙环 + slab + zone；
- `validate()` 检查父子 kind、重 id、非有限数、退化几何、opening 越界；
- snapshot schema `eve.archspace.document` / version 1；
- gizmo：墙线段、zone 折线、item 点、opening 标记；
- mesh bake：墙挤出盒 + slab 扇形三角化，供任意 viewport 上传。

## 5. 自动化 / Agent

| 命令 id | 作用 |
|---------|------|
| `archspace.bootstrap.v1` | site→building→level 骨架 |
| `archspace.room.create.v1` | 矩形或多边形房间 |
| `archspace.wall.create.v1` | 单段墙 |
| `archspace.opening.create.v1` | 门/窗 |
| `archspace.item.place.v1` | 放置物件 |
| `archspace.node.delete.v1` | 级联删除 |
| `archspace.property.set.v1` | Inspector 属性 |

MCP / Agent 路径：`eve_editor_target_create` type=`archspace` → `eve_editor_execute` / observe → `eve_screenshot`。不新增绕过 editing 事务的旁路写入。

## 6. v1 非目标

- IFC 导入、CSG 开洞真几何、屋顶/楼梯系统、材质槽位库、浏览器 WebGPU 宿主、独立 CLI 发行包。  
这些可在文档模型稳定后按 Pascal 能力增量叠加，且不得回流进 `building`/`housegen`。

## 7. 验收

1. 单元测试：房间创建、opening、属性撤销、非法 snapshot 原子拒绝、mesh bake 三角形数；  
2. `examples/archspace-editor`：脚本组装工作区并打印文档摘要；  
3. architecture contracts / module depgraph / 新增公共 API 的 Result+Doxygen。
