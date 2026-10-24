# Native ECS capacity benchmark

此 benchmark 测量当前 `external/ECS.hpp/src/ECS.hpp` 的原生 Table、View、继承、槽位复用和
defer/commit；不启动 Vulkan、窗口、Squirrel、Crowd 或 Scene。保留现有实体类型分类，不引入 Tag
或新的 Archetype 实现。整体改进步骤见
[ECS 与场景对象系统改进操作手册](../../docs/dev/ECS与场景对象系统改进操作手册.md)。
首次实测及原始数据见 [2026-10-22 试验记录](results/2026-10-22/README.md)。
首次优化及同机前后对照见 [View 准备优化记录](results/2026-10-22-view-preparation/README.md)。
后续正确性与遍历排查见 [View 作用域与迭代记录](results/2026-10-22-view-scope/README.md)。

死槽补建与稳态缓存优化见 [死槽准备优化记录](results/2026-10-22-live-slots/README.md)。

后续正确性修复的 scope/alignment/reuse 回归用例位于 ECS.hpp 子项目，standalone CTest
同时验证原生和 OBJECT/SHARED foundation 路径，当前共 23 项 CTest。它们覆盖多分支继承、组件创建顺序及
实体构造失败，不依赖计时阈值。

## 构建

在仓库根目录执行，使用 GCC（Windows 可用 MSVC）：

```sh
cmake -S benchmarks/ecs -B build/ecs-benchmark \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build build/ecs-benchmark --config Release -j 2
ctest --test-dir build/ecs-benchmark -C Release --output-on-failure
```

Linux 可执行文件是 `build/ecs-benchmark/ecs_benchmark`；MSVC 多配置生成器通常在
`build/ecs-benchmark/Release/ecs_benchmark.exe`。所有场景单线程运行，无图形依赖。
这里独立编译的是引擎使用的 vendored ECS 核心头文件；不链接引擎生成的容器 extern-template
overlay 和 foundation DLL，不能用来证明跨 DLL 默认 Table 或脚本桥接的性能/正确性。
每个世界均显式传入 `Table`，不依赖全局默认世界。
configure 会幂等应用与引擎相同的默认 Table 所有者补丁、`ecs-view-preparation.patch` 和 `ecs-idle-commit.patch`，
以及 `ecs-view-scope-and-iteration.patch`，
不修改子模块 pin；测试依赖
仓内 zeroerr。`BUILD_TESTING=OFF` 可只构建无测试框架依赖的 benchmark。
CTest 还会用引擎真实的 `EcsDefaultTable.cpp` / `EcsContainerInstances.cpp` 和生成头文件，
分别构建 OBJECT/SHARED 核心契约程序；这不等于已验证完整引擎或全部平台。
另有严格 C++17 的无组件 View 编译/运行检查，防止依赖 GCC 的零长度数组扩展。

无 CMake 时可直接构建相同源文件；此方式的 revision 元数据为 unknown，须另外记录 Git SHA：
先通过上述 CMake configure 应用引擎补丁，或用仓库 `patch_third_party.cmake` 显式应用，避免
误将未打补丁的上游头文件当作当前引擎实现。

```sh
mkdir -p build/ecs-benchmark
g++ -std=c++20 -O3 -DNDEBUG -Wall -Wextra -Wpedantic -Werror \
  -Iexternal/ECS.hpp/src '-DECS_BENCH_CONFIG="Release-direct"' \
  benchmarks/ecs/main.cpp -o build/ecs-benchmark/ecs_benchmark
```

## 运行

```sh
build/ecs-benchmark/ecs_benchmark --entities 1000000 --scenario dense \
  --samples 21 --warmup 5 --iterations 5 > /tmp/ecs-dense.csv 2> /tmp/ecs-dense.log
```

每次进程只运行一个场景，避免前一个场景保留的 allocator 内存污染后一个场景的 RSS：

```sh
mkdir -p /tmp/ecs-results
for n in 10000 100000 1000000 3000000; do
  for scenario in dense dense4 cold holes50 holes90 refill inheritance deferred vector; do
    build/ecs-benchmark/ecs_benchmark --entities "$n" --scenario "$scenario" \
      --samples 21 --warmup 5 --iterations 5 \
      > "/tmp/ecs-results/${scenario}-${n}.csv" \
      2> "/tmp/ecs-results/${scenario}-${n}.log" || exit 1
  done
done
```

正式比较至少进行三轮独立进程重复，轮换场景顺序，并记录 CPU、可用内存、容器 CPU/内存配额、
OS、编译器、编译参数、Git 修改状态及 ECS 头文件 hash。共享 VM 的抖动不能当作稳定回归。
大规模试验先从 1 万逐级增加；上限参数为 1000 万，不代表该规模一定适合每台机器。

## 场景

| 场景 | 内容 | 目的 |
| --- | --- | --- |
| dense | 全部存活，Position/Velocity 两列 | 基础顺序遍历与更新 |
| dense4 | 额外读取 Energy、更新 Health，共四列 | 多组件 zip 成本 |
| cold | 额外挂 256 字节 Cold，更新只访问热两列 | 热冷分离；验证 Cold 未被改变 |
| holes50 | 隔一个删一个，保留偶数索引 | 约 50% 空洞的扫描成本 |
| holes90 | 每十个保留第一个 | 约 90% 空洞的扫描成本 |
| refill | 删去约一半再补满 | freelist 复用、旧 Handle 不得复活 |
| inheritance | Unit/Soldier/Vehicle 交错创建，各约三分之一 | 基类 View 跨子类 Buffer |
| deferred | 每批先创建 N/100 个，再销毁同量旧实体，退出 scope 提交 | staging、发布、代际和恒定活体数量 |
| vector | 两个连续 vector，执行同样的两列计算 | 数组参考，不是功能等价 ECS |
| prepare | 创建/销毁完整两列 View，不迭代、不推进仿真 | 单独定位 View 准备与 scope 退出成本 |

Position/Velocity 各为两个 double（各 16B），Health/Energy 各 8B。数据按 seed 确定生成，无随机
数调用。dt=1/64 秒；warmup 也推进真实状态。deferred 用批次替换 handles 的前 N/100 个实体，
每批至少一个；创建和销毁各算一次 operation。继承场景不要求基类对象先出现才能成立，但本
benchmark 不是对“仅创建子类”边界的完整契约测试。

## 指标与校验

stdout 是 CSV，stderr 是配置、revision 和校验信息。解析 CSV 前必须确认进程退出码为 0，
且日志含 `validation=passed`；失败可能发生在已经输出部分指标之后，部分 CSV 不代表通过。
输出协议标识在 stderr 的 `schema=ecs-benchmark-v1`。消费者按 CSV 列名读取、忽略未知新增列，
缺少所需列或遇到未知 schema 版本则拒绝比较；改变指标含义需要提升版本。

- `update_tick`：每个 sample 连续执行 iterations 次完整 View 更新，除以 iterations。
  p50/p95/p99 是这些**批次平均 tick 耗时**的 nearest-rank 分位数，不是单帧尾延迟。
  包含 View 构造、遍历、析构及自动提交；校验不在计时区间内。
- `view_prepare`：只构建/析构 View（包含空提交），采用同样分位数口径；没有处理实体，operations 与
  million_ops_s 均为 0，仍报告 live 和 RSS。位置校验要求保持初始化值。
- `million_ops_s`：operations / p50 秒 / 1e6。update 中 operation 是一个活体的更新，
  不是扫描槽位、CPU 指令或组件数量；holes 场景必须同时比较原始 N 和 live。
- `create_once`：分配、组件初始化和 Handle 收集；`remove_once`、`refill_once`、
  `destroy_deferred_once` 是一次性测量，samples=1，分位数相同，不能作为生命周期尾延迟。
  销毁指标包含完整 View 扫描、入队及提交；不包含 Table 析构释放所有容器的时间。
- deferred 的 queue/commit 分开计时；queue 含初始化及销毁前的 Handle 解析。
  commit 含 scope 析构，但不含后续校验。此场景报告跨批次分位数。
- `rss_bytes`：Linux `/proc/self/statm` 的进程驻留内存快照；其他平台或读取失败为 -1。
  不是 ECS 专属内存、峰值或精确分配字节；含可执行文件、allocator、Handle 辅助数组等。
  销毁后 allocator 可能保留内存。不能用 RSS 减 payload 得出精确 ECS overhead。
- `hot_payload_bytes`、`cold_payload_bytes`：逻辑组件数据量/实体，未计实体、Handle、位图、
  deque/map 管理及 allocator。vector 场景没有 ECS 身份与生命周期，RSS 差额不能全归为
  ECS 容器开销。RSS 的采样和输出均在计时区间外。

所有存活实体逐一校验完整 tick 后的位置；检查 View 数量、只读速度、四列模式 Health、冷数据、
删除/复用后的旧 Handle，以及延迟创建的 Handle 在提交后仍有效且可见。double 数据与 dt
采用二进制可精确表示的步长；本试验不承诺一般浮点游戏仿真的跨平台 bit-exact。

测量代码不逐实体调用脚本、不按字符串找节点、不混入渲染提交。结果仅能说明原生 ECS 的
局部承载能力；60 FPS 的完整游戏还需要给 AI、物理、Scene、上传和渲染留预算。

## 如何据此做决策

先定义产品所需 N 与 ECS 阶段预算，例如“目标机器、100 万活体、两列更新 p95 批次均值不超过
4ms”；这是项目预算示例，不是本项目已实现承诺。按相同构建和机器对比：

1. dense 的 N 扩大十倍，时间与 RSS 是否近似线性？
2. holes90 在相同原始 N 下，是否仍接近扫描 N 的成本？若是，优先研究位图跳跃和活体索引。
3. refill 是否恢复遍历吞吐？若恢复，优先改善复用而非整体换存储。
4. dense4、inheritance 与 dense 的差额是否超预算？只有测量支持才优化 zip 和类型切换。
5. vector 与 dense 的差距是否值得维护连续页接口？vector 是参考路径，不是必须达到的指标。

修改后重新运行所有场景及生命周期契约测试，性能改善必须保留校验和既有稳定指针/Handle 契约。
