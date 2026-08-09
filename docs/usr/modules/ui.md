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

`beginBuild()` 后用 Window/Group/List 组织控件，给每个交互控件稳定 ID，完成后 `mountBuildAs("hud")`。更新阶段循环 `consumeClick()` / `consumeChange()`，渲染阶段调用 `beginFrameAndRender()`。

### 更新而不重建整个 UI

文本变化用 `setText(id, value)`，进度用 `setValue()`，显示隐藏用 `setVisible()`；结构变化才重新 build 并 `remountBuildAs()`。多宿主时先 `select(host)` 再按局部 ID 操作。

## 常见问题

- 每帧重新 mount，丢失输入焦点与控件状态。
- 多个控件使用同一 ID。
- 未循环消费全部 click/change，队列在后续帧才清空。

## API 快查

下列方法名来自当前 Squirrel 绑定；同一模块创建的辅助对象（例如 `World`、`Body`、`Source`）的方法也列在这里。

- `beginBuild()`、`beginChild()`、`beginCollapsing()`、`beginFrameAndRender()`、`beginGroup()`、`beginList()`、`beginWindow()`、`bindOwner()`
- `button()`、`checkbox()`、`consumeChange()`、`consumeClick()`、`dispatchEvents()`、`end()`、`getChecked()`、`getName()`
- `getScale()`、`getValue()`、`getValueText()`、`initBackend()`、`inputText()`、`isBackendReady()`、`listItem()`、`mountBuild()`
- `mountBuildAs()`、`mountSimple()`、`progress()`、`remountBuildAs()`、`sameLine()`、`select()`、`separator()`、`setChecked()`
- `setHostLayer()`、`setHostModal()`、`setHostOverlay()`、`setHostPos()`、`setHostVisible()`、`setNavKeyboard()`、`setScale()`、`setText()`
- `setThemeDark()`、`setThemeLight()`、`setValue()`、`setValueText()`、`setVisible()`、`slider()`、`text()`、`wantCaptureKeyboard()`
- `wantCaptureMouse()`

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/ui/`](../../../src/modules/ui/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `ui`。
