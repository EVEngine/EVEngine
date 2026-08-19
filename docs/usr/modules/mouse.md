# 鼠标模块

**脚本入口：** `eve.Mouse()`

查询鼠标位置、按键、相对模式与可见性。

## 基本用法

```squirrel
local mx = mouse.getX();
local my = mouse.getY();
if (mouse.isDown(1)) fireAt(mx, my);
```

## 对象关系与调用时机

`Mouse` 脚本绑定提供坐标、按键、可见性、抓取与相对模式。坐标位于窗口空间；世界命中测试需要用当前相机反变换。

## 目标导向指南

### 点击屏幕坐标中的对象

在更新阶段用 `getX()` / `getY()` 取得坐标，用 `isDown(button)` 判断按键；若摄像机有缩放或平移，先将窗口坐标反变换到世界坐标再做命中测试。

### 显示或隐藏游戏光标

用 `setVisible(bool)` / `isVisible()` 控制并查询光标可见性。

### 第一人称视角（相对模式）

```squirrel
mouse.setRelativeMode(true);   // 光标锁定并隐藏，只报告相对移动
mouse.setRelativeMode(false);  // 恢复绝对坐标
```

相对模式下 `getX()` / `getY()` 返回累积位置；如需逐帧增量，应在 `eve_update` 内记录上一帧值求差。
`setGrabbed(bool)` / `isGrabbed()` 控制窗口鼠标抓取（平台行为差异见 `mouse/sdl/Mouse.cpp`）。

## 常见问题

- 把按钮编号与键盘名混用：鼠标按钮使用整数编号。
- UI 已捕获鼠标仍触发游戏点击：先检查 `ui.wantCaptureMouse()`。
- 相对模式请求失败：SDL 在无聚焦窗口等情况下可能拒绝，`setRelativeMode()` 返回 `false`，调用方应检查返回值。

## API 快查

下列方法名来自当前 Squirrel 绑定；同一模块创建的辅助对象（例如 `World`、`Body`、`Source`）的方法也列在这里。

- `getName()`、`getX()`、`getY()`、`setX()`、`setY()`、`setPosition()`、`isDown()`、
  `setVisible()`、`isVisible()`、`setGrabbed()`、`isGrabbed()`、`setRelativeMode()`、`getRelativeMode()`

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/mouse/`](../../../src/modules/mouse/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `mouse`。
