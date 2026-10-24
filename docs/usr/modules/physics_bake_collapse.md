# Collapse bake runtime

`physics_bake_collapse` 位于 `physics/bake/collapse`，L3，仅依赖 `physics_bake`。
它拥有 `CollapseAnimation`、`CollapsePlayback`、`BonePresentation`、`FractureGeometry`
和 `CollapseCaptureSource`。文件仍为 EVECOLL1，v1/v2 迁移、v3 校验和 v4 配方语义保持不变。
旧 `physics/destruction/*.h` 路径是兼容转发头；没有第二份实现或状态。

## module-interface

- 原生输入/输出：拥有型 clip/几何/姿态、checked Result、显式时间、共享冻结播放器。
- Capability：消费方声明窄 `CollapseCaptureSource` 接口，由实时 GeometryCollectionInstance 实现；无全局 provider 注册或查询。
- ECS/Link：无实体、View、结构变更或持久 Link。只在 capture 当前调用借用源引用，不保留源地址。
- 事件：无订阅；capture 同步调用三个只读源查询，不持锁。查询不能重入目标 clip、改变源状态或调用游戏脚本。
- 脚本：本模块无 VM 依赖；既有 `eve.CollapseAnimation` 注册仍在 `physics_destruction`，调用同一原生实现。
- 工具协议：无；profile 探针与 inspector 只消费原生接口。

capture 的源必须在调用期间存活且稳定；每次完整采样从零开始、时间递增，并验证固定采样配方、
碎块数量、拓扑元数据和有限姿态。第一帧有嵌入几何时验证其数量与闭合凸网格；后续要求同一冻结
几何身份。任何源查询、姿态、几何或分配失败均不追加部分帧。源释放后，clip 的姿态值和共享只读
几何继续有效。接口返回的几何必须在其完整共享生命周期内保持不可变。

连接机制：离线每个采样帧读取一个已知源，整帧全成或全不成，使用直接同步接口调用；不创建
registry、跨帧源指针或运行时模块查表。采样播放不会再调用源接口。这里的接口是非 ECS 对象的
边界协议，不是统一实体基类；实际世界所有权与代际仍由 GeometryCollectionInstance 保持。

录制第一帧几何验证成本为每碎块顶点的二次量级，随后每帧复制线性于碎块数；播放复用共享样本。
采样热路径在源码架构检查器中登记。构建归属为 BACKENDS，实时源与脚本注册仍为 DOMAINS；
修改此边界须重编译所有消费者，不能复用旧类布局对象。

`physics-bake-only` profile 现在选择 common、physics_bake、physics_bake_collapse；它保留完整模块，
不需要实时 physics_destruction。OBJECT/SHARED、真实房屋资产和实时源组合的本次迁移验收结果，
以 [实施记录](../../dev/2026-10-21-physics-bake-replay.md) 最新终态为准；源码存在本身不证明验收通过。

## 旋转与离散状态的采样合同

位置线性插值，旋转使用归一化最短弧四元数线性插值（nlerp）。四元数 `q` 与 `-q`
表示同一朝向；跨 179°/-179° 时应沿约 2° 的短弧，不能把角度分量直接线性插值。
状态采用左侧记录帧的值，仅到达下一帧时间才切换；倒放/跳转也按查询时间遵守同一规则。

nlerp 不保证大角度段内恒定角速度。相邻记录姿态不能恢复额外整圈，旋转超过半圈时也无法
仅从两个端点确定真实方向；高速碎块需要提高录制频率并对实际轨迹测量误差。
该限制不会通过缓存加载自动修复。选择新采样间隔应更新 BakeRecipe，以生成独立资产身份。

`physics.bake.collapseRotationCrossesWrapWithoutStateAnticipation` 在独立无 solver 核心中验证
上述跨边界、相反四元数、精确状态切换及倒放语义。它是分析构造姿态的合同测试，
不替代真实碎块碰撞/高速旋转误差基线。
