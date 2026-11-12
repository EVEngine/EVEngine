# 递归生成：守恒摘要核心与验证边界

日期：2026-10-26，未提交工作树。设计与总验收目标不变；此记录不是阶段四完成声明。

## 已实现

[RecursiveModel](../../../src/modules/procgen/recursive/RecursiveModel.h) 是默认关闭的
procgen_recursive 卫星模块，无 graphics、physics 或原 Procgen 依赖。
复制不可变定义，公开昂贵构建/源集合准备成本，返回拥有型 Result。

节点用二进制稳定路径，潜在容量最大 2^52，节点查询最多52步，不枚举后代。
按最长轴划分体积，左右体积/质量按子叶数量分配，子节点覆盖完整父节点。
均匀静态密度下质量和一阶矩守恒到测试容差；动量为零。
物理长度采用二进制尺度指数，质量采用 mantissa * 2^exponent，避免把 2^2700 的质量强转 double。
容量可选择划分固定体积，或在平均叶体积固定时扩张根体积。尺寸缩放不增加叶数。

prepareFrontier 验证完整、互不重叠的叶区间覆盖。只细化一侧时另一侧粗节点保留，
不靠重复父源补质量；有空洞/重复覆盖/预算超限则整体拒绝。
成本明确为 O(K depth + K log K)，不能把排序隐藏进每帧 O(N) 宣称。
随机流由 root seed、rule version、node key、具名流、counter 决定，无共享 RNG 状态。

## 验证

保存的构建入口仍为 [benchmarks/physics_field](../../../benchmarks/physics_field/README.md)，
其契约测试现同时覆盖场、坐标、参考系与生成；性能可执行文件仍只测场。

- MSVC 19.51.36248，Debug 静态与 DLL 消费者编译运行通过。
  聚焦总计29用例、21776断言，其中生成新增5用例。
- WSL GCC 15.2.0，Debug、C++20、-Wall -Wextra -Werror -fno-fast-math，
  相同产品源和29个用例构建/CTest通过。这不是完整 Linux 引擎或 GPU 测试。
- 137个非二次幂容量完整细化，每次父子质量/一阶矩和最终完整源集合均检查。
- 潜在2^52容量直接查询最深叶；长度指数900时质量指数随长度增加2700。
- 容量增长与几何缩放分离、源覆盖空洞/父子重复、命名随机流及未知规则版本拒绝；seed=12345/path=3/Geometry/counter=17
  固定黄金值6882116831085870244在 MSVC/GCC 两端验证。
- manifest 199项声明检查通过；默认 full 196/199，三个新增模块均关闭，显式启用为199/199。
- 新公共头文件和实现全部行通过现有架构 lint。

上一轮 with-msvc `make check` 终态529 tests / 4 failures / 1 error / 5 skipped：
第三方缓存 Bash/Windows 路径问题和 SDK CMake 安装路径转义问题尚在。
本轮新增生成模块后的完整门禁同样以529 tests / 4 failures / 1 error / 5 skipped结束，
未出现新模块相关失败，但整体验收仍未通过。尝试补齐 WSL 源码门禁时发现
缺 ruff/Pillow，且 Python venv 缺 ensurepip；未将这些检查宣称通过。

## 仍未完成

后续玩家稀疏修改和同步候选提交见[编辑记录](2026-10-26-multiscale-edits.md)。
尚未完成保存恢复、异步任务取消、可替换规则 provider、
战舰/村落/王城几何叶适配、非零运动摘要、误差认证、近场细化规则以及渲染/物理组合。
当前规则只是均匀体积划分，不能当成完整的分形美术资产生成器。

唯一 owner、显式版本/时间无依赖、Result、Doxygen 生命周期/成本、FOUNDATION 导出和
模块所有权规则适用。没有新增 ECS System、旧调用路径或兼容豁免；完整实现目标继续保持未完成。
