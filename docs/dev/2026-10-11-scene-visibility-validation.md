# 场景遮挡测量接口与验证

本次交付第二阶段的原生测量基础：明确提交的 authoring draw set、候选矩阵与相机共同决定证据。
关系数据仍由 SceneAuthoringDocument v2 唯一拥有；物理摆放仍使用已有隔离世界和单次提交。
视觉阈值尚未进入 SceneEditorSession 的持久关系和提交门禁，联合求解是后续阶段。

## 接口

MCP `eve_scene_modify` 的 `visibility` action：target 指定目标，或 params.targets 指定至多 16 个目标。
默认宽度 256，高度按当前窗口纵横比取整；只指定一边时另一边保持该纵横比。两边均指定时使用明确的查询 viewport。
每边至多 2048，单张至多 1048576 像素，全部 pass 至多 4194304 像素。
`preview=true` 使用已有物理预览中的 world TRS，只支持可调整叶节点；`models={ObjectId:columnMajor16}`
提交显式候选世界矩阵。两者都不修改实时 SceneHost、文档、历史或渲染组件。
摄像机使用文档 eye/target/up/fov/near/far；相机裁剪之外的面积不计入遮挡分母。
继承的隐藏节点不进入 draw set。外部宿主、其他游戏实体不在此 authoring 查询的覆盖范围。

原生两步接口 `SceneVisibilityQuery::prepare(Value)` / `measure(Graphics&)`。
Squirrel 为 `eve.prepareSceneVisibility(request)` -> owning query -> `query.measure(gfx)`。
prepare 只验证和复制输入；measure 单次消费，在主线程解析 generation-qualified Renderable3D。
临时网格指针只在同步测量调用内借用；查询不保留 Graphics 或 Mesh 指针，不调用脚本/回调。
失效实体、重复句柄、无效矩阵/相机、超出预算、未知版本/字段明确失败，不留下可观察场景变更。
直接调用此底层接口时 sourceRevision 是调用方标签；MCP Director 在同一个 owner-thread 调用内从 session 取值。
证据不能当作后续正式提交的授权令牌。每次候选变化必须重新测量。

## 测量定义

场景 pass 输出前景 ObjectId 与 D32 float NDC 深度；每个目标另做同相机的 isolated pass，
保留其多个网格之间的自遮挡，移除外部对象。每次 pass 使用私有 5 个颜色附件、深度附件与 framebuffer，
GPU 完成后才读取；不会把候选附件挂到实时 RenderControl。共享的 G-buffer pipeline 仅在必要时惰性初始化。
ID 按实际 G-buffer RGB6 tint 协议打包/解包，最多 4096 draws，没有沿用旧捕获的过时 RGBA8 tint 假设。

`visibleFraction = visiblePixels / fullPixels`。
`occluders[].fraction` 分母同为 fullPixels，只有前景深度至少近 1e-6 NDC 才计入对应遮挡对象。
没有使用屏幕包围盒。目标少于 4 个像素为 Unmeasurable；不能认定满足可见性约束。
无法解释或深度重合的隐藏像素单独记 ambiguousPixels，状态 AmbiguousDepth。
自遮挡不算外部遮挡；其他对象可按实际 ObjectId 报告，不限于指定的一对。

证据 schema `eve.scene.visibility-evidence` v1：captureId、sourceRevision、candidateGeneration、完整相机/viewport、
ObjectId 映射、查询矩阵与 generation-qualified 实体列表、目标像素统计、精度和 readbackComplete。
这些是同一个同步不可变候选的多次 GPU submission，**不是**实时 presented frameId 或缓存截图。
目前不输出可跨帧复用的 geometry fingerprint，也不保存区域掩码；后续提交门禁不能省略资源版本校验。

## 支持范围

支持不透明、无贴图、无自定义 shader 的静态网格；同 ObjectId 的多个 Renderable3D 聚合。
贴图/alpha-test、packed material、透明/毛发、蒙皮、未烘焙 morph、LOD、实例化和 X-ray 明确 Unsupported。
部分支持不得静默当成硬约束满足。不会模拟折射、体积或灯光后处理的颜色可见性。
WebGPU 等尚无实现的 backend 走结构化 Unsupported，不退回包围盒。

## 验证

原生 smoke 包含 0/25/50/100% 遮挡、前后深度交换、offscreen、多个网格聚合、透明拒绝、
失效句柄、token 重复消费、物理候选渲染、文档与历史隔离；截图来自引擎捕获。
`test/scene_visibility.cpp` 包含 schema、相机退化、未知字段、预算、provider 缺失及失效 query 测试。
同一流程在 SHARED/OBJECT 分别通过 46 个局部合约测试与实际引擎 smoke。
已知目标 fullPixels=1936，四档 visibleFraction 精确为 1、0.75、0.5、0；遮挡来源均为 blocker。
候选移到后方 visibleFraction=1，offscreen 为 Unmeasurable；查询前后实时截图逐像素一致。

开启 Vulkan validation 的实际运行和不调用 visibility 的对照运行均有 20 条既有报错：
VUID-vkCmdDraw-None-09600、VUID-vkUpdateDescriptorSets-None-03047 各 10 条。
新增路径发现并修正的深度 aspect、复制布局报错已消失，不能据此宣称整个渲染器 validation clean。

默认 `make check` 仍有 59 个既有架构发现：RenderCapture.h 3、SceneQuery.h 1、StatePatch.h 1、Procgen.cpp 54。
上述四个文件相对 HEAD 无改动；未修改基线、allowlist 或豁免门禁。
日志和引擎 PNG/JSON 位于 build/visibility-evidence、build/visibility-object-evidence、
build/visibility-validation、build/visibility-validation-control，以及 build/visibility-*.log。

本次没有增加 ECS System、持久格式版本或跨模块向上依赖；Result、owner-thread 生命周期、两步成本与六面目录规则适用，无豁免。


## 本地复现

以下是该工作树已配置的 Windows Debug 构建（SHARED 开启 strict warnings，OBJECT 不是 Release 发布验证）：

```powershell
cmake/with-msvc.cmd cmake --build build/win32-debug --target eve scene_authoring_contract_test --parallel 8
ctest --test-dir build/win32-debug -L scene_authoring_contract_test -LE bundle --output-on-failure
python -X utf8 scripts/scene_authoring_smoke.py --exe build/win32-debug/src/engine/eve.exe --output build/visibility-evidence --port 7540
cmake/with-msvc.cmd cmake --build build/win32 --target eve scene_authoring_contract_test --parallel 8
ctest --test-dir build/win32 -L scene_authoring_contract_test -LE bundle --output-on-failure
python -X utf8 scripts/scene_authoring_smoke.py --exe build/win32/src/engine/eve.exe --output build/visibility-object-evidence --port 7541
$env:EVENGINE_VULKAN_VALIDATION='1'
python -X utf8 scripts/scene_authoring_smoke.py --exe build/win32-debug/src/engine/eve.exe --output build/visibility-validation --port 7540
python -X utf8 scripts/scene_authoring_smoke.py --exe build/win32-debug/src/engine/eve.exe --output build/visibility-validation-control --port 7540 --skip-visibility
make check
```

`check_architecture_contracts.py --base HEAD` 的诊断检查通过，但它不替代上述默认基线门禁。
UTF-8 脚本规范的 10 个单元测试也通过，`git diff --check` 无空白错误。
