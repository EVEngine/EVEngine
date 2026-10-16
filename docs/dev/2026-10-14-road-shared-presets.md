# 共享道路类型与字段覆盖

日期：2026-10-14。复审清单第一组的作者核心实现，覆盖共享类型、道路/marker/设施字段覆盖、名称与锁定；专用作者面板与自动设施规则仍未标为完成。

## 作者状态与消费路径

RoadPresetDocument 是一个作者文档的唯一 owner。RoadPresetDraft.source 保存拓扑、作者控制点以及取消类型绑定后的 fallback style；types 保存稳定字符串 ID、parentId 和显式字段覆盖，bindings 保存网络内 edgeId、typeId 和实例覆盖。
source style 与 resolved style 有不同语义：前者是未绑定时使用的作者值，后者只能由定义和覆盖解析，不可独立编辑或持久化。
调用 beginEdit 得到独立 builder，编辑后 prepare 完整验证和解析，commit 发布；修改 builder 不影响 live state。
network() 只借用只读派生图，直接传给已有 bakeRoadNetwork；调用期间不得编辑文档或保留跨 mutation 借用。
没有复制 Unity scene 单例，没有 ECS、provider、事件、时间或 RNG，也没有按帧字符串查询。

## 覆盖与更新传播

顺序为 native RoadStyle 默认值 → 父预设链 → 当前预设 → 道路实例显式覆盖；marker 宽度/横坡按父预设默认 → 当前预设默认 → 道路默认 → marker 显式覆盖解析；没有默认时保留 source 对应值，取消绑定后恢复 source。
预设可以用 project/scene 等层级组织，但层级名称不改变解析规则。允许绑定到父预设或叶预设。
RoadStyle 所有当前字段都有 owning optional 覆盖，包含框架、宽度策略、截面、桥隧、设施、几何、标线、交通和侧物开关。
absence 继承；明确的 false、0 和空数组都是覆盖，不能被默认值判断吞掉。
style 数组字段整体替换；其后 binding.facilities 按稳定设施 ID 应用局部字段覆盖，未覆盖的几何/区间继续继承。清除字段覆盖恢复继承；取消类型绑定恢复 source 中的 fallback style。
修改类型后所有绑定边在 prepare 的私有图中统一解析，实例覆盖保留；任何失败都不发布前面成功解析的边。

最多 256 个预设、4096 个节点/边/绑定、128 个历史帧。拒绝重复 ID、循环父链、悬空父预设/绑定和无效最终 style；未使用的预设也通过 native style 约束验证。
metadata 保存道路显示名称和 direct-edit lock，不作为几何或身份真源。锁定拒绝 source 边、共享端点及 binding 的直接编辑；候选显式解锁可允许修改，预设更新仍传播，历史撤销/恢复可以恢复锁定。
修改 source 拓扑后必须在同一个 builder 中维护 bindings；悬空引用不会悄悄被丢弃。
反向/拆分道路的源截面、设施与区间，需要作者同时维护对应实例覆盖和绑定，当前没有自动迁移绑定的专用命令。

## 保存、撤销与失败边界

eve.procgen.roadPresets schemaVersion=1 保存完整作者历史和 cursor。派生网络、运行实例 stamp 和准备中的 token 均不保存。
canonical common Value 是公开持久化边界，内部复用现有 EditingProperty 的转换与 RoadNetwork codec；没有另建通用 Value 转换机制。
未知对象字段忽略，未知 schema/version 拒绝；嵌套 source 路网沿用原 schema-v1 迁移。
原生 RoadStyle 默认值构成 preset v1 的根语义，未来更改默认值须配套版本迁移。
恢复先检查树深 64、元素 262144、字符串/JSON 8 MiB 上限，再完整解析所有历史帧；私有候选全部成功后才获得 commit token。
捕获过大历史明确失败，不截断历史或悄悄丢弃作者数据。
undo/redo 同时切换源图、预设、绑定和派生图；保存时保留 redo 分支，加载后可以继续 redo。
prepare token 绑定文档实例和 revision；跨 owner、过期和已消费 token 被拒绝。失败不改变 revision、当前图或 redo。

## 成本、层级与验证

beginEdit 是图大小的 owning builder 拷贝；prepare 是显式昂贵解析阶段；commit 只移动最多 128 个共享历史引用，不烘焙网格。
API 同步、owner-thread-only，不持锁调用回调。Result/nodiscard、单一事实 owner、两阶段昂贵操作、原子恢复与六面模块目录规则适用；无架构例外。
作者类属于现有 procgen/editing satellite，runtime procgen-core-only 不引入编辑依赖。独立 STATIC/SHARED CPU 生产源码闭包验证 L2，正式 core-only 编译只提供 L1；不声称完整引擎 L3/L4 裁剪。

test/procgen_road_presets.cpp 覆盖父链传播与实例保护、实际 native bake 变化、JSON 往返与 redo、循环/悬空/非法候选、foreign/stale token、未知版本/字段、明确 false/空数组、清除覆盖和取消绑定。
可运行 shared-presets 夹具经过更新 → undo → JSON 保存/恢复 → redo → native bake，保存 preset_state.json、authoring.json 和 mesh_data.nut。
实帧位于 build/road-parity/preset-scenes/shared-presets/ev-frame.png，由 EVEngine presented-frame readback 取得；不将它计作 Unity 共享类型更新的对照验收。

仍保留：跨文档/项目预设资产同步、专用作者面板、layer/tag/static/castShadow 消费投影、更丰富 marker 参数、自动设施规则、编辑命令自动迁移类型/marker 绑定，以及 authored-network 到标准 recipe/artifact 入口。
缺失可选 metadata、pointDefaults、markers、facilities 字段迁移为无名称/未锁定/无新增覆盖；明确 0、false 和空数组仍保留 presence。
后续新增测试覆盖 marker 优先级、继承更新保护显式零横坡、设施局部覆盖、锁定/解锁、共享端点保护，以及恢复历史末帧失败不发布任何状态。

最终 STATIC/SHARED 聚焦回归各 37 项、6,450 断言通过，无警告/失败。共享预设作者场景 4,912 顶点、2,454 三角形已取得真实 readback；输入 JSON/mesh 与帧 SHA-256 保存在该场景 evidence.json。完整 renderer 使用现有构建上传当前 core 生成的网格，不声称 renderer 整体重新构建或 Unity 类型传播验收通过。

组合校验在完整最终边记录上执行，避免宽度与 marker 缩放的合法联合更新被中间接入口形状拒绝；新用例同时确认 active/blocked lane links 保留。每边最多 4096 marker，与嵌套 roadNetwork codec 的恢复上限一致。预设/绑定/marker/设施覆盖按身份规范排序。提交明确区分 foreign owner 的 Rejected 与 revision 冲突的 Conflict。
增量 make check CI_BASE=HEAD 完整通过（268 项脚本测试，269.027 秒）；其后增加的联合接入口、预算与状态回归已通过双链接聚焦测试，最终架构和改动行格式另复核。默认门禁仍有 IStateProvider.h 三项 bool API 与 RenderSystem.h 两项生命周期注释违规，不以增量结果替代默认门禁。

## 横坡根默认迁移

后续道路默认横坡政策改为 SmoothStep 后，roadPresets 输出升级为 schemaVersion 2。读取 v1 历史会对无显式政策的根类型注入 Linear 覆盖，保存旧继承行为；源 roadNetwork 缺字段也保留 Linear。全部历史候选验证后发布，新 v2 不再使用旧根默认。见 [真实横坡契约](2026-10-14-road-roll-reference-contract.md)。
## 采样默认迁移补充（2026-10-14）

当前保存schemaVersion3，接受旧v1/v2。新根类型采样默认2m/45°/ParametricDistanceTurn来自实际创建读回；旧v1/v2在每个历史帧的根类型上将缺失采样值物化为0m/4°/MaximumIntervals，以保留旧生成行为。子类型继续继承，显式零、角度、策略及数组保持presence。v1的roll迁移同时执行，恢复仍先构造和验证全部私有状态，最后以同一instance/revision token提交。未知对象字段忽略，未知schema/版本及非法已知字段拒绝。

