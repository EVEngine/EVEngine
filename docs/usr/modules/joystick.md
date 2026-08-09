# 手柄模块

**脚本入口：** `eve.Joystick()`

枚举手柄并管理 SDL GameController 映射。

## 基本用法

```squirrel
local pads = eve.Joystick();
print("controllers=" + pads.getJoystickCount() + "\n");
```

## 对象关系与调用时机

当前 `Joystick` 绑定聚焦设备数量和 SDL gamepad mapping 管理，尚未暴露轴、按钮和单设备对象。它适合设备诊断与映射准备，而不是完整玩法输入。

## 目标导向指南

### 检测手柄是否可用

启动和设备事件发生后读取 `getJoystickCount()`；数量为 0 时保留键鼠提示，数量大于 0 时切换为手柄提示。

### 支持社区手柄映射

启动时用 `loadGamepadMappings(path)` 导入 SDL 映射数据库；设置界面修改映射后用 `saveGamepadMappings(path)` 保存，并可用 `getGamepadMappingString(index)` 显示诊断信息。

## 常见问题

- 文档/API 中找不到轴读取：当前确实未绑定，应使用键鼠或扩展模块。
- mapping 数据库格式错误：使用 SDL 标准映射文本。
- 保存映射覆盖随包文件：写入用户存档目录。

## API 快查

下列方法名来自当前 Squirrel 绑定；同一模块创建的辅助对象（例如 `World`、`Body`、`Source`）的方法也列在这里。

- `getGamepadMappingString()`、`getJoystickCount()`、`getName()`、`loadGamepadMappings()`、`saveGamepadMappings()`

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/joystick/`](../../../src/modules/joystick/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `joystick`。
