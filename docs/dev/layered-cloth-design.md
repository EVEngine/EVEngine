# 多层布料：资料、设计与实现

调研日期：2026-10-19。目标是在 EVEngine 现有 CPU Cloth3D 上补齐多片布料联合求解，提供可运行对照示例；不是 UE 工程导入器或 Chaos 的完整替代。

## 可用工程与教学资料

| 资料 | 获取入口与用途 | 对实现的启发及证据范围 |
| --- | --- | --- |
| Epic Panel Cloth Example Files 5.4 | [官方工程入口](https://dev.epicgames.com/community/learning/tutorials/Ybvo/unreal-engine-panel-cloth-example-files-5-4)、[作者发布帖](https://forums.unrealengine.com/t/tutorial-panel-cloth-example-files-5-4/1836811) | 发布帖确认提供 zip 工程及配置说明；官方正文为动态页面，未在本环境下载或运行工程，不据此声称已复现特定多层服装。 |
| Epic Panel Cloth Editor Walkthrough 5.4 | [官方教学](https://dev.epicgames.com/community/learning/tutorials/Mpze/unreal-engine-panel-cloth-editor-walkthrough-updates-5-4) | 学习资产、Dataflow 与服装工作流，非开放求解器源代码。 |
| Epic Talisman / Captain Roux jacket | [官方项目与教学入口](https://dev.epicgames.com/documentation/en-us/fortnite/create-your-clothing-asset-in-unreal-engine-in-unreal-editor-for-fortnite) | 官方介绍可下载的穿夹克 MetaHuman UE 项目；只作为服装资产制作参考，没有复制资产或实际运行。 |
| Unity 6 Cloth | [官方手册](https://docs.unity3d.com/6000.0/Documentation/Manual/class-Cloth.html)、[层间距离 API](https://docs.unity3d.com/cn/6000.0/ScriptReference/Physics-interCollisionDistance.html)、[参与碰撞顶点 API](https://docs.unity3d.com/cn/6000.0/ScriptReference/Cloth.SetSelfAndInterCollisionIndices.html) | 内置 Cloth 有 self/inter collision 配置；不能误称 Unity 不支持层间碰撞。借鉴统一碰撞配置与参与集合的概念。 |
| Ten Minute Physics Unity | [作者源码工程，MIT](https://github.com/Habrador/Ten-Minute-Physics-Unity) | README 提供 Unity XPBD 学习工程，但其 14 Cloth、15 Self-collisions 标为 TODO；不能当成已经完成的多层布料范例。 |
| Ten Minute Physics 原版 | [教程总页，含视频、代码、Demo 和 PDF](https://matthias-research.github.io/pages/tenMinutePhysics/) | 09 XPBD、11 空间散列、14 布料、15 自碰撞，形成算法学习路径。原版是浏览器演示，不是 UE/Unity 原生多层成衣工程。 |

这些资料分别覆盖服装制作、引擎配置和算法教学，未找到并实际运行一个可直接等价复制的开源多层成衣工程。因此下面的架构是根据资料和现有引擎约束作出的自主设计，代码复用 EVEngine 自身三角形查询，不搬运第三方资产或源码。

## 设计与权威状态

`LayeredCloth3D` 是 `physics_cloth` 卫星内部的组合类型，独占各层 `Cloth3D`，不引入新模块、ECS System、跨模块 provider 或跨帧外借指针。构造可接受规则网格或 `span<const ClothModel>`，复制后不保留源模型。C++ 的 `layer(index)` 只供 owner 生命周期内同步配置；脚本只访问整个组，避免保存借用层指针。层不能被独立推进或销毁，更新前会检查已销毁层。

每个固定步：

1. 所有层积分同一个子步，处理自动撕裂。
2. 每轮先求每层 stretch/shear/bend、skin、tether、attachment、自碰撞、刚体及边界约束。
3. 对不同层按稳定顺序做三角形接触投影，再进入下一轮耦合。
4. 子步结束处理抓取速度；整步结束清除瞬时力。

层间窄相复用 `triangleDistance` 的点面和边边最近特征，用接触点重心权重构造梯度。厚度为两层粒子半径之和。设梯度权重为 w、可移动逆质量为 m，则修正分母为 Σ(m w²)，固定或被抓取粒子贡献零。分离量为厚度减最近距离，各粒子按 m w 分担，保持线性质量中心。零距离时用上一时刻特征点差定向，没有历史方向则使用确定的 +Y。

摩擦通过 Verlet 历史位置消除接触切向位移，上限为 μ×本轮法向分离量；μ 取两层系数的几何平均。层间不施加反弹。两侧 category/mask 必须互相匹配，才执行接触。

宽相采用 X 排序与三轴 AABB 过滤，候选边界在每轮开始时重建。投影新引入的接触在下一轮发现。扫描在稠密重叠时仍可能达到 O(Ta×Tb)，不宣称线性复杂度或工业规模性能。统计是投影次数，包含重复轮次，不是唯一接触点数量。

## 时间、生命周期和模块边界契约

- `update` 注入 dt，不读墙钟、不用随机数；有限 0–0.05 秒，超出范围抛异常，输入校验在状态变更前完成。零步长不推进或消耗瞬时力。
- 示例累积时间，以 1/120 秒步进，单渲染帧最多追赶 12 步，剩余时间保留。
- 全部操作同一物理线程，不回调、不支持并发。相同 CPU 构建、相同初态和调用顺序采用 1e-5 米回放测试容差；没有跨平台 bit-exact 或 CPU/GPU 等价承诺。
- 没有新持久化格式、身份 registry、Link、异步生命周期或可选 provider；所有权只在组内。组析构释放运行时，图形资源仍按现有 Graphics owner 管理。
- 模块边界六面：能力输入/输出、ECS/Link、事件、工具协议均未新增；新增脚本绑定为 `eve.LayeredCloth3D`。保留原 `physics_cloth` 依赖，无新增向上 include。没有新增裁剪等级承诺。
- 输入错误沿用 Cloth3D 的编程错误异常约定，查询返回值和配置操作不创建含混 bool 状态码。没有故意豁免仓库架构要求。

## 范围与限制

这是低到中分辨率 CPU 原型。没有连续碰撞检测、全局交叉消解、持久接触流形、接触 XPBD compliance、GPU 加速、服装缝合编辑器、MD/USD 导入或人体动画演示。离散最近特征接触无法保证高速、初始已交叉或互相矛盾的固定点情况下永不穿透。增加子步和耦合轮能改善，但不是无穿透证明。外部碰撞与层间接触互相矛盾时会留下残余误差。

渲染路径改为使用运行时实际粒子与三角形拓扑，支持非网格模型，并让撕裂后的可见面随碰撞面一起移除。网格保留原 UV，任意网格使用位置平面 UV；不提供成衣原始 UV 导入。

每次调用复用现有完整单层约束求解，XPBD 乘子会在耦合轮间重新初始化；因此整体材料响应仍会受耦合预算影响，不宣称迭代次数无关的完整全局 XPBD。后续应抽出单轮约束阶段和子步级乘子生命周期，并引入 swept vertex-face/edge-edge CCD、持久接触及 BVH。

## 验证入口

`test/unit/physics_cloth/physics_cloth_unit.cpp` 的 layered-cloth 测试段 覆盖面接触分离、不同质量的中心守恒、固定层、双向过滤、关闭碰撞、零步长、非法输入不变更、三层有限值与重放、重置。`test/physics_cloth.nut` 的独立 `layered()` 用例验证脚本构造与控制，避免依赖原 `basic()` 中的 Rope 模块。

```sh
make test/linux-debug DOMAIN=physics FILTER=PhysicsLayeredCloth
make run/linux-debug GAME=examples/layered-cloth
MIN_RUN_SECONDS=2 bash scripts/smoke_examples.sh layered-cloth
make check
```

运行记录见同目录 `layered-cloth-validation.txt`；只以实际完成的检查为准。
