# PixelWorld 材料与反应编辑器

`pixelworld_editor` 提供专用的 `PixelWorldCatalogPanel`。它把材料浏览器、颜色预览、
物态与热学参数、标签、二元反应和相变规则组织成通用 `ui::WidgetDesc` 视觉树，可由
桌面 ImGui UI、游戏内编辑器或自动化 host 挂载。

Panel 只拥有经过完整验证的 Catalog draft，不保存 `PixelWorld` 指针，也不成为运行时
材料状态的第二权威来源。材料或规则修改会复制完整 candidate，经
`MaterialCatalog::create()` 验证后一次替换；失败保持 draft、revision 和选择状态不变。
JSON 导入导出复用 `pixelworld:material-catalog` 的 canonical codec。

发布时调用 `apply()`，它通过 `PixelWorldControlService`、world id 和预期 Catalog
fingerprint 进入已有的暂停世界事务热重载路径。世界不存在、未暂停、fingerprint stale、
材料 id/name 不兼容或规则无效都会以结构化 `Result` 拒绝，不会部分修改世界。

调用 `open()` 会创建或复用名为 `eve_pixelworld_catalog` 的生产 UI host，并立即挂载视觉
树；`close()` 只隐藏 host，不丢弃 draft。返回的视觉树包含借用 Panel 的交互 callback，
因此 Panel 析构时会清空并隐藏 host。交互回调接受一次 edit 后自动 reconcile 最新内容；
外部批量修改也可显式调用 `refresh()`。

脚本侧的 `pixelworldEditor.openCatalog()`、`closeCatalog()` 与 `isCatalogOpen()` 提供应用
入口；`examples/pixelworld` 默认打开该面板，便于边运行材料仿真边编辑 Catalog draft。

源码：[`src/modules/pixelworld_editor/`](../../../src/modules/pixelworld_editor/)。
测试：[`test/pixelworld_editor.cpp`](../../../test/pixelworld_editor.cpp)。
