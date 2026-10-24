# View 准备与空提交优化记录

日期：2026-10-22。第一批已实施；其余迭代器扫描、空间查询、并行调度及场景表示工作仍按整体操作手册推进。

## 实现与不变量

1. `ecs-view-preparation.patch`：复用现有组件 presence 位图，完整初始化的 64 槽组直接跳过；不完整组仍按原顺序逐槽检查和构造。没有缓存“已准备长度”，因此 reset、复用、构造异常和构造期间修改后续槽位都不会漏补。
2. `ecs-idle-commit.patch`：Table 维护由原生创建路径设置的待发布 dirty 标记。没有主存储 staging 创建时，commit 跳过整个实体注册表的发布扫描；销毁回收及 legacy staging manager 发布仍照常执行。发生过 staging 创建时仍扫描发布，当前没有引入额外的逐实体待发布索引。

两项均经仓库既有第三方补丁流程接入引擎和独立 benchmark；pin 不变。新 dirty 位是 Table owner 内维护的派生状态，不是组件/实体的第二份权威状态，不增加线程安全承诺。组件列、实体地址、代际 Handle 和 View 补建语义不变。**Table 私有布局增加字段，必须完整重编译 engine/SDK/插件，不能混用新头与旧库。** 直接修改生命周期 flags 不属于受支持创建/销毁路径。

## 测量口径

同一共享 VM，GCC 14.2.0，Release `-O3 -DNDEBUG`；100 万实体，warmup=3、samples=11、iterations=3。每个数据点是三次完整 scope 的平均耗时。prepare/dense/holes90 各三轮，其余场景各一轮；同轮 before/after 顺序交替。仓库源码质量检查并行运行，未控制 CPU 频率；所有运行均通过结果校验。

先做位图单项试验，发现空 View 退出时仍由 `publish_staging()` 扫描全表；再加入空提交优化。两阶段原始结果分别保存，避免把不同实现混为一组。

## 最终组合优化：三轮结果

下表为三轮 p50 的中位数，并附三轮 p50 范围；不是所有样本合并后的 p50，也不代表单帧尾延迟。

| 场景 | 优化前 ms（范围） | 优化后 ms（范围） | 中位数比值 |
| --- | ---: | ---: | ---: |
| prepare | 33.177（28.020–34.857） | 0.106（0.059–0.135） | 312.48× |
| dense | 114.997（72.664–115.629） | 56.014（55.377–146.285） | 2.05× |
| holes90 | 72.472（56.419–81.324） | 31.331（28.922–50.039） | 2.31× |

prepare 不更新任何实体，主要验证固定准备/退出成本消除。holes90 仍有原始 100 万槽位，但仅 10 万活体。

**不能声称完整更新在每一轮都更快**：dense 第三轮 before=115.629ms、after=146.285ms，存在回退。前两轮分别为 114.997→56.014ms、72.664→55.377ms。空 scope 的降低在三轮中一致；完整更新的稳定吞吐和尾延迟需要受控机器继续测量，不能据此宣称已满足 60 FPS 百万实体更新。

## 验证

- CMake Release 共 13 项 CTest 全通过（10 个 benchmark 场景、原生/OBJECT/SHARED 三个契约入口）；本次新增 7 个契约用例，连同既有容器用例共 10 个测试、733 个断言。
- 最终优化版 10 个场景均通过 300 万实体校验（samples=3、warmup=1、iterations=1，仅作容量/正确性 smoke，不据此计算性能提升）。
- 契约覆盖：0/63/64/65 等边界、缩短前缀、空洞重建、已有值保留、构造异常重试、构造期间更改后续槽位、非默认构造组件、仅派生实例、Table 隔离、嵌套 defer、显式提前 commit、force_main、创建取消、代际复用。
- ASan/UBSan 契约运行通过；因 ptrace 环境关闭 LeakSanitizer，不宣称泄漏验证。
- 补丁在 pinned 输入上首次应用、重复应用及整个补丁序列重放通过；不接受静默忽略漂移。
- 使用引擎真实的 EcsDefaultTable/EcsContainerInstances 与生成 extern-template 头，OBJECT/SHARED 两种路线的核心契约程序均构建、运行通过。
- `make check` 完整通过（base=origin/dev，313 个脚本测试通过）；clang-format 18、ruff、测试域清单与文档相对链接检查通过。

未执行完整引擎图形启动、全平台或全模块测试；不据此宣称 Scene/脚本/GPU 的性能提升。

## 架构适用项与回滚

- 类型短根、组件和 Handle 模型保持；无 Tag、新实体根、新 provider 或跨领域连接。无架构例外。
- `R-COST-5`：更新本记录与 benchmark 的 scope 成本定义；未新增昂贵公共运行时 API。
- `R-MECH-1/2`：优化现有批量 View 机制，未新增逐实体字符串/能力查询。
- `R-BOUND-1/3`：只对独立 core/owner 测试的真实编译链接结果作结论；不从 include 图推断全引擎裁剪。
- `R-FACE-1/2`：未新增六面交换、模块或公共协议，保留既有数据所有权和生命周期；不增加平行 JSON 契约目录。

回滚这批优化时移除两项性能补丁及引擎/benchmark 中对应应用项，保留原有 shared-default-table 补丁；重新取出 pinned 子模块源码并完整重编译所有消费者。不要对仍有在途对象的进程热替换 Table 布局。

## 原始数据

- [最终组合前后对照](combined.csv)、[原始配置/校验日志](combined.log)。
- [位图单项前后对照](word-only.csv)、[单项日志](word-only.log)。
- [300 万容量 smoke CSV](scale-smoke.csv)、[smoke 配置/校验日志](scale-smoke.log)。
- [环境、源码 hash 和构建口径](environment.json)。
- [benchmark 构建与运行](../../README.md)。
