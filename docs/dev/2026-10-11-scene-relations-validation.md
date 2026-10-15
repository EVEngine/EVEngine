# 场景关系摆放：第一阶段实现与验证

本阶段复用已有 ScenePhysicsPlacementBackend / SceneEditorSession，不新增物理引擎、
视觉理解或资产检索工具。已实现作者关系、对象对接触证据和正式 MCP 预览/提交链。
遮挡测量、多对象独立联合求解、支撑 patch 和自动修复仍按设计留待后续阶段。

## 已实现行为

- eve.scene.authoring v2 增加 relations；v1 显式迁移为空数组。严格校验未知类型/字段、
  有限数、容差、唯一关系 ID 与对象引用。恢复失败不发布；删除叶节点连同相关关系
  一次提交，undo 一并恢复。hierarchy/live 文档不接受作者关系，不另建关系状态仓库。
- scene.authoring.relations.set.v1 以 value 数组替换全部关系；支持 supported_on 和
  nonpenetrating。前者要求指定对象的碰撞代理接触、world Y-up 法线匹配，以及分离/
  穿透在容差内；后者验证指定对象对的穿透深度。容差采用场景长度单位。
- 复用 World3D.queryBodyContacts，输出稳定 scene ObjectId 对，而非世界 contact 总数。
  查询超过预算、未步进、对象缺席或选择组禁用内部碰撞，报告 Unmeasurable。
- beginRelationPlacement 支持一个可调整对象；其余 admitted 对象为静态环境，锁定
  对象保留为障碍，选中锁定对象拒绝。所有与选中对象相关的关系参与者必须入场。
  不相关关系 NotEvaluated，hardSatisfied 只覆盖 incident-to-selection 范围。
- checkRelationPlacement 返回逐关系 status、contactCount、penetration、supportNormalDot、
  tolerance 和精度 collision-proxy，以及世界空间 previewObjects。不推进仿真、不改变
  场景。几何代理是使用者声明的精度；不保证真实 mesh、抗倾倒稳定性或物体间距离。
- commitPhysicsPlacement 在作者文档有关系时重新检查相关硬约束；违规或不可测量则
  拒绝，保留预览便于修正。版本冲突拒绝提交。既有 transform batch 事务一次提交，
  一次撤销/重做；普通无关系交互摆放保留原有行为。
- 物理预览的对象/复合 collider/handle 旋转与场景统一为 Y-X-Z。world/local 分解也采用
  相同约定；无法用 TRS 表示的父级剪切仍明确拒绝。

报告格式为 `eve.scene.relation-report` version 1；消费者拒绝未知版本，允许忽略新增的
只读证据字段。输入关系仍由作者文档 v2 与版本化命令严格校验。

## AI 接口

继续使用 eve_scene_modify，不增加第二套工具状态。示例关系：

```json
{"action":"relations","params":{"value":[
  {"id":"pot-on-table","type":"supported_on","subject":"pot","reference":"table","hard":true,"tolerance":0.02},
  {"id":"pot-wall","type":"nonpenetrating","subject":"pot","reference":"wall","hard":true,"tolerance":0.01}
]}}
```

1. relation_begin，target 指定可调整对象，params 可指定 mode 和 settings；默认 place。
   Director 用实际本地 bounds 生成带中心偏移的 box proxy，自动纳入整个有几何场景。
   params.colliders 可以按对象提供 compound parts；native caller 仍可使用原有缓存与
   immutable colliderResourceKey。无几何或退化 bounds 明确失败，不跳过障碍。
2. relation_update，params.pos 为 handle 世界位置，ticks 为 1..240，固定 dt=1/60。
   place/drag 会驱动 handle；fall/drop 按重力推进，pos 不改变下落初始位置，沿用旧后端
   的模式契约。需改变初始位置时先正式移动再 begin。旋转默认冻结，可用 settings 解锁。
3. relation_check 返回 Result.value 报告；根据违反对象和实际 previewObjects 调整 handle。
4. relation_commit 发布，relation_discard 取消。每个 Session 同时只有一个预览；修改接受
   expectedRevision，scene revision 改变使 check/commit 返回 Conflict。

当前没有独立 GPU 候选投影，预览期间截图仍是正式场景；提交后 refresh 才投影候选。
不能将这种截图用作预览遮挡证据。下一阶段通过独立渲染投影接入同帧 ID/depth 测量。
priority/weight/hard 是持久意图，本阶段只测量和拦截硬约束，不实现软目标自动搜索。
supported_on 是接触而非重心/支撑面覆盖证明；contained_in、clearance、occludes 等
尚未实现的关系类型返回 Unsupported。

## 生命周期与架构契约

关系由 SceneAuthoringDocumentTarget 独占，Session 独占目标、历史、预览和碰撞缓存。
关系使用持久 scene ObjectId；物理 body 与 renderable 不被持久化。预览拥有私有物理世界，
对象/shape 按既有 owner 顺序销毁；Session 销毁即清理预览。恢复/源配方变更导致 revision
失效，显式 collider cache 使用 resourceKey 检查；不能识别同一 revision 的磁盘内容替换。
不在每帧自动修复关系，不把支撑关系变成 transform parent。

公开失败路径使用 Result/nodiscard；内部 contactsComplete/hardSatisfied 是证据状态。
owner-thread-only，无持锁脚本回调。固定 dt/tick，无新增 RNG。scene editing 与 editor
共享既有模块依赖，不添加 graphics 反向 include，不新增 ECS System 或通用实体根。
碰撞证据查询为显式昂贵步骤；原来的 placementFrame 不追加每帧 contact 扫描。
six-face 目录在 scene shard 更新；无架构例外，无新裁剪等级声明。

## 验证入口与结果

新增 test/integration/gameplay-composition/scene_relations.cpp 覆盖 schema/migration、删除/撤销、违规/不可测量、指定支撑、
锁定障碍、无副作用预览、版本冲突与 Y-X-Z 复合旋转。定向 composition target 同时纳入
既有 scene_physics_placement.cpp，保证旧摆放模式回归覆盖。

```powershell
cmake/with-msvc.cmd cmake --build build/win32-debug --target eve scene_authoring_contract_test --parallel 8
ctest --test-dir build/win32-debug -L scene_authoring_contract_test -LE bundle --output-on-failure
python -X utf8 scripts/scene_authoring_smoke.py --exe build/win32-debug/src/engine/eve.exe --output build/relations-evidence
```

同一验证在 build/win32 OBJECT 配置重复。原生 smoke 通过正式 MCP 构建 floor/crate/wall，
拒绝未测量提交，下落 240 tick，检查 support 与 nonpenetrating，验证保存未变、提交及
一次撤销/重做。原生截图 relations-before.png / relations-after.png，接触证据与前后文档
在 relations-proof.json。保留既有参考反馈、GLB asset 和 SceneLoader live host 验证。

最终 SHARED/OBJECT 各 43 个定向、进程隔离测试全部通过，其中关系用例 6 个，既有物理
摆放回归 14 个。两种配置的原生 smoke 均通过：指定 floor 支撑 4 个接触点，法线 dot=1，
穿透在声明容差内，wall 对关系满足。检查 previewObjects 的 world 变换，再验证正式
提交/撤销/重做。它只证明当前案例的代理接触，不证明抗倾倒或真实 mesh 精度。

模块依赖图无新增反向边；73 条 composed catalogue 检查通过，改动行的独立架构诊断
（--base HEAD）通过，clang-format 18 改动行检查、ruff 和 git diff --check 通过。
默认 make check 仍失败：RenderCapture.h 3、SceneQuery.h 1、statepatch/StatePatch.h 1、
Procgen.cpp 54，共 59 条；四个文件相对 HEAD 无本任务修改。未更改基线、allowlist 或
豁免要求。完整日志 build/relations-source-check.log；独立诊断不替代默认门禁。
