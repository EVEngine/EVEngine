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

## 自定义组件

脚本组件拥有实例级 `props` 与 `state` 表；`setProps(table)` / `setState(table)` 会合并
变更并标记脏状态，`updateIfDirty()` 通过稳定 ID 协调旧树。持久化的子组件实例可用
`renderChild(child, props)` 嵌入，子组件 `markDirty()` 会自动向父组件传播。首次挂载和后续
重建分别调用 `onMount()`、`onUpdated()`。

```squirrel
class Label extends eve.UIComponent {
    function build() { ui().text(props.text, "label") }
}
class Counter extends eve.UIComponent {
    label = null
    constructor(u) {
        base.constructor(u, { title = "Counter" })
        state.value <- 0
        label = Label(u)
    }
    function build() {
        local u = ui()
        u.beginWindow(props.title, "root")
        renderChild(label, { text = "Value " + state.value })
        u.button("+1", "increment")
        u.end()
    }
}
```

## `.9.png` 九宫格资源

支持未经 Android 编译的标准 `.9.png`：顶边和左边各一段连续黑色像素定义可拉伸区，
底边和右边可选黑线定义内容区。加载时会移除四周 1px 标记框并缓存纹理。

```squirrel
ui.beginBuild()
ui.beginWindow("Nine patch", "root")
if (ui.beginNinePatch("assets/panel.9.png", "panel", 320, 0)) {
    ui.text("内容会自动使用 .9.png 声明的 padding", "content")
    ui.end()
}
ui.ninePatch("assets/button.9.png", "preview", 180, 48)
ui.end()
ui.mountBuildAs("nine-patch-demo")
```

`ninePatch(path,id,w,h)` 创建叶子图片；`beginNinePatch(path,id,w,h)` 创建可容纳子控件的
面板；`setImageNinePatchFile(id,path)` 可替换已挂载图片。当前解析器有意只接受单段拉伸
区（与引擎单中心九宫格模型一致），多个不连续拉伸段会返回 `false` 并输出诊断。

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

Editor 常用控件已内置：`searchField`、`switch`、`badge`、`colorPalette`、`sectionHeader`、`beginCard`、
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
local addSwitch = ui["switch"].bindenv(ui); // `switch` 是 Squirrel 关键字
addSwitch("Visible", true, "visible");
ui.badge("Modified", "state");
ui.colorPalette("Accent", 0.18, 0.42, 0.86, 1.0, "accent");
ui.end();
```

`colorPalette(label, r, g, b, a, id)` 显示颜色编辑器与色板网格。当前颜色存在节点 tint 上，用
`getColorR/G/B/A` / `setColor` 读写；拖动或点选色块走普通 `consumeChange()`。
`setValueText(id, "#ff0000;#00ff00")` 可换成自定义色板（`#rrggbb` / `#rrggbbaa` 或 `r,g,b[,a]`，
分号或换行分隔）；留空则使用内置 16 色。

### 用 Theme 统一布局

Theme 除颜色和 ImGui 基础几何外，还提供类似 CSS design tokens 的语义布局默认值：
`toolbarHeight`、`statusBarHeight`、`sidebarWidth`、`toolboxCellSize`、
`splitterSize`、`minPaneSize`、`panelPadding`、`cardPadding`、`barPadding`、
`sectionSpacingY`、`searchMinWidth` 和 `searchIconGap`。暗色与亮色主题共享这些布局
参数，因此切换配色不会改变界面结构。

`Toolbar`、`Sidebar`、`Toolbox`、`Card`、`StatusBar`、`SplitPane` 和
`SearchField` 在没有显式尺寸时自动继承 Theme。局部差异继续通过
`setItemSize`、`setItemPadding`、`setItemMargin`、`setItemMinSize` 等接口覆盖，
其优先级高于 Theme 默认值。SplitPane 的直接子节点总是填满获分配的 pane，内部控件
再依据可用宽度响应式重排；Toolbox 的列数是上限，空间不足时会自动减少列数。

### 更新而不重建整个 UI

文本变化用 `setText(id, value)`，进度用 `setValue()`，显示隐藏用 `setVisible()`，
启用状态用 `setEnabled()`；结构变化才重新 build 并 `remountBuildAs()`。多宿主时先
`select(host)` 再按局部 ID 操作。

### 配置焦点、输入策略与无障碍语义

构建树时，`setItemFocusMode("none"|"click"|"all")`、
`setItemMouseFilter("stop"|"pass"|"ignore")` 与 `setItemTabIndex(index)` 作用于刚添加的
控件。`setItemFocusOrder(previous, next)` 配置顺序导航，
`setItemFocusNeighbors(left, right, up, down)` 配置方向导航；空字符串表示继续使用稳定的
tabIndex/树顺序。`setItemEnabled(false)` 会同时禁止交互与焦点。

指针点击在保留树中按 `MouseFilter` 路由：`stop` 在当前控件处理后截断，`pass` 处理后继续
向祖先冒泡，`ignore` 不处理但允许继续向祖先传递。轮询队列仍只记录原始目标一次，祖先
通过各自的 `onClick` 处理器观察同一事件，避免脚本收到重复目标。

`setItemAccessibility(role, name, description)` 为控件附加语义角色、可读名称和说明。
角色可用 `button`、`checkbox`、`combobox`、`textbox`、`slider`、`menuitem`、
`progressbar`、`image`、`heading`、`status`、`region` 或 `generic`。

挂载后可用 `requestFocus(id)` 请求焦点，或用
`moveFocus("next"|"previous"|"left"|"right"|"up"|"down")` 导航；
`getFocusedId()` 返回当前焦点的稳定 ID。键盘 Tab、Shift+Tab 和方向键会走同一套显式邻居
与顺序规则，游戏手柄或自定义输入层可直接调用 `moveFocus()`。

```squirrel
ui.searchField("Search", "", "search");
ui.setItemTabIndex(0);
ui.setItemFocusNeighbors("", "save", "", "");
ui.setItemAccessibility("textbox", "Search scene", "Filter scene nodes");

ui.iconButton("save", "", "save");
ui.setItemTabIndex(1);
ui.setItemFocusOrder("search", "");
ui.setItemAccessibility("button", "Save scene", "");
```

### 切换统一主题

内置 `dark` / `light` 预设共享圆角、边框、间距与字体缩放，只切换配色。推荐 `setTheme("dark")` / `setTheme("light")`，用 `getTheme()` 读取当前名；也可用 `setThemeDark()` / `setThemeLight()`。DPI 用 `setScale()`，主题几何会按比例缩放。

需要在同一界面中区分编辑器面板、游戏 HUD 或嵌入工具时，在添加容器后调用
`setItemTheme("dark"|"light"|"inherit")`。主题覆盖作用于该容器及其整个子树，嵌套容器
可以再次覆盖；离开子树后自动恢复外层主题。该设置随 UI JSON 资产保存和加载。
对于刚通过 `beginWindow`、`beginCard`、`beginGroup` 等打开且仍在构建的当前容器，使用
`setThemeScope()`；这样根 Window 也能拥有独立于全局预设的主题。

## 常见问题

- 每帧重新 mount，丢失输入焦点与控件状态。
- 多个控件使用同一 ID。
- 未循环消费全部 click/change，队列在后续帧才清空。

## API 快查

下列方法名来自当前 Squirrel 绑定；同一模块创建的辅助对象（例如 `World`、`Body`、`Source`）的方法也列在这里。

- `beginBuild()`、`beginCard()`、`beginChild()`、`beginCollapsing()`、`beginColumn()`、`beginFlex()`、`beginFrameAndRender()`、`beginGroup()`、`beginList()`、`beginMenu()`、`beginMenuBar()`、`beginNinePatch()`、`beginRow()`、`beginSidebar()`、`beginSplitPane()`、`beginStatusBar()`、`beginScrollList()`、`beginToolbar()`、`beginToolbox()`、`beginWindow()`、`bindOwner()`
- `animateHostPos()`、`badge()`、`button()`、`checkbox()`、`colorPalette()`、`combo()`、`consumeChange()`、`consumeClick()`、`consumeDrop()`、`dispatchEvents()`、`dragDropSupport()`、`end()`、`getChecked()`、`getColorA()`、`getColorB()`、`getColorG()`、`getColorR()`、`getDropOrigin()`、`getDropSource()`、`getDropText()`、`getDropType()`、`getName()`
- `getFocusedId()`、`getScale()`、`getTheme()`、`getValue()`、`getValueText()`、`icon()`、`iconButton()`、`initBackend()`、`inputText()`、`isBackendReady()`、`listItem()`、`mountBuild()`、`moveFocus()`
- `menuItem()`、`mountBuildAs()`、`mountSimple()`、`progress()`、`remountBuildAs()`、`sameLine()`、`searchField()`、`sectionHeader()`、`select()`、`separator()`、`setChecked()`
- `requestFocus()`、`setEnabled()`、`setFlexAlign()`、`setFlexJustify()`、`setHostAnchor()`、`setHostLayer()`、`setHostModal()`、`setHostMovable()`、`setHostOverlay()`、`setHostOverlayAlpha()`、`setHostPercent()`、`setHostPos()`、`setHostResizable()`、`setHostSize()`、`setHostVisible()`、`setHostWorldAnchor()`、`clearHostWorldAnchor()`、`setHostWorldEdgePolicy()`、`setHostWorldDistanceScale()`、`setHostWorldOverlap()`、`getHostWorldState()`、`getHostWorldScreenX()`、`getHostWorldScreenY()`、`setImageCornerRadius()`、`setImageNinePatch()`、`setImageTint()`、`setImageUv()`、`setItemAbsolute()`、`setItemAccessibility()`、`setItemDragSource()`、`setItemDropTarget()`、`setItemEnabled()`、`setItemFlexGrow()`、`setItemFocusMode()`、`setItemFocusNeighbors()`、`setItemFocusOrder()`、`setItemMargin()`、`setItemMaxSize()`、`setItemMinSize()`、`setItemMouseFilter()`、`setItemPadding()`、`setItemPercent()`、`setItemSize()`、`setItemTabIndex()`、`setItemTheme()`、`setItemTooltip()`、`setNavGamepad()`、`setNavKeyboard()`、`setScale()`、`setText()`
- `setTextWrap()`、`setTheme()`、`setThemeDark()`、`setThemeLight()`、`setThemeScope()`、`setColor()`、`setValue()`、`setValueText()`、`setVisible()`、`slider()`、`spacer()`、`switch()`、`text()`、`textWrapped()`、`wantCaptureKeyboard()`
- `wantCaptureMouse()`、`registerTexture()`、`unregisterTexture()`、`setImageTextureId()`、`setImageNinePatchFile()`
- `image()`、`imageButton()`、`ninePatch()`、`onClick()`、`onChange()`、`saveTreeJson()`、`loadTreeJson()`、`getStats()`
- `viewport()`、`viewportCanvas()`、`viewportHovered()`、`viewportActive()`、`viewportMouseX()`、`viewportMouseY()`、`viewportDragDX()`、`viewportDragDY()`、`viewportWheel()`

## 引擎纹理控件

`image()` 和 `imageButton()` 可以显示任意引擎 `Texture`。先用
`textureId = ui.registerTexture(texture)` 获得后端中立句柄，再用
`ui.setImageTextureId(id, textureId)` 绑定到当前 Host 的控件。纹理不再使用时，先把相关
控件设为 `0`，再调用 `ui.unregisterTexture(textureId)`；这样 Vulkan 与 WebGPU 后端都能
释放对应资源。UV、色调、九宫格与圆角仍通过 `setImageUv/Tint/NinePatch/CornerRadius` 组合。

## 桌面 Drag & Drop

Drag & Drop 仅在 Windows、macOS、Linux 桌面构建启用；Android、iOS 和 Web/WASM 的
`dragDropSupport()` 返回 `unsupported-platform`，不会伪装成功。`setItemDragSource(type,
text)` 和 `setItemDropTarget(type)` 作用于刚添加且会产生交互 item 的控件；目标类型可用
`*` 接受全部内部 payload，`file` 接受操作系统文件拖入。

```squirrel
ui.button("Stone", "stone")
ui.setItemDragSource("asset", "textures/stone.png")
ui.viewport("scene", 640, 360)
ui.setItemDropTarget("asset")

local target = ui.consumeDrop()
if (target != "") {
    local payloadType = ui.getDropType()
    local payloadText = ui.getDropText()
    local source = ui.getDropSource() // OS 文件为空
    local origin = ui.getDropOrigin() // internal / os-file
}
```

事件保存 owning UTF-8 文本快照，源节点或目标节点在消费前销毁不会留下悬空引用。

## 内嵌渲染视口（Viewport）

`ui.viewport(id, w, h)` 声明一个内嵌渲染目标控件：它维护一个离屏 `Canvas`
（尺寸跟随控件矩形），游戏在 `eve_render` 里先把 2D（`gfx.setCanvas` + 立即模式绘制）
或 3D（`gfx.renderScene3DToCanvas(canvas, camera)`）渲染进去，然后
`ui.beginFrameAndRender()` 会把该 Canvas 纹理显示在控件中。视口交互输入
（悬停、按住、控件本地鼠标坐标、拖拽增量、滚轮）通过 `viewportHovered/Active/MouseX/
MouseY/DragDX/DragDY/Wheel(id)` 每帧读取。完整示例见
`examples/terrain-editor`（高度图地形 + orbit 相机 + 抬高/压低笔刷）。

## 3D 世界锚点（World Anchor）

世界锚点把整个 UI Host 投影到当前活动 `Camera3D` 的屏幕位置，适合单位名牌、交互提示、
任务标记和编辑器 3D gizmo 标签。先选择 Host，再调用
`setHostWorldAnchor(x, y, z)`；`clearHostWorldAnchor()` 恢复普通屏幕布局。

`setHostWorldEdgePolicy("hide"|"clamp")` 控制目标离开视口后的行为；
`setHostWorldDistanceScale(enabled, referenceDistance, minScale, maxScale)` 配置按相机距离缩放。
相机不可用或目标在相机背面时 Host 自动隐藏，不会沿用上一帧坐标。世界坐标由游戏场景
持有者负责更新；UI 只保存投影输入和逐帧派生的屏幕状态。当前版本不做深度遮挡判断，
需要被场景几何遮挡的面板应继续使用 3D mesh/material。

大量名牌同时出现时，可用 `setHostWorldOverlap(true, priority, padding,
maxDisplacement)` 开启确定性的屏幕矩形避让。高优先级、近距离 Host 先占位，其余 Host
在限定距离内上下寻找空位；没有可用位置时状态变为 `crowded` 并暂停渲染。
`getHostWorldState()` 返回 `visible`、`behind-camera`、`outside-viewport`、`no-camera`、
`crowded` 或 `disabled`；最终屏幕坐标可通过 `getHostWorldScreenX/Y()` 查询。

```squirrel
ui.select("unit-nameplate")
ui.setHostWorldAnchor(unitX, unitY + 2.0, unitZ)
ui.setHostWorldEdgePolicy("clamp")
ui.setHostWorldDistanceScale(true, 10.0, 0.7, 1.2)
ui.setHostWorldOverlap(true, 10, 4.0, 96.0)
```

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
