# 性能分析：先捕获，再解释成本

**入口：** `eve.Profiler()`，槽位 `profiler`。CPU zone 是统计数据，GPU timing 依赖
有效设备及时间戳支持。0 毫秒不能自动解释成 GPU 没有工作，先核对可用性与捕获范围。

## 任务一：测量一段脚本 CPU 工作

当前源码的 begin 直接绑定 const char*，普通脚本字符串调用会出现类型转换错误；
本章不把现有 profiler.nut 的 zone 示例当作已通过的脚本能力。自定义 zone 由原生
Profiler::begin/end 标记；脚本可以捕获已完成帧及读取原生 zone。

以下是独立捕获片段，可在初始化的诊断步骤运行。它手动拥有一帧；正常游戏帧已有框架
begin/end 时只读取捕获，不再嵌套调用 beginFrame/endFrame。

```squirrel
local meter = eve.Profiler();
meter.setEnabled(true);
meter.beginFrame();
// 当前 begin(const char*) 的脚本绑定不能接收普通字符串；本例只捕获整帧。
local sum = 0;
for (local i = 0; i < 1000; ++i) sum += i;

meter.endFrame();
local rows = meter.capture();
foreach (row in rows)
    print(row.name + ": self=" + row.selfMs + " total=" + row.totalMs + "\n");
assert(meter.hasFrame());
assert(meter.frameMs() >= 0.0);
meter.setEnabled(false);
```

selfMs 是本 zone 排除子 zone 的时间，totalMs 包含子 zone，count 是调用数。
不能把父子 totalMs 相加当帧总成本。begin/end 必须配对；被测操作若可抛异常，catch 后
也要 end，再把异常向上报告。不要每实体每帧创建一个 Profiler 实例。

## 任务二：比较优化前后

固定同一关卡、相机、分辨率、单位数、模拟种子、构建配置与 profiling 开关，预热资源后
捕获多帧，比较帧时间分布及同名 zone。启动时编译 shader 的一次峰值和稳态 GPU 时间
分开记录。textReport 用于快速诊断，capture 的结构化行供工具展示；没有完成帧时先检查
hasFrame，不能展示一份旧捕获当新结果。

若 CPU 已等待 GPU，脚本 CPU 优化不一定改善总帧时间；检查 gpuFrameMs 与渲染提交。
GPU 时间只在已完成的结果可用时解释，设备重建后重新采样。测试隔离场景，不在比较
中同时改变粒子数量、分辨率和 backend。

源码：[Profiler.cpp](../../../src/modules/profiler/Profiler.cpp)，对应脚本证据：
[profiler.nut](../../../test/profiler.nut) 和
[profiler_script.cpp](../../../test/profiler_script.cpp)。Profiler 编辑面板通过
[editor_profiler.cpp](../../../test/editor_profiler.cpp) 验证；它消费捕获，不拥有模拟状态。

## 补充绑定参考

参数与接收者见 [源码生成参考](profiler/api-reference.md)，使用前先确认本章的前置条件与生命周期。
