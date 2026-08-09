# 键盘模块

**脚本入口：** `eve.Keyboard()`

查询按键状态、键盘重复和文本输入。

## 基本用法

```squirrel
if (keyboard.isDown("left")) playerX -= speed * dt;
if (keyboard.isDown("space")) jump();
```

## 对象关系与调用时机

`Keyboard` 提供“当前状态”，不保存 pressed/released 边沿。按键名适合配置文件；scancode 适合与物理键位绑定。文本输入是独立模式，交给 UI 或输入框消费。

## 目标导向指南

### 连续移动与单次触发

`isDown(key)` 适合持续移动。菜单确认、跳跃等单次动作要保存上一帧状态，实现 `down && !wasDown` 的边沿检测，避免按住一帧触发多次。

### 文本输入

进入输入框时启用文本输入，离开时关闭；不要用按键名拼接用户文字，因为输入法、组合字符和键盘布局不会正确工作。

## 常见问题

- `isDown()` 驱动一次性动作导致连发：自行保存上一帧状态。
- 混用 key 和 scancode：键位布局变化时行为会不同。
- 输入框关闭后仍启用文本输入：会继续唤起移动端软键盘。

## API 快查

下列方法名来自当前 Squirrel 绑定；同一模块创建的辅助对象（例如 `World`、`Body`、`Source`）的方法也列在这里。

- `getKeyFromScancode()`、`getName()`、`getScancodeFromKey()`、`hasKeyRepeat()`、`hasScreenKeyboard()`、`hasTextInput()`、`isDown()`、`isScancodeDown()`
- `setKeyRepeat()`、`setTextInput()`

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/keyboard/`](../../../src/modules/keyboard/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `keyboard`。
