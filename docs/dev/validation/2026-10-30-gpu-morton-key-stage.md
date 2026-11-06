# GPU 空间键阶段验证（2026-10-30）

`physics_field_gpu` 默认关闭。新增 `GpuMortonKeys`，从 GPU 常驻 `vec4`
源位置缓冲区生成 30 位 Morton 键与原始源索引（每轴 10 位），随后可调用
`sort()` 在 GPU 上执行 30 轮稳定二进制基数排序。
输入边界为有限、视图相对、闭区间。每次更新读取一个拒绝标志；
越界或非有限源使整批失败，上一版键仍可供设备端消费者读取。
三个输出缓冲及扫描暂存区由构建器拥有；显式 `snapshot` 才读回所有键。
排序一次提交 120 个 dispatch；更新失败保留已排序版本，排序失败保留
更新后发布的版本。重复排序不提交 GPU 工作。

真实 Vulkan 测试 `mortonKeysPublishArbitrarySpatialSourcesAtomically`
覆盖负/正边界、中心、分散点、原始行身份、稳定排序、越界拒绝和拒绝后保留旧版本；
`mortonKeysScaleWithResidentSources` 覆盖连续更新和全量键值、顺序。
Windows Debug SHARED 严格编译和两个聚焦测试通过。
重新配置 `EVENGINE_MODULE_PHYSICS_FIELD_GPU=OFF` 后，图形测试目标编译通过，
Morton 测试发现数为 0，已有 14 项 `graphics.multiscale.*` 测试通过。

本机 AMD Radeon AI PRO R9700，1,000,000 个 GPU 常驻源，固定范围
[-1,1]³，120 次连续更新及排序：键生成首轮 0.6378 ms、P50 0.173 ms、
P95 0.2668 ms、P99 0.357 ms、最大观测 0.6378 ms；排序 P50 1.292 ms、
P95 1.5213 ms、P99 1.5528 ms、最大观测 1.5717 ms。输入由模 1023 的
确定性坐标公式生成，只有有限个不同位置；此测量是键生成吞吐，
不代表任意分布的场求解性能。源数组上传发生在计时前，计时包含
GPU 派发、同步和一个拒绝字读回；排序测量包含 120 个 GPU dispatch 和
一次同步，未计入末尾的全量快照验证。原始输出保留在
`build/gpu-morton-sort-million.log`。

每轴量化 10 位使空间分辨率有限；相同键可对应许多源，后续树节点
必须从真实坐标计算保守包围与误差，不能把量化单元当作精确几何。
当前输出已稳定排序，尚未生成压缩树/多极摘要/交互列表，也未接入
`GpuGravitySession` 的固定八簇求值。下一阶段需在 GPU 上完成
有容量诊断的线性层次构建，并对照 CPU 参考场验证误差。
