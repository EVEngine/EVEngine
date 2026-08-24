# 声明式 UI模块

**脚本入口：** `eve.UI()`

构建并挂载保留式控件树，通过稳定 ID 消费点击和更改事件。

## 基本用法

```squirrel
ui.beginBuild();
ui.beginWindow("HUD", "root");
ui.text("Ready", "status");
ui.button("Start", "start");
ui.end();
ui.mountBuildAs("hud");
```

## 对象关系与调用时机

`UI` 管理多个命名 Host；Host 保存控件树；稳定 ID 标识控件；`UIComponent.build()` 可封装可复用树。事件返回完整 host/id 路径，修改接口在当前 selected host 中查找。

## 目标导向指南

### 创建可交互 HUD

`beginBuild()` 后用 Window/Flex/Group/List 组织控件，给每个交互控件稳定 ID，完成后 `mountBuildAs("hud")`。更新阶段循环 `consumeClick()` / `consumeChange()`，渲染阶段调用 `beginFrameAndRender()`。

### 使用弹性布局（Flex）

`beginRow` / `beginColumn` / `beginFlex("row"|"column", id, gap)` 自动排列子控件，无需手写 `sameLine`。`spacer()` 吸收剩余空间；`setItemFlexGrow` / `setItemSize` 作用在刚添加的子项上；`setFlexAlign` / `setFlexJustify` 配置当前 Flex 容器。

```squirrel
ui.beginRow("toolbar", 8.0);
ui.setFlexJustify("space-between");
ui.iconButton("save", "Save", "save");
ui.spacer("sp");
ui.button("Quit", "quit");
ui.end();
```

桌面框架可用 `beginToolbar`、`beginToolbox`、`beginSidebar`、`beginStatusBar` 和
`beginSplitPane("row"|"column", ratio, id)` 直接组合。SplitPane 必须包含两个直接子项，
拖拽分隔条会产生普通 value change；比例可通过 `getValue` / `setValue` 读取和恢复。
独立面板可用 `setHostMovable(true)` / `setHostResizable(true)` 允许用户调整；ImGui 会按
稳定 Host ID 自动写入和恢复 ini 布局。无标题 Overlay 的不透明度由
`setHostOverlayAlpha(0..1)` 控制。

```squirrel
ui.beginToolbar("toolbar");
ui.iconButton("save", "", "save");
ui.spacer("toolbar-fill");
ui.badge("Ready", "ready");
ui.end();

ui.beginSplitPane("row", 0.25, "workspace");
ui.beginSidebar("left", 240.0);
ui.searchField("Search tools", "", "tool-search");
ui.beginToolbox("tools", 40.0, 3);
ui.iconButton("pointer", "", "select");
ui.iconButton("move", "", "move");
ui.iconButton("paint-brush", "", "paint");
ui.end();
ui.end();
ui.beginCard("content");
ui.sectionHeader("Inspector", "inspector");
ui.end();
ui.end();
```

### 使用内置编辑器图标

`icon(name, id)` 显示语义图标，`iconButton(name, label, id)` 创建图标按钮。图标字体随
引擎构建和 SDK 安装，不依赖游戏工作目录。名称不区分大小写，空格和下划线会转成
连字符；常用名称包括 `search`、`settings`、`save`、`undo`、`redo`、`folder-open`、
`pointer`、`move`、`paint-brush`、`database`、`layers`、`play` 和 `warning`。

```squirrel
ui.beginRow("tools", 4.0);
ui.iconButton("pointer", "", "select");
ui.iconButton("move", "", "move");
ui.iconButton("paint-brush", "Paint", "paint");
ui.end();
```

### 组合桌面编辑器控件

Editor 常用控件已内置：`searchField`、`switch`、`badge`、`sectionHeader`、`beginCard`、
`beginMenuBar` / `beginMenu` / `menuItem`。`setItemTooltip` 为刚添加的控件设置悬停说明。
菜单栏应作为 Window 的直接子项；所有可交互控件都应使用稳定 ID。

```squirrel
ui.beginMenuBar("main-menu");
ui.beginMenu("File", "file");
ui.menuItem("Save", "Ctrl+S", "save");
ui.end();
ui.end();
ui.searchField("Search assets", "", "asset-search");
ui.setItemTooltip("Filter project assets");
ui.sectionHeader("Properties", "properties");
ui.beginCard("selection-card");
ui.switch("Visible", true, "visible");
ui.badge("Modified", "state");
ui.end();
```

### 更新而不重建整个 UI

文本变化用 `setText(id, value)`，进度用 `setValue()`，显示隐藏用 `setVisible()`；结构变化才重新 build 并 `remountBuildAs()`。多宿主时先 `select(host)` 再按局部 ID 操作。

### 切换统一主题

内置 `dark` / `light` 预设共享圆角、边框、间距与字体缩放，只切换配色。推荐 `setTheme("dark")` / `setTheme("light")`，用 `getTheme()` 读取当前名；也可用 `setThemeDark()` / `setThemeLight()`。DPI 用 `setScale()`，主题几何会按比例缩放。

## 常见问题

- 每帧重新 mount，丢失输入焦点与控件状态。
- 多个控件使用同一 ID。
- 未循环消费全部 click/change，队列在后续帧才清空。

## API 快查

下列方法名来自当前 Squirrel 绑定；同一模块创建的辅助对象（例如 `World`、`Body`、`Source`）的方法也列在这里。

- `beginBuild()`、`beginCard()`、`beginChild()`、`beginCollapsing()`、`beginColumn()`、`beginFlex()`、`beginFrameAndRender()`、`beginGroup()`、`beginList()`、`beginMenu()`、`beginMenuBar()`、`beginRow()`、`beginSidebar()`、`beginSplitPane()`、`beginStatusBar()`、`beginScrollList()`、`beginToolbar()`、`beginToolbox()`、`beginWindow()`、`bindOwner()`
- `animateHostPos()`、`badge()`、`button()`、`checkbox()`、`combo()`、`consumeChange()`、`consumeClick()`、`dispatchEvents()`、`end()`、`getChecked()`、`getName()`
- `getScale()`、`getTheme()`、`getValue()`、`getValueText()`、`icon()`、`iconButton()`、`initBackend()`、`inputText()`、`isBackendReady()`、`listItem()`、`mountBuild()`
- `menuItem()`、`mountBuildAs()`、`mountSimple()`、`progress()`、`remountBuildAs()`、`sameLine()`、`searchField()`、`sectionHeader()`、`select()`、`separator()`、`setChecked()`
- `setFlexAlign()`、`setFlexJustify()`、`setHostAnchor()`、`setHostLayer()`、`setHostModal()`、`setHostMovable()`、`setHostOverlay()`、`setHostOverlayAlpha()`、`setHostPercent()`、`setHostPos()`、`setHostResizable()`、`setHostSize()`、`setHostVisible()`、`setImageCornerRadius()`、`setImageNinePatch()`、`setImageTint()`、`setImageUv()`、`setItemAbsolute()`、`setItemFlexGrow()`、`setItemMargin()`、`setItemMaxSize()`、`setItemMinSize()`、`setItemPadding()`、`setItemPercent()`、`setItemSize()`、`setItemTooltip()`、`setNavGamepad()`、`setNavKeyboard()`、`setScale()`、`setText()`
- `setTextWrap()`、`setTheme()`、`setThemeDark()`、`setThemeLight()`、`setValue()`、`setValueText()`、`setVisible()`、`slider()`、`spacer()`、`switch()`、`text()`、`textWrapped()`、`wantCaptureKeyboard()`
- `wantCaptureMouse()`
- `image()`、`imageButton()`、`onClick()`、`onChange()`、`saveTreeJson()`、`loadTreeJson()`、`getStats()`
- `viewport()`、`viewportCanvas()`、`viewportHovered()`、`viewportActive()`、`viewportMouseX()`、`viewportMouseY()`、`viewportDragDX()`、`viewportDragDY()`、`viewportWheel()`

## 内嵌渲染视口（Viewport）

`ui.viewport(id, w, h)` 声明一个内嵌渲染目标控件：它维护一个离屏 `Canvas`
（尺寸跟随控件矩形），游戏在 `eve_render` 里先把 2D（`gfx.setCanvas` + 立即模式绘制）
或 3D（`gfx.renderScene3DToCanvas(canvas, camera)`）渲染进去，然后
`ui.beginFrameAndRender()` 会把该 Canvas 纹理显示在控件中。视口交互输入
（悬停、按住、控件本地鼠标坐标、拖拽增量、滚轮）通过 `viewportHovered/Active/MouseX/
MouseY/DragDX/DragDY/Wheel(id)` 每帧读取。完整示例见
`examples/terrain-editor`（高度图地形 + orbit 相机 + 抬高/压低笔刷）。

## MCP EditorHost 脚本接口

`eve mcp` 会在根表建立 `eve.host`，供项目脚本创建和维护 AI 编辑器。它与
`eve.UI()` 的游戏内 retained UI 相互独立，但都运行在同一个 Squirrel VM 中。

- 窗口与状态：`status()`、`openWindow()`、`closeWindow()`、`windowState()`。
- View 与交互：`applyEditor()`、`removeEditor()`、`setValue()`、`events()`、
  `widgetRect()`、`capture()`、`save()`。
- ViewModel：`registerVM()`、`unregisterVM()`、`runScript()`。
- 热更新：`reloadResource(path)` 手动重载项目内的 `mcp.nut`、`mcp/*.nut`、
  `editors/*.vm.nut` 或 `editors/*.editor.json`；`hotReloadStatus()` 返回 watcher、
  成功/失败计数与最近诊断。正常保存会由 MCP 主机自动触发，无需重启。

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/ui/`](../../../src/modules/ui/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `ui`。
