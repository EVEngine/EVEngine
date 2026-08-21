# 手柄模块

**脚本入口：** `eve.Joystick()`

枚举手柄 / 摇杆设备，读取轴、按钮、方向帽（hat）与 GameController 映射，并支持振动。

## 基本用法

```squirrel
local joy = eve.Joystick();
print("pads=" + joy.getJoystickCount() + "\n");

// 读取第一个设备的轴（返回数组，取值约 -1..1）与按钮状态：
local pad = joy.getJoystick(0);
if (pad != null) {
    local axis = pad.getAxis(0);
    local a = pad.isDown(0);        // 0 号按钮
    local bx = pad.getGamepadAxis("leftx");
    local bA = pad.isGamepadDown("a");
}
```

## 对象关系与调用时机

`Joystick` 模块由 SDL 事件泵在设备热插拔时维护 `Pad` 列表；脚本通过 `getJoystick(index)` /
`getJoystickFromID(instanceID)` 获取 `Pad` 引用。`Pad` 由模块持有，脚本不应释放；设备断开后查询返回空值/0。
轴与按钮读取应放在 `eve_update`（每帧），不要在 `eve_render` 中做输入采样。

`Pad` 提供两套命名：

- 原始摇杆：`getAxis(index)` / `getAxes()` / `isDown(index)` / `getHat(index)`（方向串 `c/u/d/l/r/lu/ld/ru/rd`）。
- GameController：`getGamepadAxis("leftx")` / `isGamepadDown("a")`，名称遵循 SDL GameController 字符串
  （`leftx`、`lefty`、`rightx`、`righty`、`a`、`b`、`x`、`y`、`dpup` 等），需要设备已被 SDL 识别为手柄。

## 目标导向指南

### 检测手柄是否可用

启动和设备事件发生后读取 `getJoystickCount()`；数量为 0 时保留键鼠提示，数量大于 0 时切换为手柄提示。

### 用左摇杆控制移动

```squirrel
local pad = eve.Joystick().getJoystick(0);
local x = pad.getGamepadAxis("leftx");
local y = pad.getGamepadAxis("lefty");
```

### 支持社区手柄映射

启动时用 `loadGamepadMappings(mappings)` 导入 SDL 映射数据库；设置界面修改映射后用 `saveGamepadMappings()`
保存，并可用 `getGamepadMappingString(guid)` 显示诊断信息。

### 手柄振动

`isVibrationSupported()` 探测能力，`setVibration(left, right)` 持续振动，`setVibrationTimed(left, right, seconds)`
限时振动，`stopVibration()` 停止，`getVibrationLeft()` / `getVibrationRight()` 读回当前强度。

## 常见问题

- `getJoystick(i)` 返回 null：设备不存在或已拔出，先判断 null。
- 轴读数为 0：设备是纯摇杆而非 GameController，改用 `getAxis` / `getAxes`。
- mapping 数据库格式错误：使用 SDL 标准映射文本。
- 保存映射覆盖随包文件：写入用户存档目录。

## API 快查

- `Joystick`：`getName()`、`getJoystickCount()`、`getJoystick(index)`、`getJoystickFromID(instanceID)`、
  `getIndex(pad)`、`addJoystick(deviceIndex)`、`removeJoystick(pad)`、`loadGamepadMappings(mappings)`、
  `saveGamepadMappings()`、`getGamepadMappingString(guid)`
- `Pad`：`getName()`、`getAxisCount()`、`getButtonCount()`、`getHatCount()`、`getAxis(index)`、`getAxes()`、
  `getHat(index)`、`isDown(index)`、`isGamepad()`、`getGamepadAxis(name)`、`isGamepadDown(name)`、
  `getGamepadMappingString()`、`getGUID()`、`getInstanceID()`、`getID()`、`getVendorID()`、`getProductID()`、
  `getProductVersion()`、`isVibrationSupported()`、`setVibration(left, right)`、
  `setVibrationTimed(left, right, seconds)`、`stopVibration()`、`getVibrationLeft()`、`getVibrationRight()`

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/joystick/`](../../../src/modules/joystick/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `joystick`。
