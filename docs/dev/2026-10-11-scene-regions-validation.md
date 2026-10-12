# 对象局部区域可见性与版本绑定

本阶段复用现有实际网格 ID/depth 测量、物理候选和提交历史，增加局部区域约束。没有开发图像拆分、资产选择或新的 AI 搜索工具。

## 数据与调用

作者文档 eve.scene.authoring 升到 v3；v1 添加空关系后迁移到 v3，v2 保留原关系迁移到 v3。旧版本文档中不能塞入新 region 字段。恢复仍完整验证后原子发布；未知字段/版本拒绝。未改动普通 scene hierarchy/live 目标的持久协议。

visible_fraction.region 定义在 subject 局部空间；occludes.region 定义在 reference 局部空间。区域是关系中的不可变选择定义，同 object+region.id 出现多次时必须具有完全相同的定义，冲突拒绝。作者文档是唯一持久所有者；解析/测量副本均为拥有型瞬时数据，没有第二个区域可变注册表，也不保存 ECS/GPU 指针。

region 格式为 eve.scene.local-region v1：
{"schemaId":"eve.scene.local-region","schemaVersion":1,"kind":"local_box","id":"window",
 "geometryRevision":"由引擎读取的版本","minimum":[-0.45,0.1,-0.6],"maximum":[0.45,0.45,0.6]}

最低/最高三轴均须有限且严格递增；不接受零厚度或 screen_rectangle。该 box 是选择对象实际表面的局部体积，不能解释成精确自动识别的窗户多边形。

MCP relations 动作中省略 region.geometryRevision 时，Director 读取原生 session.geometryRevision(object) 并写入持久定义；已有 token 不会被悄悄覆盖。geometry_revision 动作可查询 token。原生正式命令要求完整版本化定义。几何 revision 来自权威 source 的 kind/seed/mesh_params/path/revision 的确定性规范序列化，带 eve.scene.geometry-recipe/1 标识；TRS/材质不参与。资产 revision 继续遵循现有不可变资源承诺。资源原生上传变化另由上一阶段 host/entity/mesh generation 证明捕获。

源几何配方改变后区域意图保留，但下次 prepare 明确 Conflict，要求显式重新绑定。活跃预览的正式变更首先由 sourceRevision 拒绝。删除测量对象会把其区域关系与节点一并删除，undo 一并恢复。

## 实际像素算法

visibility-request v2 的 targets 同时支持旧 object 字符串和 {object,region}；v1 字符串请求仍能使用。visibility-evidence 输出 v2，各 regional target 带完整 region 定义。

1. 同一候选相机和矩阵渲染全场景 ID/depth。
2. 每个目标对象单独渲染一次，保留自遮挡；同对象的多区域复用该 fullMask/depth。
3. 用像素中心、实际 D32 深度和 inverse(projection*view*model) 还原目标表面点到对象局部空间。
4. 仅保留位于 local_box 内的真实目标三角形像素，再测 visibleFraction / 指定前方 occluder 的贡献。
5. 对象+region.id+完整定义匹配证据；不能拿整对象结果或其他区域结果放行。

没有投影矩形面积替代区域 mask；sphere 投影包围盒的空角落区域必须是空 mask。self-occluded 部位不进入 full 分母。画面裁剪仍排除在分母外，不承诺 in_frame。区域边界容差为 1e-4*max(1,abs(min),abs(max)) 局部单位，并写入证据；深度前后判定继续用 NDC epsilon 1e-6。少于 4 个区域像素或深度歧义不能满足硬关系。

每对象多 mesh 必须共享同一个对象局部 model；不一致则 Unsupported。当前支持 local_box 选择实际 opaque 静态表面；没有实现 polygon、alpha-test、半透明、变形/蒙皮或多个可调整对象联合搜索。

## 架构与成本

共用 SceneLocalRegion 值协议在 common；scene/editing 拥有严格持久 codec/几何 token/原生 target planning，scene/editor 复用既有 capability 和候选提交，graphics 执行像素反投影。没有 scene/editor -> graphics include，没有新 ECS System 或运行时 per-frame 查找。

全部 owner-thread 同步值操作，无回调/持锁/跨帧裸指针。check/commit 仍是显式昂贵操作，预算沿用 16 targets、最大 4096 draws、合计 4194304 raster 像素；GPU pass 为一个场景加每个不同对象一个 full pass。多区域另增逐像素 CPU 过滤，复用同对象 readback。提供者缺失继续明确 Unsupported，纯物理路径可用。没有新增裁剪层级声明或架构例外。

## 验证

定向契约覆盖 v1/v2 迁移、区域往返/删除撤销、非法 bounds/字段/版本原子拒绝、同 id 定义冲突、TRS/material 保留几何 token、source 变化失效、整对象/不同区域证据隔离与旧 query 格式。

原生 MCP 场景：house 上部 window 需 >=95% 可见，tree 需遮住 lower-wall >=95%。初始 tree 在上部时拒绝；移动到下部后两条约束满足并一次提交/undo/redo；另验证旋转后局部区域、几何版本变化及恢复、sphere 空角落。

命令：
cmake/with-msvc.cmd cmake --build build/win32-debug --target eve scene_authoring_contract_test --parallel 8
ctest --test-dir build/win32-debug -L scene_authoring_contract_test -LE bundle --output-on-failure
python -X utf8 scripts/scene_authoring_smoke.py --exe build/win32-debug/src/engine/eve.exe --output build/scene-regions-evidence --port 7540
make check

实际结果与范围以末尾记录为准。默认源门禁的既有失败不作为新代码豁免。

## 最终记录（2026-10-12）

SHARED（严格警告开启）与 OBJECT Debug 两种配置均完成 eve/scene_authoring_contract_test 编译链接，各 56 个过程隔离定向测试全部通过。OBJECT 配置未声称 Release 验证。两种配置的正式 main.nut + 原生 MCP smoke 均成功：上部 window 从 0 可见到 100% 可见，下部指定遮挡从 0 到 100%；不满足时提交拒绝，满足后一次历史提交及 undo/redo 成功。旋转、几何 recipe 变化及 undo 恢复均覆盖。sphere whole fullPixels=860，而空角落 regional fullPixels=0，状态 Unmeasurable。

最终证据：build/scene-regions-final-evidence/regions-proof.json、regions-window-visible.png，以及 build/scene-regions-object-evidence/ 对应文件。截图人工检查与像素测量一致。固定测试场景的 IoU=1.0 仅证明回归案例，不代表任意效果图还原精度。

默认 make check 已执行，仍失败于 59 项既有架构问题：RenderCapture.h 3 项 bool API、SceneQuery.h 1 项、StatePatch.h 1 项、Procgen.cpp 54 项运行时 ModuleManager。四文件相对 HEAD 无变更；没有更改基线/allowlist。新增 header 解析生命周期告警通过将 codec 实现移到 common/SceneLocalRegion.cpp 修复。额外 --base HEAD 差分诊断通过；该诊断不替代默认门禁。git diff --check 通过。

开启 Vulkan validation 的区域 smoke 记录了已在前阶段发现的两类 VUID：vkCmdDraw-None-09600、vkUpdateDescriptorSets-None-03047（日志总计 20 次标识出现）；本次未修复这些既有问题，也不声称 GPU validation 全绿。最终复验默认关闭 validation。

适用规则已落实：拥有型 Result/nodiscard 接口、作者文档单一所有者、严格 schema/version/迁移及原子恢复、几何过期检测、显式测量成本和预算、既有 provider 缺失拒绝契约、模块边界与原生提交重新验证。没有新增 ECS System/裁剪声明，没有批准或采用架构例外。
