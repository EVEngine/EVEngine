# 窗口模块

**脚本入口：** `eve.Window()`

创建和配置单一游戏窗口。

## 基本用法

```squirrel
local window = eve.Window();
print(window.getWidth() + "x" + window.getHeight() + "\n");
```

## 对象关系与调用时机

`WindowSettings` 只是创建参数；`Window` 持有平台窗口；`Graphics` 必须先通过 `setGraphics()` 关联。窗口初始化放在引擎启动阶段，游戏逻辑通常直接使用全局 `win`，只在设置菜单中调用尺寸变更。

## 目标导向指南

### 按配置创建窗口

1. 在 `config.nut` 保存宽、高和标题等游戏配置。
2. 创建 `eve.WindowSettings()`，填写 `width`、`height`、`centered`。
3. 调用 `win.setGraphics(gfx)` 后再调用 `win.setWindowSettings(settings)`。
4. 用 `getWidth()` / `getHeight()` 回读真实尺寸；移动端可能不会采用请求尺寸。

### 在选项菜单切换分辨率

调用 `setSize(width, height)`，成功后重新读取窗口尺寸，并同步摄像机 viewport 与 UI 布局。退出按钮应调用 `close()`，不要直接终止进程。

## 常见问题

- `setWindowSettings()` 失败：通常是 Graphics 尚未关联，或平台不支持请求的模式。
- 移动端尺寸不同：必须信任 `getWidth/Height()` 的回读值。
- 调整窗口后画面拉伸：同步更新 camera viewport、Canvas 和 UI 布局。

## API 快查

下列方法名来自当前 Squirrel 绑定；同一模块创建的辅助对象（例如 `World`、`Body`、`Source`）的方法也列在这里。

- `close()`、`getHeight()`、`getName()`、`getWidth()`、`getWindowSettings()`、`setGraphics()`、`setSize()`、`setWindowSettings()`

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/window/`](../../../src/modules/window/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `window`。
