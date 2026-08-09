# 触摸模块

**脚本入口：** `eve.Touch()`

按索引读取当前触点数量和归一化/屏幕坐标。

## 基本用法

```squirrel
local touch = eve.Touch();
for (local i = 0; i < touch.getTouchCount(); i++)
    moveCursor(touch.getTouchX(i), touch.getTouchY(i));
```

## 对象关系与调用时机

`Touch` 用每帧索引暴露活动触点；索引只在当前帧有效。手势识别器应复制坐标和自己的状态，不应保存“触点对象”。

## 目标导向指南

### 单指拖动

检查 `getTouchCount() > 0` 后读取第 0 个触点，保存上一帧位置计算拖动量。触点消失时结束拖动，不要继续访问旧索引。

### 多点缩放

至少有两个触点时计算两点距离，与上一帧距离比较得到缩放比例；触点数量改变时重置基准，避免缩放突跳。

## 常见问题

- 触点数量为 0 仍读索引：先检查 count。
- 假设索引跨帧代表同一手指：触点变化后要重置手势。
- 桌面无触摸时当作错误：这是正常的 0 个触点。

## API 快查

下列方法名来自当前 Squirrel 绑定；同一模块创建的辅助对象（例如 `World`、`Body`、`Source`）的方法也列在这里。

- `getName()`、`getTouchCount()`、`getTouchX()`、`getTouchY()`

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/touch/`](../../../src/modules/touch/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `touch`。
