# 完整一圈横坡与地形编辑约束

实际 EasyRoads 参考探针扩展到 64 个。新增 16 个 terrain-deformation 开关与角度组合及 0→270→0 测试：SetMarkerTilting 在两种开关下都原样读回 -360、-45、15、30、45、180、270、360，没有在开启变形时按 30° 拒绝或钳位。手册 scene.html 的“terrain deformation max 30°”描述的是 marker 作者编辑控制；此次只测试 API 和实际生成网格，没有运行其交互编辑器角度手柄，也没有地形对象，不把该结果推广为完整地形工具验收。

原生 RoadControlPoint.rollDegrees 的有界输入范围从 [-180,180] 扩展为 [-360,360]，不取模、不做最短旋转。0→270→0 的 SmoothStep 在首 span 中点是 135°，保持作者的长方向路径。原生仍拒绝超范围/非有限值，参考 setter 的范围之外行为未用作扩大输入范围的依据。

参考 varying-270 的第20行参数 u=.5555557，左右顶点高度差的一半为 1.152525663，左顶点 native Z 为 2.769780874；扣除公共微小 Y 扰动后，这一截面与原生 SmoothStep 270° 一致。该 CPU 断言来自实际参考网格，不使用参考坐标驱动原生生成器。

作者、共享 preset、标准 recipe、实际 artifact 和全部 LOD 保留未取模角度。网络快照 schema 仍为 1，旧范围的数据读入行为不变，保存不归一化角度。旧消费者可能拒绝新范围值，不宣称旧二进制能读取新角度。双次反向恢复原快照；split 插入保留来源 span 的连续角度；无效操作不改变作者；preset prepare/commit、保存后 undo/redo 保留 ±360°。生产者算法修订同步更新为 road-core-2026-10-14-loft-diagonal-v3，避免新范围能力与旧算法版本混用缓存身份。

地形作者编辑模式的 30° 约束、terrain deformation 的独立作者开关及真正多通道写回筛选仍是未完成项。已有通用地形写回不等价于此开关；本轮不把这些能力标为已完成。新正负 270° 场景只验证驾驶面轮廓，材质、交通、地形效果继续按主清单核验。

渲染消费取证：用于对照的 SDK 默认 gbuffer=true、gpuDriven=false，运行时材质读回 unlit、doubleSided=false、receiveLight=false。开启 G-buffer 时仍观察到倒置部分覆盖及额外照明；最小实验只关闭 G-buffer 和原已关闭的 gpuDriven、重新 compile 后，背面剔除恢复。证据日志在 build/road-parity/material-state-probe.out.log 和 forward-state-probe.out.log。具体源代码根因尚未在当前完整 graphics 构建中复现和修复，该 SDK 观察进入 B7/B8 的未完成渲染消费清单。

驾驶面校准显式使用 forward unlit 单面材质，匹配参考 Unlit/Color；原生材质与上传 mesh 的脚本引用跨帧保留。所有32场景按这一相同设置重新捕获，没有仅针对失败场景切换 Cull Off 或放宽 .98。新两个场景进一步使用参考 .375m / 4° 参数扫描作者设置，两端都是162个三角形；原先固定96段在大横坡下造成可见剔除边界差异，不能仅凭中心截面相同宣称透视一致。

EV 证据现在同时记录并核查渲染脚本、捕获 PowerShell、标准启动脚本、引擎读回 hook，以及每视图 main/config/mesh_data 的内容哈希。过期适配器、输入篡改或输入文件丢失直接拒绝，不能在保留相同 fixture 和 PNG 时跳过这一链路。13项证据测试覆盖这些拒绝路径。该证据增强不是生产缓存的第二个作者入口。

适用规范：唯一角度作者、派生帧消费、Result/nodiscard 与失败原子性、持久角度兼容契约、模块六面和核心裁剪。没有新 ECS System、Link、provider、事件或跨域依赖；没有架构例外。

实际索引进一步定位出扫掠四边形的物理对角线差异：参考原始索引 [0,3,1,2,3,0] 在 Z 反射后，连接上一行 -Z 角与下一行 +Z 角；原生原来连接相反的两角。已在 loftProfileClean 明确选择 SecondToFourth，对应原生 [previous-right,next-right,next-left,previous-left] 顶点顺序。仅改变扫掠截面三角化，顶点位置、UV、分组、预算和非扫掠 quad 默认策略保持；原生 CPU 回归验证真实公共连接边和 UV。三角化变化再次更新生产者修订为 v3。

正负270°场景在最终相同 .375m 参数扫描和单面 forward unlit 下，实际透视/俯视 IoU 分别为 .996329826/.999101796 与 .991692992/.999401078，均通过固定 .98；参考微小高度扰动仍独立生成。所有旧场景因索引变化也重拍，整体结果以最后 comparison.json 为准，不能沿用先前29/1或初次新增后的29/3。

最终32场景/64对真实图像：31 passed、1 different、0 blocked；镜像 Circular 的透视 .972161、俯视 .988270，仍未通过。STATIC 道路58 cases / 8851 assertions、实际 artifact/store/全部LOD 1 / 156、Spline 16 / 372通过；SHARED 道路与Spline同一契约通过。正式 EVProcgenCoreCheck 严格警告编译通过，13项证据检查器、ruff、相对HEAD架构检查和差异格式检查通过。默认 make check 仍仅在未改动 IStateProvider.h 的3个 bool操作 API 和 RenderSystem.h 的2处指针生命周期文档失败；没有换基线或豁免。整体核心对齐仍未完成。
