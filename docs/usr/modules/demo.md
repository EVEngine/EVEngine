# 内置演示模块

**脚本入口：** `eve.Demo()`

查询或运行随宿主编译的演示能力，用于验证引擎安装。

## 基本用法

```squirrel
local demo = eve.Demo();
print(demo.getName() + "\n");
```

## 对象关系与调用时机

`Demo` 是可选模块，生成程序纹理和 SoundData 供 Graphics/Audio 使用。它没有游戏循环所有权，也不是通用资产管理器。

## 目标导向指南

### 验证图形和音频安装

使用 `newPlanetTexture()` 生成无需外部资产的纹理，用 `newSound(kind)` 生成测试声音；若这些资源能够由 Graphics/Audio 正常播放，说明基础后端可用。

### 原型阶段使用占位资产

在美术资源完成前可用 Demo 生成物占位，但发布内容应换成项目资产。Demo 模块是可选构建项，正式游戏不要把核心逻辑依赖在它一定存在上。

## 常见问题

- 关闭 `EVENGINE_BUILD_DEMO` 后仍依赖 Demo。
- 把验证资产当最终内容。
- 生成后未保留 Texture/SoundData 生命周期。

## API 快查

下列方法名来自当前 Squirrel 绑定；同一模块创建的辅助对象（例如 `World`、`Body`、`Source`）的方法也列在这里。

- `getName()`、`newPlanetTexture()`、`newSound()`

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/demo/`](../../../src/modules/demo/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `demo`。
