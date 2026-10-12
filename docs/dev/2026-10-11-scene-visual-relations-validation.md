# 持久视觉关系与原生提交检查

本阶段把已有的原生遮挡测量接入作者文档和物理预览提交。没有新开发图像拆分、资产选择或搜索代理。

## 关系协议

作者文档仍为尚未发布的 eve.scene.authoring v2；新增关系类型属于该版本的严格关系数组。v1 显式迁移为空关系；未知字段、未知类型和非法比例拒绝，完整恢复原子验证。删除 subject/reference 叶节点会在同一事务删除关系，撤销恢复。

- visible_fraction: subject 的可见像素比例处于 [minimum, maximum]；minimum 必填，maximum 默认 1。
- occludes: subject 在前方实际挡住 reference 的像素比例处于 [minimum, maximum]。
- hard 默认 true；priority/weight 作为反馈保留，soft 不阻止提交。本阶段没有实现自动联合搜索。
- 视觉关系禁止 contact 专用 tolerance/normalMinDot。比例单位是屏幕像素比，不是世界距离。
- fullPixels 小于 4、深度有歧义或不支持的材质均不能放行硬约束。

例如 MCP relations 的 value：
[{"id":"show","type":"visible_fraction","subject":"target","minimum":0.7},
 {"id":"cover","type":"occludes","subject":"blocker","reference":"target","minimum":0.2,"maximum":0.3}]

relation_begin(blocker) 自动准备原生投影证据。relation_update(pos,ticks) 和 relation_check 返回物理关系、视觉关系 fraction/residual、原生 visibilityEvidence 及候选 world TRS。relation_commit 再测一次后才发布一次 transform batch；拒绝保留预览；discard 不改文档。准备失败会取消刚建立的预览。移动任意对象可能改变其他目标的遮挡，因此全部视觉关系都评估；物理关系只评估与选中对象相关的对象对。

## 所有权与接口

SceneAuthoringDocumentTarget 是持久意图唯一所有者。SceneEditorSession 拥有私有 Box3D 候选和临时拥有型请求/证明；不缓存 ECS/网格指针。物理候选生成不改 live SceneHost，不先移动再恢复对象。

common/ISceneVisibilityEvidence 是可选能力。graphics 启动时绑定、匹配销毁时撤销；scene/editor 只在显式编辑调用中查询，禁止向 graphics 添加依赖。没有提供者时视觉路径明确 Unsupported，纯物理路径仍工作。没有新增 ECS System、事件、持久原生 Link 或新的裁剪层级声明。

每个预览只能成功准备一次投影证明；变更资源必须 discard/begin，不能重新准备覆盖失效证明。
prepareRelationVisibility 验证相机等于作者镜头、源版本等于预览版本、初始模型等于作者 world matrix。原生提供者验证 named SceneHost 的完整可见映射，稳定 object id 对应 generation-qualified Renderable3D；错误标签/不完整 draws 拒绝。证明包含 host/entity generation、网格上传 generation 和 live node matrix。网格上传 generation 在 computeBounds 中由单一原生单调计数分配；用于静态网格资源失效检测，不是持久格式。

每次 check/commit 从最新 PhysicsPlacementFrame 原生生成候选矩阵，再验证投影证明和网格代次。原生测量返回拥有型证据并直接评估；没有接受外部截图或客户端 hardSatisfied 的提交入口。文档相机/资源/关系等命令变更会改变文档 revision，提交首先拒绝过期预览。

接口只在 owner/render 线程同步执行，不调用脚本回调，不持锁。准备是有界线性验证；check/commit 是显式阻塞 GPU 工作，成本为一个场景 pass 加每目标一个 isolated pass/readback。像素边缘受量化影响；遮挡需 NDC depth epsilon 1e-6。本阶段仍仅支持 untextured opaque static single-part renderables（同对象可以多个 renderables）、整对象 mask 和一个可调整叶节点。区域 mask、alpha-test/动画蒙皮和多对象联合搜索待后续阶段。

## 验证命令

cmake/with-msvc.cmd cmake --build build/win32-debug --target eve scene_authoring_contract_test --parallel 8
ctest --test-dir build/win32-debug -L scene_authoring_contract_test -LE bundle --output-on-failure
python -X utf8 scripts/scene_authoring_smoke.py --exe build/win32-debug/src/engine/eve.exe --output build/visual-relations-evidence --port 7540

定向测试包括持久往返/删除撤销、非法区间原子拒绝、真正遮挡者归属、深度歧义拒绝及提供者缺失。原生 MCP smoke 验证全遮挡拒绝、约四分之一遮挡通过、一次提交/撤销/重做、错误相机/版本/对象标签/初始模型/缺失 draws 拒绝和网格替换后提交拒绝；输出引擎截图与 visual-relations-proof.json。正式运行结果追加在下方。

应用了 Result/nodiscard、唯一所有者、严格 schema/原子恢复、可选能力显式缺失、无跨帧指针、显式成本和模块目录契约；没有豁免架构规则。

## 当前运行结果

SHARED Debug（严格警告 ON）与 OBJECT Debug（现有配置严格警告 OFF）均编译链接成功，各通过 51 个进程隔离定向契约。不是 Release/全平台验证。两种原生 MCP smoke 均验证真实 GPU 与脚本接口；测试没有把编译通过当作运行证明。

SHARED 的 hard visible_fraction >=0.7 与 occludes [0.2,0.3] 案例：初始可见 0、遮挡 1，两条 Violated 且 commit 拒绝；候选可见 0.735294、遮挡 0.264706，两条 Satisfied，原子 commit/undo/redo 成功；目标移到画外时两条 Unmeasurable，拒绝提交。相机修改、网格替换后 commit 拒绝。错误初始相机/版本/映射/模型/不完整 draws 均不能准备，证明不能被重新准备覆盖。畸形 evidence 返回 Failed；测量结果的 sourceRevision/candidateGeneration/camera/viewport/readbackComplete 必须匹配当前请求。

原生证据：build/visual-relations-evidence/visual-relations-proof.json 和 visual-relations-committed.png；OBJECT 对照在 build/visual-relations-object-evidence。截图由引擎截图路径生成。前置通用参考图案例的 silhouette IoU 1.0，仅证明该固定几何案例，不代表任意效果图复原率。

默认 make check 仍失败，当前 59 条均在四个本工作树未改动文件：common/RenderCapture.h 3、common/SceneQuery.h 1、statepatch/StatePatch.h 1、procgen/Procgen.cpp 54。对四文件的 git diff HEAD 为空；没有新增 baseline/allowlist、没有放行门禁。HEAD 诊断检查（74 个目录契约）及 module_depgraph --check 通过，它们不替代默认源门禁。格式/ruff/UTF-8 检查另见日志。
