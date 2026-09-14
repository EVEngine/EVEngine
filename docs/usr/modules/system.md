# 系统信息模块

**脚本入口：** `eve.HostSystem()`

查询操作系统、CPU、内存、电量、剪贴板和 GPU 信息。

## 基本用法

```squirrel
local sys = eve.HostSystem();
print(sys.getOS() + " / " + sys.getProcessorCount() + " cores\n");
sys.setClipboardText("EVEngine");
```

## 对象关系与调用时机

`System` 是平台查询和主循环帧率策略入口。硬件信息适合启动时缓存；wall time、进程内存和电量可低频刷新；`sleepMilliseconds()` 会阻塞当前线程。

## 目标导向指南

### 根据设备能力选择质量档

启动时读取 `getSystemRAM()`、`getProcessorCount()` 和 GPU 信息，将结果映射为低/中/高画质；这些值用于选择默认设置，不应每帧查询。

### 实现剪贴板和平台信息页

复制按钮调用 `setClipboardText(text)`，粘贴按钮直接用 `getClipboardText()` 读取，并对空字符串做处理。错误报告中可附加 `getOS()`、CPU、内存和显卡信息。

## 常见问题

- 每帧查询硬件：没有收益且部分后端查询昂贵。
- 用 wall time 驱动动画：系统时间可能跳变，应使用 Timer。
- 在主线程 sleep 做计时：会冻结事件和渲染。

## API 快查

下列方法名来自当前 Squirrel 绑定；同一模块创建的辅助对象（例如 `World`、`Body`、`Source`）的方法也列在这里。

- `getCPUCacheLineSize()`、`getClipboardText()`、`getEngineVersion()`、`getGpuDeviceType()`、`getGpuMemoryTotalMB()`、`getGpuName()`、`getGpuVendor()`、`getName()`
- `getOS()`、`getPlatform()`、`getPowerPercent()`、`getPowerSecondsLeft()`、`getPowerState()`、`getProcessMemoryMB()`、`getProcessorCount()`、`getSystemRAM()`
- `getWallTime()`、`setClipboardText()`、`sleepMilliseconds()`、`limitFrame()`
- `getPhotoModeVSync()`、`getPhotoModeTargetFPS()`

Pcg `FrameRateManager` 由 `eve.PcgFrameRateManager()` 承接。`configure(targetFrameRate,checkInterval,minQuality,maxQuality,currentQuality)` 原子配置状态机；每帧调用 `update(dt,timeScale)`，再读取 `getFps()`、`getQuality()`、`getQualityChanged()` 和 `getPreset()`。手动选择使用 `selectManualQuality(level)`，自动模式使用 `setAutomatic(enabled)` / `getAutomatic()`。返回的 `PcgTerrainQualityPreset` 提供 `treeDistance`、`treeBillboardDistance`、`treeCrossFadeLength`、`treeMaximumFullLodCount`、`detailObjectDistance`、`detailObjectDensity`、`heightmapPixelError`、`heightmapMaximumLod`、`basemapDistance`。

PhotoMode 的 System 域由模块实例自动接管。`m_vSync` 的 0/1/2 分别保留“不同步/每次垂直空白/每两次垂直空白”语义，并通过 Graphics presentation capability 触发真实交换链策略更新；非零 VSync 时忽略软件帧率上限。`m_targetFPS` 支持 -1 到 240，VSync 为 0 且值大于 0 时，桌面主循环在 present 后调用 `limitFrame()` 节流。由浏览器宿主驱动帧循环时不会阻塞宿主调度。

## 使用要点

- 模块对象和它创建的资源对象应保存在全局或实体状态中，不要在每帧重复创建。
- 带 `update(dt)` 的系统应在 `eve_update` 调用；绘制方法应在 `eve_render` 调用。
- 参数约束、默认值和返回类型以对应模块头文件及 `addFunc` 绑定为准；本文 API 快查与当前源码同步生成。

**源码：** [`src/modules/system/`](../../../src/modules/system/)
**相关测试：** 在 [`test/`](../../../test/) 中搜索 `system`。
## Pcg 延迟任务队列

`eve.PcgTaskQueue()` 移植 Pcg `PcgTask` / `PcgTaskBase` 的等待与轮询生命周期。`add(waitSeconds)`
加入任务并返回稳定 ID，默认等待 0.25 秒；`tick(deltaSeconds)` 只把到期任务发布为 Ready，调用方通过
`getReadyTaskId` 取得 ID、在队列外执行任务，然后调用 `resolveReady(finished)`。未完成任务在下一轮继续，
完成任务被移除。`cancelAll` 清空队列；`getQueueSize` 与 `getStatus` 提供状态。队列不保存回调或脚本对象，
显式 dt 使调度可回放，非法时间不会改变队列。
