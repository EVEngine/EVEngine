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
ui.setItemSelected(true); // 当前工具使用主题激活色
ui.iconButton("move", "", "move");
ui.iconButton("paint-brush", "Paint", "paint");
ui.end();
```

`setItemSelected(true)` 为刚加入的普通按钮或图标按钮设置选中外观；工具切换后用既有的
`setChecked(id, selected)` 更新已挂载控件。选中态只是 UI 投影，工具的权威状态仍由
`EditorToolbar` 或具体编辑器 session 持有。

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
- `requestFocus()`、`setEnabled()`、`setFlexAlign()`、`setFlexJustify()`、`setHostAnchor()`、`setHostLayer()`、`setHostModal()`、`setHostMovable()`、`setHostOverlay()`、`setHostOverlayAlpha()`、`setHostPercent()`、`setHostPos()`、`setHostResizable()`、`setHostSize()`、`setHostVisible()`、`setHostWorldAnchor()`、`clearHostWorldAnchor()`、`setHostWorldEdgePolicy()`、`setHostWorldDistanceScale()`、`setHostWorldOverlap()`、`getHostWorldState()`、`getHostWorldScreenX()`、`getHostWorldScreenY()`、`setImageCornerRadius()`、`setImageNinePatch()`、`setImageTint()`、`setImageUv()`、`setItemAbsolute()`、`setItemAccessibility()`、`setItemDragSource()`、`setItemDropTarget()`、`setItemEnabled()`、`setItemSelected()`、`setItemFlexGrow()`、`setItemFocusMode()`、`setItemFocusNeighbors()`、`setItemFocusOrder()`、`setItemMargin()`、`setItemMaxSize()`、`setItemMinSize()`、`setItemMouseFilter()`、`setItemPadding()`、`setItemPercent()`、`setItemSize()`、`setItemTabIndex()`、`setItemTheme()`、`setItemTooltip()`、`setNavGamepad()`、`setNavKeyboard()`、`setScale()`、`setText()`
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

## Canvas 高度驱动的父布局

`ParentScalerSettings`、`ParentScalerInput`、`ParentScalerState`、`ParentScalerOutput` 和
`evaluateParentScaler(state,output,settings,input)` 对应 Pcg `ParentScaler`。`scaleWithCanvas=true` 且存在
Canvas 和目标时，FullScreen 输出原始 `canvasHeight`；`partScreen=true` 时按 Pcg 公式将其钳制到
`[0.1,maxHeight]`。只有 Canvas 高度变化或 `lastScaleHeight==0` 时才令 `applyHeight=true`，调用者再把
`height` 应用到每个仍有效的目标 Rect，保留各自宽度。

脚本字段包括 `scaleWithCanvas`、`partScreen`、`maxHeight`、`hasCanvas`、`targetCount`、`canvasHeight`、
`lastScaleHeight`、`applyHeight` 和 `height`。状态与输出由调用者持有；零目标不会消耗高度变化，后续加入目标
仍能获得写入。无效目标由调用者跳过，负目标数或非有限高度在修改 state/output 前失败。

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/ui/`](../../../src/modules/ui/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `ui`。

PhotoMode 的 Photo 域由 `PcgPhotoModePhotoAuthority` 保存并统一发布。它覆盖场景身份、
截图分辨率、EXR/JPG/PNG/TGA 格式、加载保存设置、关闭时恢复，以及 FPS、准星和三分法
叠层开关。分辨率索引 0 使用当前 drawable 尺寸，1..9 精确映射 Pcg 的 640×480 到
7680×4320 档位；`screenshotSize()` 与 `screenshotExtension()` 供捕获和 UI 调用方读取。
authority 只保存稳定值，不持有 widget；实际叠层仍由 UIHost 使用现有 UI API 构建。

### Pcg 截图按钮与颜色预览同步

`AutoAssignTakePhotoEvent` 的启动期按钮接线使用现有 `ui.onClick(id, callback)`：回调中调用 `gfx.saveFramePng(path)`；首次启用 readback 后若尚无已呈现帧会返回 `false`，下一帧再次调用。该组合对应 Unity `Button.onClick -> ScreenShotter.TakeHiResShot`，并允许调用者显式选择输出路径。

`eve.PcgColorPreviewSync()` 对应 `ColorPreviewSync.OnEnable`。调用 `sync(r,g,b,a)` 后，通过 `getRed()`、`getGreen()`、`getBlue()`、`getAlpha()` 取得按钮 highlighted color：纯黑源色映射到 `(0.5,0.5,0.5,1)`，其余颜色严格乘以原脚本硬编码的 `2.5`。非有限输入返回结构化错误且保留原状态。

### PcgDraggableWindow

`eve.PcgDraggableWindow()` 对应照片模式的 `DraggableUIWindow`。`configure(x,y,width,height,screenWidth,screenHeight,canvasScale)` 保存初始 anchored position；`drag(deltaX,deltaY)` 按 canvas scale 换算并复现 Pcg 的 X 负向、Y 正向边界钳制。`pointerDown(middleButtonDown)` 总是产生置顶请求，中键同时复位；用 `consumeBringToFront()` 消费请求，并通过 `getX()` / `getY()` 将结果交给 `ui.setHostPos`。

### PcgPhotoModePanels

`eve.PcgPhotoModePanels()` 保留七个 Pcg 照片模式页签的固定编号：Camera=0、Unity=1、Terrain=2、Lighting=3、Water=4、PostFX=5、PhotoMode=6。`select(panel)` 原子关闭其余六个面板并选择目标；`getSelected()`、`isActive(panel)`、`getRevision()` 提供快照，`consumeScrollReset()` 通知调用者重建 scroll content 并把滚动条置顶。

`eve.PcgPhotoModePanelButton()` 通过 `configure(nonSelectedRGBA,selectedRGBA)` 保存两组颜色，`setSelected()` 同时控制按钮 normal/selected 色和面板布局开关。读取接口为 `getNormalR()`、`getNormalG()`、`getNormalB()`、`getNormalA()`、`getHighlightR()`、`getLayoutEnabled()`、`getSelected()`；highlight 始终来自选中色，与 Pcg 注册逻辑一致。

### PcgPhotoModeColorPicker

`eve.PcgPhotoModeColorPicker()` 实现照片模式颜色选择器。`open(r,g,b,a,hdr)`、`setLast(...)`、`setFocusedName(name)`、`close()` 管理打开、旧值和标题；`setRed()`、`setGreen()`、`setBlue()`、`setHdr()` 对应四条滑杆，`reset()` 恢复旧值。读取接口包括 `getRed/Green/Blue/Alpha`、`getPreviewRed/Green/Blue`、`getHdrSwatch()`、`getHdrEnabled()`、`getOpen()`、`getFocusedName()`、`getChangeRevision()` 和 `consumeBringToFront()`。HDR alpha 大于 1 或小于 0 时严格保留 Pcg 打开阶段和预览阶段的两次 RGB 乘法。
预览通道的完整方法名为 `getPreviewRed()`、`getPreviewGreen()`、`getPreviewBlue()`。

### PcgScreenshotSavedNotice

`eve.PcgScreenshotSavedNotice()` 对应截图保存提示。`configure(enabled,showSeconds)` 设置开关和持续时间；截图成功后调用 `request(path)`，在该帧渲染结束调用 `endFrame(unscaledNow)` 才显示，再用 `tick(unscaledNow)` 到期隐藏。`getVisible()`、`getPathVisible()`、`getPath()`、`getPending()` 返回 UI 快照。重复 request 会取消旧显示并从新的 end-of-frame 重新计时。

### PcgTooltipManager

`eve.PcgTooltipManager()` 合并移植 Tooltip、TooltipManager、TooltipProfile 和 TooltipTrigger。`configure(interactionMode,theme,delay,bottomOffset,topOffset,wrapLimit)` 中 interactionMode 为 UI=0、SceneObjects=1、Both=2，theme 为 Light=0、Dark=1。`addTooltip()`、`removeTooltip()` 管理 profile；`enter(source,id,now)` 做 Pcg 的首个 substring 匹配，`enterText(source,content,header,now)` 对应 Trigger 自带文本，`exit(source)`、`hide()` 和 `tick(now,mouseX,mouseY,screenWidth,screenHeight,anyInput)` 管理延迟、位置与隐藏。

状态通过 `getVisible()`、`getPending()`、`getHeader()`、`getText()`、`getHeaderVisible()`、`getWrapEnabled()`、`getPivotX()`、`getPivotY()`、`getBackgroundR()`、`getForeground()`、`getCount()` 读取。

### PcgControllerSelection

`eve.PcgControllerSelection()` 对应 `UIControllerSelection`。用 `add(name,controllerType,widgetId)` 按 Inspector 顺序登记说明项，`refresh(currentController)` 选择第一个类型匹配项并生成互斥可见快照；没有匹配时返回成功但 value=false，保留之前状态。通过 `getCount()`、`getSelectedIndex()`、`getVisible(index)`、`getWidgetId(index)` 读取，再用 `ui.setVisible` 应用到稳定 widget id。

### PcgPhotoModeRuntimeUI

`eve.PcgPhotoModeRuntimeUI()` 对应 `PhotoModeUtils` 与 `PhotoModeUIHelper` 的运行时控件状态。`configure(kind,name,value,current,min,max,imageFound)` 的 kind 从 Field=0 到 Vector3=13。各 `get*Visible()` 返回原 prefab 的互斥可见快照，`getInitialCallbackRevision()` 记录配置后主动派发初值的次数。

### PcgLoadingScreen

`eve.PcgLoadingScreen()` 接收 TerrainLoader 的四类加载事件。`configure(fadeOutSpeed)` 设置背景每秒
衰减的 alpha；`onLoadProgressStarted()` 显示画布并把背景和进度复位，
`onLoadProgressUpdated(progress)` 更新进度且在值达到 1 时自动执行 `onLoadProgressEnded()`。
结束会隐藏进度条和文本并开始淡出，调用方每帧用 `tick(deltaTime)` 推进；alpha 到零后画布关闭。

超时诊断使用 `beginTimeout()` 开始事务，`addMissingScene(terrainName,impostorName)` 返回索引，随后用
`addRegularReference(index,name)` 和 `addImpostorReference(index,name)` 收集仍引用该场景的对象，最后
`endTimeout()` 原子发布与 Pcg 相同结构的诊断并开始淡出。`getCanvasVisible`、`getProgressVisible`、
`getTextVisible`、`getFading`、`getProgress`、`getBackgroundAlpha` 和 `getTimeoutMessage` 返回 UI 快照。

`markSliderUsed()`、`setSliderValue()`、`applyFloatInput()` 保留滑杆与文本框同步规则；`updateWrap(text)` 复现 68 字符 metrics 换行，包括无空格硬切时跳过切点字符的原始行为。

状态查询包括 `getKind()`、`getName()`、`getValueText()`、`getValue()`、`getMinimum()`、`getMaximum()`、
`getWholeNumbers()`、`getUsingSlider()`、`getInputRefresh()`、`getLabelVisible()`、
`getSecondLabelVisible()`、`getSliderVisible()`、`getInputVisible()`、`getToggleVisible()`、
`getButtonVisible()`、`getDropdownVisible()`、`getImageVisible()`、`getHeaderVisible()`、`getColorVisible()`、
`getVector2Visible()`、`getVector3Visible()` 和 `getInitialCallbackRevision()`。

### PcgPhotoModeValues

`eve.PcgPhotoModeValues()` 完整保存 Pcg `PhotoModeValues` 的 102 个字段及原始默认值。字段名沿用 `m_*` 配置名；`getFieldCount/getFieldName/getFieldType` 提供稳定 schema，类型编号为 Bool=0、Int=1、Float=2、String=3、Color=4。使用对应 `setBool/setInt/setFloat/setString/setColor` 与 `getBool/getInt/getFloat/getString`；颜色先 `selectColor(name)`，再读取 RGBA。`resetDefaults()` 原子恢复原始配置。

`snapshotJson()` 输出 `eve.ui.pcg-photo-mode-values` schema version 1，并包含全部 102 字段；`restoreJson(json)` 要求精确 schema、版本和字段集合，拒绝未知或缺失字段，解析到临时候选对象后一次性交换，因此任何错误都保留当前配置。

### PcgPhotoModeSession

`eve.PcgPhotoModeSession()` 对应 `PhotoMode.cs` 的进入、加载和退出事务。`begin(capturedJson,savedJson,loadSaved,savedEver,currentPipeline,savedPipeline,sceneName,lightingProfile)` 保存进入前快照，仅在管线、场景和光照配置均匹配时加载保存值。`getLoadDecision()` 返回 Current=0、Saved=1、SaveCurrent=2。`replaceWorkingJson()` 原子更新运行值；`end(resetOnDisable,applicationPlaying)` 选择工作值或进入前快照作为 `outputJson()`。

退出后 `getRemovePhotoCameraRequested()` 和 `getUnfreezePlayerRequested()` 始终为真；只有两个退出参数均为真时 `getRestoreRequested()` 为真。调用者按这些请求恢复各权威模块并清理临时对象。

脚本方法：`begin()`、`replaceWorkingJson()`、`end()`、`workingJson()`、`outputJson()`、`getActive()`、`getRestoreRequested()`、`getRemovePhotoCameraRequested()`、`getUnfreezePlayerRequested()`、`getLoadDecision()`、`getRevision()`。

### PcgPhotoModeRanges

`eve.PcgPhotoModeRanges()` 完整保存 `PhotoModeMinAndMaxValues` 的 57 组范围。`getCount/getName` 按原声明顺序枚举；`select(name)` 后用 `getMinimum/getMaximum/getIntegral` 读取。`setRange()` 原子替换自定义范围，整数范围要求整数端点；`clamp(name,value)` 返回钳制结果。`m_densityVolumeFogDistance` 的默认上限保留正无穷。

### Pcg PhotoMode 跨域应用事务

PcgPhotoModeApplyPlan 比较两个完整 PcgPhotoModeValues 快照，按 Pcg 源字段声明顺序输出变更，并把字段路由到 Photo、System、Graphics、Camera、Streaming、Weather、Lighting、Water、PostFx、Terrain、Grass 或 Audio 权威域。executeRegistered() 通过 eve.photo-mode.apply capability 同步执行；未安装提供者会返回可观察的 Unsupported 错误。任一字段失败时，先前字段按逆序恢复，getRollbackComplete() 报告恢复是否完整。脚本可用 compileJson、命令枚举和执行状态来驱动照片模式。

脚本方法：compileJson、executeRegistered、getCommandCount、getCommandField、getCommandDomain、getAppliedCount、getRollbackComplete、getRevision。

照片模式注册执行器会把 m_globalVolume 通过现有 IAudioQuery 写入真实 Audio master volume。其他字段由实现 IPhotoModeFieldSink 的模块 listener 接收；每个字段必须恰有一个提供者，缺失或重复所有者都会使事务失败并触发逆序回滚。

### Pcg Lighting profile 身份

PhotoMode 的 `m_isUsingPcgLighting` 与 `m_selectedPcgLightingProfile` 由 `PcgPhotoModePhotoAuthority` 作为持久化身份联合保存。`lightingProfileMatches(currentProfile)` 同时比较 profile 索引和 Pcg/custom 模式；`PcgPhotoModeSession.begin()` 也使用这两个条件与场景名、渲染管线共同决定是否允许加载旧设置。
