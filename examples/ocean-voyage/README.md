# Ocean Voyage

可驾驶的程序化小船与广阔海洋：三频段 JONSWAP/FFT、40 公里海面分级网格、
GPU 驻留位移、真实 World3D 浮力与推进、局部尾浪、泡沫及三维飞沫。
不需要下载船模或贴图，船体和舱室由脚本创建。

```sh
make run/linux-debug GAME=examples/ocean-voyage
```

当前示例明确要求 Vulkan GPU 计算和绘制路径；初始化失败会报告 Result 诊断，
不静默改为 CPU。软件 Vulkan 也可运行，但实时性能仍需测量。

- W / S：前进 / 倒车。
- A / D：船舵。
- C：自动巡航开关，默认开启，便于观察船与波浪交互。
- P：暂停仿真。
- V：飞沫显示开关，仅影响绘制；可暂停后比较相同海面与船位。

统一 60 Hz 整数 tick 驱动谱帧、船体、尾浪。每帧最多补四步，剩余积累时间保留。
显示位置来自刚体姿态；海面中心跟随船只，尾浪窗口按整数格滚动并保留重叠历史。
同一 VM 的脚本热重载保留运行中的原生对象根；它不标记为 persist，也不冒充可序列化检查点。
仅驾驶输入和观测投影使用 persist；销毁/恢复 World3D 后需按新 body 重建 OceanBoat。

接口说明见 [海洋脚本 API](../../docs/usr/ocean.md)。
海岸和水下效果尚在系统后续阶段，不能把本示例作为这些能力的验收。

`game.agent.json` 提供正式 Play Host 契约：`ocean-ready` 观察 tick、船速与完整姿态；
forward/stop/left/right/straight/pause/resume 修改驾驶或暂停输入，不直接移动刚体；
spray-on/spray-off 修改飞沫显示开关。观测同时给出飞沫 alive/emitted/dropped。
`voyageTelemetry` 是只读观测投影，不是恢复或仿真权威。通过 `eve_play` 驾驶和观察，
通过引擎 readback 捕获画面，不使用 eve_eval 操纵游戏。

三维飞沫由同一船体接触 receipt 和水面查询驱动，固定步只推进一次；
拥有型不可变帧投影为按相机深度排序的透明四边形，海面与刚体不读取渲染状态。
最新引擎 readback：`build/ocean-spray-on.png` / `build/ocean-spray-off.png`。
两图均暂停于 tick 164，船速 2.91385 米/秒，59 个存活飞沫，dropped=0；
船体姿态完全一致，差异局限在船周围。这是 Vulkan 绘制证据，不是 WebGPU 或实时性能验收。

船内遮水每个渲染 tick 从刚体位置与四元数投影，船壳网格和遮水斜面共用同一组顶点，
无需单独维护一套船形。遮水范围向船内收 1 厘米，仅裁除内部海面，浮力仍使用完整水面。
上边界为甲板高度；高于甲板的浪不会被无限延伸的遮罩隐藏。
