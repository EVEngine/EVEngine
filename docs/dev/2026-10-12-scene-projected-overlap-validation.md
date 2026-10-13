# 投影轮廓重叠关系

本阶段实现 projected_overlap，复用原生可见性 query、私有 Box3D 候选、正式 check/commit 及历史。没有开发 AI 图像拆分、资产搜索或自动求解器。

## 协议与语义

作者文档 eve.scene.authoring v4；v1/v2/v3 显式迁移到 v4，关系完整保留，v1 添加空关系。v1/v2 禁止 region，v1/v2/v3 禁止 projected_overlap，未知字段/版本和非法定义原子拒绝。区域协议仍为 eve.scene.local-region v1。

关系示例：{"id":"compose","type":"projected_overlap","subject":"tree","reference":"house","minimum":0.3,"maximum":0.5}。minimum 必须指定，maximum 默认 1，区间在 [0,1]；支持既有 hard/priority/weight。contact tolerance/normalMinDot 禁止。可选 region 选择 reference 的局部真实表面，并沿用 geometryRevision 失效检查。

定义是 |fullMask(subject) ∩ fullMask(reference/region)| / |fullMask(reference/region)|。这是有方向的 reference 覆盖率，不是 IoU，不是可见区域比例，也不是前方遮挡贡献。每个对象独立渲染保留自身遮挡；其他对象遮挡和前后深度顺序不参与交集。任一参与 mask 少于 4 像素则 Unmeasurable，硬约束拒绝；画面裁剪不计入分母，不提供 in_frame 保证。

visibility-request v1/v2 继续支持原格式；visibility-evidence 输出 v3，target.projectedOverlaps 返回每个不同 whole-object partner 的 object/status/intersectionPixels/referencePixels/subjectPixels/fraction。本 target 可为 region，partner 只支持整个 subject。同一对象不同区域互相重叠不是本阶段协议。Native Session 自动规划 reference/region 与 whole subject，客户端不能省略参与对象来放行。

## 实现与架构

实际 isolated triangle pixels 形成瞬时 owning byte masks，复用既有 full-object GPU readback；随后做 CPU 像素交集。没有包围盒面积替代，也没有新增 GPU pass。单次 query 仍最多 16 targets、4096 draws、4194304 总 raster 像素预算；额外内存为 targets*viewport bytes，CPU 最坏 O(targets²*viewport)。显式两步 prepare/measure 成本已在接口注明。

作者文档仍是关系唯一所有者；mask/evidence 仅为同步瞬时副本。ObjectId/原生 host/entity/mesh generation、source revision/candidate/camera proof 和提交重新测量沿用既有机制。没有新 ECS System、Link、provider、向上 include 或 per-frame lookup。模块接线仍为单消费者、显式低频同步 capability，已知 provider，测量与提交保持同一事务验证边界。缺席 provider 明确 Unsupported。无新裁剪声明、无架构例外。

支持范围仍为 untextured opaque static meshes；alpha-test、透明、polygon、多对象独立联合求解未实现。soft 关系报告 residual，不在本阶段自动排名搜索。

## 验证计划

定向测试覆盖 v3→v4 迁移、旧版本不能夹带新类型、往返/删除 undo、区域 native target planning、上下界和软硬规则、缺失/非法/重复 pair 证据拒绝。原生 MCP 场景包含第三遮挡者：target 完全不可见仍能测量两对象轮廓交集；硬约束初始拒绝，候选一次提交/undo/redo；另检查前后反转、部分交集、画外 partner 不可测。复验现有区域/遮挡/物理案例和缺席路径。

实际构建、运行和门禁结果追加在末尾。

## 最终记录（2026-10-12）

SHARED 严格警告开启和 OBJECT Debug 均完成 eve/scene_authoring_contract_test 编译链接。最终各 60 个过程隔离测试全部通过；没有声称 Release 编译或全平台验证。新增 4 个案例涵盖持久迁移、区域 target planning、严格像素计数/比例/可测状态一致性、非法/缺失 pair fail-closed，以及 provider 缺席不能 check/commit，且文档未改变。既有区域/视觉/物理测试共同通过。ruff 与 10 项 UTF-8 脚本测试通过，git diff --check 通过。

两种链接配置最终正式 main.nut + 原生 MCP smoke 成功，证据位于 build/scene-overlap-final-evidence 和 build/scene-overlap-object-final-evidence。projected-overlap-proof.json 记录组合硬约束（nonpenetrating、whole/region projected_overlap、occludes）从失败到满足，预览不改文档、提交一次 undo/redo；相同物理/视觉完整检查在 commit 重新执行。

真实 reference fullPixels=1156；前/后两个深度顺序完整重叠均 intersectionPixels=1156、fraction=1。第三 blocker 完全遮挡时 reference visibleFraction=0，但 overlap=1；移除 blocker 且 subject 在后方时 visibleFraction=1，overlap 仍为 1。partial intersectionPixels=816，fraction≈0.705882。画外 subjectPixels=0，pair 状态 Unmeasurable。局部 lower region 的 projected_overlap=1。原生截图 projected-overlap-depth-reversed.png 已人工查看：前方橙色 reference 位于后方绿色 subject 的完整轮廓内。固定回归场景原有 IoU=1.0 不是任意效果图精度声明。

默认 make check 实际执行两次，均为 59 项既有违规：RenderCapture.h 3 个 bool 操作、SceneQuery.h 1 个、StatePatch.h 1 个、Procgen.cpp 54 个运行时 ModuleManager 调用。四文件相对 HEAD 没有改动。未调整 CI_BASE/基线/allowlist，不声称源门禁全绿。一次过早启动的 OBJECT CTest 在 exe 链接完成前得到 BAD_COMMAND；等待链接完成后的最终 60 项测试全通过。

R-FACE-1..3：更新 scene/graphics 模块契约 evidence/tests 和版本协议；R-MECH-1..4：沿用既有显式 owner-thread capability，不增加 runtime 查找；R-COST-1..4：复用两步昂贵 query，并注明 O(targets²*viewport) 的 CPU 成本及原预算。Result/nodiscard、单一权威所有者、严格版本迁移/原子恢复、代次失效、native commit 再测、provider present/absent 都有验证。无新 ECS System/Link/裁剪声明，无批准或采用的架构例外。GPU validation 全绿仍未声称；前阶段两类既有 VUID 未在本阶段修复。

## 合并 dev 后的最终 PR 验证

已合并 origin/dev c64a2fc27e178b9df544958c80f54e8d869ce266，保留本地全部场景接口和双方文档/契约条目；模块分片重新生成 80 项汇总契约。无冲突标记，git diff --check 通过。

合并后的默认 make check 成功（base=origin/dev），247 项全量脚本测试通过。前述 59 项记录是合并前的历史门禁结果，不是当前 PR 验证状态。

Windows SHARED（严格警告开启）与 OBJECT Debug 的 eve/scene_authoring_contract_test 均编译链接成功；两种配置各 60 项过程隔离定向测试全部通过。正式 main.nut + 原生 MCP 两种配置回归成功，证据目录 build/dev-sync-evidence、build/dev-sync-object-evidence。未声称全平台 CI、Release 或 GPU validation 全绿。

提交排除 external/ECS.hpp 的本地第三方补丁。SHARED 的 manifest-derived link-group anchors 保留仅含静态注册的模块，避免裁剪 authoring host 没有 API 引用时 provider 被链接器丢弃；CTest NAME_PREFIX 将 composition runner 与普通 domain runner 的重复用例名称隔离，保留原过滤器和过程隔离。它们是原生 authoring composition 路径所需的接线修复，不新增独立模块声明渠道。
