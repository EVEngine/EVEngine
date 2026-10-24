# Physics Bake Deform Capture

L6 可选录制卫星依赖 physics_bake、physics_cloth 和 physics_rope；纯播放模块无反向依赖。
`captureDeformFrame(source, seconds)` 读取真实 Cloth3D 或 Rope3D 的米制粒子与当前连通性。
绳索只录制 active 元素的实际端点，布料只录制 surviving triangles，不重建原始网格。
UV 通道为空。断开、修复、撕裂或改变长度导致不同连通性时，固定拓扑轨道拒绝混合录制。
全部元素消失的快照显式拒绝，不能默默回填原网格。

## module-interface

- 原生输入输出：同步借用 const Cloth3D/Rope3D、显式秒数，返回 checked owning DeformFrame。
- Capability：无 provider 查询或注册。
- ECS/Link：无实体、Link 或跨帧指针；源恢复后调用方提供当前活源，旧快照不受影响。
- 事件：无回调与事件。
- 脚本：暂无绑定。
- 工具协议：无。

调用限定仿真 owner thread，并排除并发源修改；不推进仿真或读取系统时钟。
源先销毁不影响快照，快照先销毁不影响源。线性分配只属于录制阶段。
本模块接入不代表变形文件、表现消费或完整裁剪已验收。
