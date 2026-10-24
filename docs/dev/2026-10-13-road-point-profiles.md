# 道路控制点剖面契约

EasyRoads 外层包的 marker 宽度与横坡是这一批实现的参考。当前加入
`RoadControlPoint.widthScale` 和 `rollDegrees`；它们属于 `RoadNetwork`
作者数据，bake、车道、标线和放置结果都是派生数据。

## 数据与几何

- widthScale 范围 [0.05,16]，默认 1，只缩放行车部分。路缘、人行道、标线宽度、结构厚度继续使用米。
- rollDegrees 范围 [-180,180]，默认 0，围绕中心线旋转完整截面。两个参数复用 SplinePath 的逐控制点线性插值，不按弧长重新插值。
- 反转改变控制点顺序并取反横坡，保持物理截面朝向；节点移动、合并、接入口重连只改端点位置，保留剖面。
- 拆分点采用实际样条参数处的插值剖面，两条新边共享相同端点值。
- 导航、标线、交通标志及侧边锚点使用采样处宽度。路口 socket 的裁切搜索采用整边最大宽度作为保守界，端口几何采用实际采样宽度。
- 地形投影继续消费生成的沥青面，因此包含宽度和横坡。沥青 material group 不包含垂直路缘面。

调用同步、限定在网络拥有者线程，不新增回调、跨帧裸指针或跨域 Link。
参数检查与网络验证使用结构化 Result。无 RNG、时间或 ECS 结构修改。
几何验证以 1e-4 米容差比较，不声明跨编译器位级一致。

## 快照和事务

schema `eve.procgen.roadNetwork` version 1 增加可选 edge 字段
`pointProfiles`，与 xyz 三元组 `points` 平行，每项包含 widthScale、rollDegrees。
旧 v1 快照缺少此字段时恢复为 1、0。存在字段必须数量一致且两个值有限、在范围内。
未知字段延续既有忽略策略，未知 schema/version 拒绝；旧消费者不能无损往返新增字段。
解码到私有候选网络后完整验证再发布，失败不改变当前 revision 或可观察数据。
编辑命令、快照和 inverse 使用同一编码器；撤销保存原始作者值。

## 验证和范围

`test/unit/procgen/procgen_road_profiles.cpp` 检查宽度、车道/侧边锚点、横坡、物理反转、拆分和无效编辑。
`test/integration/gameplay-composition/procgen_road_profile_editing.cpp` 检查快照往返、旧数据迁移、损坏数据原子拒绝和事务撤销。
独立 CPU harness 验证 STATIC 与 SHARED 消费者；这只是该源闭包的链接证据，不代表完整引擎或裁剪配置验证。

本批同时将大型 baker 与网络 recipe 按职责拆分，纯拆分阶段六个既有 fixture 输出逐字节相同。
之后修复无路口裁切时首尾采样丢失以及内侧路缘错误 material group，预期会改变旧 mesh。

Unity 账户待恢复，未宣称渲染一致。侧边偏移横坡枢轴、独立左右横坡、控制点地形参数、
桥隧、任意截面、连续设施和资产端口的后续实现见
[这一批数据契约](2026-10-13-road-sections-structures-connections.md)；75 项报告保留初始差距快照。
本批应用单一权威、Result、确定性、持久化原子性、公共 API 文档及模块分层规则，无架构例外。

验证记录：STATIC 与 SHARED 下新增 5 个用例均通过，各 1,987 项断言；CTest 包装通过。
既有道路编辑文件在本轮 STATIC harness 中出现 5 项警告，隔离编译未修改 HEAD 后
复现同样 5 项，未作为通过处理。它们涉及 NoOp 构造、注册数、位置投影与撤销。
`make check CI_BASE=HEAD` 完整通过，含 264 个脚本测试。默认 origin/dev 基准的
`make check` 曾报本分支既有 `IStateProvider.h` 的三个 bool 操作返回值违规；
不修改这些其他领域接口，不新增 baseline/allowlist。本轮新增目录拆分导致的私有
操作返回值已改为明确 TriangulationStatus，谓词采用 is 前缀。

最新六个基础几何、12 张真实引擎回读帧及哈希位于 ignored
`build/road-parity/current-fixtures/fixtures/`；使用当前核心编译的 mesh 上传到既有
EVEngine 渲染宿主，并非完整引擎重编译证据。这六个基础场景不包含新增宽度/横坡，
新增参数由上述 CPU 契约测试验证。Unity 缺帧对照报告全部保持 blocked。


## Unity 实测后的宽度策略（2026-10-14）

真实参考顶点证实宽度沿 Catmull–Rom 曲线插值。新 RoadStyle.widthInterpolation 默认为
CatmullRom；显式 Linear 保留初次实现的线性规则。旧 schema-v1 快照缺失此字段时恢复 Linear，
未知值在完整候选中拒绝。横坡仍沿控制点线性插值；15 度固定中心横坡已进行真实双端验证。
样条通用 SplinePoint.scaleXInterpolation 默认 Linear；道路从唯一 style 派生到此字段，无另一套可编辑宽度。
Cubic 复用同一段中心线的前后节点和参数，对极端值采用正值下限 1e-4 和有限 float 上限；
派生值允许正常 cubic overshoot，节点作者约束仍为 [0.05,16]。插入拆分节点若派生值超出作者范围则原子拒绝。
路口裁切界包含 cubic overshoot 的保守界 max + 0.125*(max-min)。
进一步说明和实测记录见 [Unity 道路对齐](2026-10-14-road-unity-alignment.md)。

RoadStyle.frameOrientation：新道路默认 WorldUp，应用作者横坡前按世界 Y 保持截面水平；通用样条默认仍平行传输。schema-v1 缺失该字段恢复 ParallelTransport；未知枚举原子拒绝。该 owning 策略由 style 快照与编辑补偿保存，拆分/反向继承。近竖直切线采用确定性世界 X 参考，作者角度仍线性插值。
