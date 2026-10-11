#include "ScriptTest.h"

#include "common/Capability.h"
#include "common/ECS.h"
#include "common/ScriptComponentStore.h"
#include "editor/Editor.h"
#include "editor/EditorAutomationTargetFactory.h"
#include "editor/EditorTransactionService.h"
#include "scene/editor/SceneEditorModule.h"
#include "scene/editor/ScriptComponentPayloadProvider.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// 脚本组件在 inspector 里的属性通路（J-3 编辑器通路，注册表级端到端）。
//
// 三段接线：场景节点 → 脚本实体（§9.72 的原生读路径）→ 已登记组件的字段值（引擎侧权威存储）。
// 值不来自脚本实例的槽位，所以脚本内外读到的是同一份数据；写回也走同一个存储，因此脚本侧
// 下一次读就看见新值。
//
// 单独一个 TU：`ScriptTest.h` 带 `using namespace eve;`，和 `editor_scene_component_payload.cpp`
// 里的 `using namespace eve::editor;` 混在一起会让 `Revision`/`TransactionId` 变成二义符号。
// ---------------------------------------------------------------------------

namespace {

using namespace eve;
using namespace eve::scene_editing;

const char* kScriptComponentPayloadScript = R"SQ(
probeEntity <- null
untypedEntity <- null

class InspectProbe extends eve.Component { x = 1.5; y = 2.5; label = "probe" }
class InspectEntity extends eve.SceneEntity { probe = InspectProbe }
// 声明成 null 的字段没有类型可校验：它必须保持只读，而当前值照样能被看见。
class UntypedProbe extends eve.Component { untyped = null; count = 3 }
class UntypedEntity extends eve.SceneEntity { probe = UntypedProbe }

function buildInspectorScene() {
    local registered = eve.component("InspectProbe", 1, InspectProbe).ok
    registered = eve.component("UntypedProbe", 1, UntypedProbe).ok && registered
    local scene = eve.Scene()
    scene.beginBuild()
    scene.beginNode("root", "")
    scene.addNode("probe", "")
    scene.addNode("untyped", "")
    scene.end()
    scene.mountBuildAs("editorprobe")
    local e = scene.attachEntity("probe", InspectEntity)
    if (e == null) return false
    e.probe.x = 42.0
    probeEntity = e
    local u = scene.attachEntity("untyped", UntypedEntity)
    if (u == null) return false
    u.probe.count = 7
    untypedEntity = u
    return registered
}

// 脚本侧的读写：用来钉"编辑器写入与脚本写入落到同一份权威数据上"，以及"计划基于过期数据时必须拒绝"。
function probeX() { return probeEntity.probe.x }
function bumpProbeX() { probeEntity.probe.x = 99.0; return true }

// 同名同版本再登记 = 换了一代：用来看"引用过期"能不能被发现。
function advanceInspectorGeneration() {
    class InspectProbeV2 extends eve.Component { x = 1.5; y = 2.5; label = "probe" }
    return eve.component("InspectProbe", 1, InspectProbeV2).ok
}
)SQ";

class ScriptComponentPayloadTest : public ScriptTest {
public:
    ScriptComponentPayloadTest() : ScriptTest(kScriptComponentPayloadScript) {}

protected:
    /** 脚本侧读到的值：证明"编辑器写的就是脚本读的那一份"。 */
    double scriptNumber(const char* function) { return vm.callFunc(vm.findFunc(function), vm).toFloat(); }
};

/** 域操作载荷/反向值里的一项（`editing::Value` 没有按名字取值的接口，只有类型化访问）。 */
const EditorValue* operationField(const EditorValue& payload, const char* name) {
    const auto* object = payload.getIf<EditorValue::Object>();
    if (object == nullptr) return nullptr;
    const auto found = object->find(name);
    return found == object->end() ? nullptr : &found->second;
}

/** 引擎侧存储里的一个字段值（测试直接从权威存储取证，不经过脚本）。 */
eve::Result<eve::Value> storedField(const char* componentName, const char* field) {
    for (const ScriptComponentStoreEntry& entry : scriptComponentStoreEntries()) {
        if (entry.key.name != componentName) continue;
        if (const eve::Value* value = entry.values.find(field)) return eve::Result<eve::Value>::success(*value);
        return eve::Result<eve::Value>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "field is not stored", field, {}, "test.script-component"));
    }
    return eve::Result<eve::Value>::failure(eve::Diagnostic::error(DiagnosticCode::NotFound, "component is not stored",
                                                                   componentName, {}, "test.script-component"));
}

/** 同一个字段的数值形态（脚本里的 `7` 存成整数、`42.0` 存成浮点）；读不到时返回 -1。 */
double storedNumber(const char* componentName, const char* field) {
    const auto value = storedField(componentName, field);
    if (!value.ok()) return -1.0;
    if (const auto* number = value.value().getIf<double>()) return *number;
    if (const auto* integer = value.value().getIf<std::int64_t>()) return static_cast<double>(*integer);
    return -1.0;
}

/** 某个已登记组件挂在哪个实体上（清数据的失败注入要用）。 */
std::uint64_t entityIdOf(const char* componentName) {
    for (const ScriptComponentStoreEntry& entry : scriptComponentStoreEntries())
        if (entry.key.name == componentName) return entry.key.entityId;
    return 0;
}

eve::Result<eve::editing::TransactionReceipt> commitOperation(IDomainOperationTarget&               target,
                                                              eve::editor::LocalTransactionBackend& transactions,
                                                              const DomainOperation&                operation,
                                                              eve::editing::Revision baseRevision, const char* id) {
    eve::editing::TransactionSpec specification;
    specification.id           = eve::editing::TransactionId(id);
    specification.label        = "Script component edit";
    specification.target       = TargetId(target.targetId());
    specification.baseRevision = baseRevision;
    auto begun                 = transactions.begin(std::move(specification));
    if (!begun.ok()) return eve::Result<eve::editing::TransactionReceipt>::failure(begun.status());
    auto appended = transactions.append(operation);
    if (!appended.ok()) return eve::Result<eve::editing::TransactionReceipt>::failure(appended.status());
    return transactions.commit();
}

/** 一次失败的提交会把"待处理事务"留在原地：下一次 begin 之前必须显式丢弃。 */
void discardPending(eve::editor::LocalTransactionBackend& transactions) { REQUIRE(transactions.discard().ok()); }

}  // namespace

TEST_CASE_FIXTURE(ScriptComponentPayloadTest, "editor.scene.script_component_payload_reads_declared_fields") {
    REQUIRE(callScript("buildInspectorScene"));

    eve::scene_editor::ScriptComponentPayloadProvider provider;
    SceneComponentPayloadRegistry                     registry;
    REQUIRE(registry.registerProvider(&provider).ok());
    // 一类槽只允许一个提供方（脚本组件刻意共用一个类型槽）。
    REQUIRE_EQ(static_cast<int>(registry.registerProvider(&provider).code()), static_cast<int>(EditorStatus::Conflict));

    const TargetId sceneTarget("editorprobe");
    const ObjectId objectId("probe");
    auto           references = registry.componentPayloads(sceneTarget, objectId);
    REQUIRE(references.ok());
    REQUIRE_EQ(references.value().size(), std::size_t{1});
    const auto& reference = references.value().front();
    REQUIRE_EQ(reference.type, eve::scene_editor::ScriptComponentPayloadProvider::payloadType());
    // 稳定组件 id 把节点编进去：`SelectionItem` 不带节点，`read`/`schema` 只能从这里恢复它。
    REQUIRE_EQ(reference.component.value(), std::string("probe|InspectProbe@1"));
    REQUIRE(reference.generation >= 1);

    // 没有脚本的节点、未知宿主都返回空引用表（编辑器据此显示"这个对象没有脚本组件"）。
    REQUIRE(registry.componentPayloads(sceneTarget, ObjectId("root")).value().empty());
    REQUIRE(registry.componentPayloads(TargetId("nope"), objectId).value().empty());

    auto selection = makeSceneComponentSelection("inspector", references.value(), 3);
    REQUIRE(selection.ok());
    auto properties = registry.propertyProvider(selection.value());
    REQUIRE(properties.ok());

    const PropertySchema schema = properties.value()->schema(selection.value());
    REQUIRE_EQ(schema.typeId, eve::scene_editor::ScriptComponentPayloadProvider::payloadType());
    REQUIRE_EQ(schema.properties.size(), std::size_t{3});  // x, y, label（声明顺序）
    REQUIRE_EQ(schema.properties.at(0).path.value(), std::string("x"));
    REQUIRE_EQ(schema.properties.at(1).path.value(), std::string("y"));
    REQUIRE_EQ(schema.properties.at(2).path.value(), std::string("label"));
    REQUIRE_EQ(static_cast<int>(schema.properties.at(0).type), static_cast<int>(PropertyType::Float));
    REQUIRE_EQ(static_cast<int>(schema.properties.at(2).type), static_cast<int>(PropertyType::String));
    for (const PropertyDescriptor& descriptor : schema.properties)
        REQUIRE(!hasPropertyFlag(descriptor.flags, PropertyFlag::ReadOnly));
    // 默认值来自**声明**，不是当前值：x 现在是 42.0，但脚本里写的是 1.5。
    REQUIRE_EQ(*schema.properties.at(0).defaultValue.getIf<double>(), 1.5);
    REQUIRE_EQ(storedNumber("InspectProbe", "x"), 42.0);

    const PropertyReadResult x = properties.value()->read(selection.value(), PropertyPath("x"));
    REQUIRE_EQ(static_cast<int>(x.state), static_cast<int>(PropertyReadState::Value));
    REQUIRE(x.value.getIf<double>() != nullptr);
    REQUIRE_EQ(*x.value.getIf<double>(), 42.0);
    const PropertyReadResult y = properties.value()->read(selection.value(), PropertyPath("y"));
    REQUIRE(y.value.getIf<double>() != nullptr);
    REQUIRE_EQ(*y.value.getIf<double>(), 2.5);
    // 没声明的字段是 Missing，不是"读到空值"。
    REQUIRE_EQ(static_cast<int>(properties.value()->read(selection.value(), PropertyPath("nope")).state),
               static_cast<int>(PropertyReadState::Missing));

    // 声明成 null 的字段：只读（没有类型可校验），但声明 schema 与当前值都看得见。
    auto untypedReferences = registry.componentPayloads(sceneTarget, ObjectId("untyped"));
    REQUIRE(untypedReferences.ok());
    REQUIRE_EQ(untypedReferences.value().size(), std::size_t{1});
    auto untypedSelection = makeSceneComponentSelection("inspector", untypedReferences.value(), 4);
    REQUIRE(untypedSelection.ok());
    auto untypedProperties = registry.propertyProvider(untypedSelection.value());
    REQUIRE(untypedProperties.ok());
    const PropertySchema untypedSchema = untypedProperties.value()->schema(untypedSelection.value());
    const auto           untypedField  = untypedSchema.find(PropertyPath("untyped"));
    REQUIRE(untypedField);
    REQUIRE(hasPropertyFlag(untypedField->flags, PropertyFlag::ReadOnly));
    REQUIRE_EQ(static_cast<int>(untypedField->type), static_cast<int>(PropertyType::ReadOnlyText));
    const auto countField = untypedSchema.find(PropertyPath("count"));
    REQUIRE(countField);
    REQUIRE(!hasPropertyFlag(countField->flags, PropertyFlag::ReadOnly));
    REQUIRE_EQ(static_cast<int>(countField->type), static_cast<int>(PropertyType::Int));
    REQUIRE_EQ(*countField->defaultValue.getIf<std::int64_t>(), 3);  // 声明 3，当前值 7
    const PropertyReadResult count = untypedProperties.value()->read(untypedSelection.value(), PropertyPath("count"));
    REQUIRE(count.value.getIf<std::int64_t>() != nullptr);
    REQUIRE_EQ(*count.value.getIf<std::int64_t>(), 7);

    // 引用过期：同名同版本换了一代之后，同一个引用必须被报成 Conflict（不是继续显示旧值）。
    REQUIRE(registry.validatePayload(reference).ok());
    REQUIRE(callScript("advanceInspectorGeneration"));
    auto stale = registry.validatePayload(reference);
    REQUIRE(!stale.ok());
    REQUIRE_EQ(static_cast<int>(stale.code()), static_cast<int>(EditorStatus::Conflict));
}

TEST_CASE_FIXTURE(ScriptComponentPayloadTest, "editor.scene.script_component_write_is_undoable_and_visible_to_script") {
    REQUIRE(callScript("buildInspectorScene"));

    eve::scene_editor::ScriptComponentPayloadProvider provider;
    SceneComponentPayloadRegistry                     registry;
    REQUIRE(registry.registerProvider(&provider).ok());
    const TargetId sceneTarget("editorprobe");

    auto references = registry.componentPayloads(sceneTarget, ObjectId("probe"));
    REQUIRE(references.ok());
    auto selection = makeSceneComponentSelection("inspector", references.value(), 11);
    REQUIRE(selection.ok());
    auto properties      = registry.propertyProvider(selection.value());
    auto operationTarget = registry.operationTarget(selection.value());
    REQUIRE(properties.ok());
    REQUIRE(operationTarget.ok());

    // 编辑器的撤销契约要求"能克隆候选、能原子发布候选"，所以操作目标必须实现 staging。
    auto* staging = dynamic_cast<IDomainOperationTargetStaging*>(operationTarget.value());
    REQUIRE(staging != nullptr);

    const eve::editing::Revision before = operationTarget.value()->revision();
    auto change = properties.value()->makeSet(selection.value(), PropertyPath("x"), 5.5, PropertySetMode::Absolute);
    REQUIRE(change.ok());
    REQUIRE(change.value().hasInverse);
    REQUIRE_EQ(change.value().type, std::string("eve.script-component.field.set.v1"));
    const auto* inverseValue = operationField(change.value().inverse, "value");
    REQUIRE(inverseValue != nullptr);
    REQUIRE(inverseValue->getIf<double>() != nullptr);
    REQUIRE_EQ(*inverseValue->getIf<double>(), 42.0);

    // 候选副本上的写入**不**碰权威存储：撤销可以"先在候选上试算，失败不留半套状态"。
    auto candidate = staging->cloneDomainState();
    REQUIRE(candidate != nullptr);
    REQUIRE(candidate->applyDomainOperation(change.value()).ok());
    REQUIRE_EQ(storedNumber("InspectProbe", "x"), 42.0);
    REQUIRE_EQ(scriptNumber("probeX"), 42.0);

    eve::editing::LocalWorldAuthority    authority(operationTarget.value());
    eve::editor::LocalTransactionBackend transactions(&authority);
    REQUIRE(commitOperation(*operationTarget.value(), transactions, change.value(), before, "script.x.1").ok());
    REQUIRE_EQ(storedNumber("InspectProbe", "x"), 5.5);
    // 脚本侧下一次读就看见新值：编辑器写的和脚本读的是同一份权威数据。
    REQUIRE_EQ(scriptNumber("probeX"), 5.5);
    REQUIRE(operationTarget.value()->revision() != before);

    REQUIRE(transactions.undo().ok());
    REQUIRE_EQ(storedNumber("InspectProbe", "x"), 42.0);
    REQUIRE_EQ(scriptNumber("probeX"), 42.0);

    REQUIRE(transactions.redo().ok());
    REQUIRE_EQ(storedNumber("InspectProbe", "x"), 5.5);
    REQUIRE_EQ(scriptNumber("probeX"), 5.5);

    // 脚本写回同一个字段之后，编辑器按新版本号再计划一次必须能提交（基于过期数据的计划见下一条用例）。
    REQUIRE(callScript("bumpProbeX"));
    REQUIRE_EQ(storedNumber("InspectProbe", "x"), 99.0);
    auto afterScript =
        properties.value()->makeSet(selection.value(), PropertyPath("x"), 1.25, PropertySetMode::Absolute);
    REQUIRE(afterScript.ok());
    REQUIRE(commitOperation(*operationTarget.value(), transactions, afterScript.value(),
                            operationTarget.value()->revision(), "script.x.2")
                .ok());
    REQUIRE_EQ(storedNumber("InspectProbe", "x"), 1.25);
    REQUIRE_EQ(scriptNumber("probeX"), 1.25);
}

TEST_CASE_FIXTURE(ScriptComponentPayloadTest, "editor.scene.script_component_planner_refuses_invalid_edits") {
    REQUIRE(callScript("buildInspectorScene"));

    eve::scene_editor::ScriptComponentPayloadProvider provider;
    SceneComponentPayloadRegistry                     registry;
    REQUIRE(registry.registerProvider(&provider).ok());
    const TargetId sceneTarget("editorprobe");

    auto references = registry.componentPayloads(sceneTarget, ObjectId("probe"));
    REQUIRE(references.ok());
    auto selection = makeSceneComponentSelection("inspector", references.value(), 21);
    REQUIRE(selection.ok());
    auto properties = registry.propertyProvider(selection.value());
    REQUIRE(properties.ok());

    // 没声明的字段：明确 NotFound，而不是计划一个写不进去的操作。
    REQUIRE_EQ(static_cast<int>(properties.value()
                                    ->makeSet(selection.value(), PropertyPath("nope"), 1.0, PropertySetMode::Absolute)
                                    .code()),
               static_cast<int>(EditorStatus::NotFound));
    REQUIRE_EQ(static_cast<int>(properties.value()->makeReset(selection.value(), PropertyPath("nope")).code()),
               static_cast<int>(EditorStatus::NotFound));
    // 类型不匹配：结构化的 Rejected，且带诊断。
    auto mismatched = properties.value()->makeSet(selection.value(), PropertyPath("x"), EditorValue("text"),
                                                  PropertySetMode::Absolute);
    REQUIRE_EQ(static_cast<int>(mismatched.code()), static_cast<int>(EditorStatus::Rejected));
    REQUIRE(!mismatched.diagnostics().empty());
    // 相对模式对"绝对赋值"的字段没有意义。
    REQUIRE_EQ(
        static_cast<int>(
            properties.value()->makeSet(selection.value(), PropertyPath("x"), 1.0, PropertySetMode::Relative).code()),
        static_cast<int>(EditorStatus::Rejected));

    // 声明成 null 的字段没有类型可校验：写与重置都明确拒绝。
    auto untypedReferences = registry.componentPayloads(sceneTarget, ObjectId("untyped"));
    REQUIRE(untypedReferences.ok());
    auto untypedSelection = makeSceneComponentSelection("inspector", untypedReferences.value(), 22);
    REQUIRE(untypedSelection.ok());
    auto untypedProperties = registry.propertyProvider(untypedSelection.value());
    REQUIRE(untypedProperties.ok());
    REQUIRE_EQ(static_cast<int>(
                   untypedProperties.value()
                       ->makeSet(untypedSelection.value(), PropertyPath("untyped"), 1.0, PropertySetMode::Absolute)
                       .code()),
               static_cast<int>(EditorStatus::Rejected));
    REQUIRE_EQ(static_cast<int>(
                   untypedProperties.value()->makeReset(untypedSelection.value(), PropertyPath("untyped")).code()),
               static_cast<int>(EditorStatus::Rejected));
}

TEST_CASE_FIXTURE(ScriptComponentPayloadTest, "editor.scene.script_component_write_refuses_stale_and_missing_targets") {
    REQUIRE(callScript("buildInspectorScene"));

    eve::scene_editor::ScriptComponentPayloadProvider provider;
    SceneComponentPayloadRegistry                     registry;
    REQUIRE(registry.registerProvider(&provider).ok());
    const TargetId sceneTarget("editorprobe");

    auto references = registry.componentPayloads(sceneTarget, ObjectId("probe"));
    REQUIRE(references.ok());
    auto selection = makeSceneComponentSelection("inspector", references.value(), 31);
    REQUIRE(selection.ok());
    auto properties      = registry.propertyProvider(selection.value());
    auto operationTarget = registry.operationTarget(selection.value());
    REQUIRE(properties.ok());
    REQUIRE(operationTarget.ok());
    auto* staging = dynamic_cast<IDomainOperationTargetStaging*>(operationTarget.value());
    REQUIRE(staging != nullptr);

    eve::editing::LocalWorldAuthority    authority(operationTarget.value());
    eve::editor::LocalTransactionBackend transactions(&authority);

    // ① 计划之后脚本改了同一个字段：按旧版本号提交必须 Conflict，且不留半套状态。
    auto stale = properties.value()->makeSet(selection.value(), PropertyPath("x"), 3.0, PropertySetMode::Absolute);
    REQUIRE(stale.ok());
    const eve::editing::Revision planned = operationTarget.value()->revision();
    REQUIRE(callScript("bumpProbeX"));
    REQUIRE_EQ(storedNumber("InspectProbe", "x"), 99.0);
    auto conflicted = commitOperation(*operationTarget.value(), transactions, stale.value(), planned, "script.stale");
    REQUIRE_EQ(static_cast<int>(conflicted.code()), static_cast<int>(EditorStatus::Conflict));
    REQUIRE_EQ(storedNumber("InspectProbe", "x"), 99.0);
    discardPending(transactions);

    // ② 过期的操作目标：提供方只服务一个选中组件，绑定被后来的选中项改写之后，旧操作必须被拒绝。
    auto laterReferences = registry.componentPayloads(sceneTarget, ObjectId("untyped"));
    REQUIRE(laterReferences.ok());
    auto laterSelection = makeSceneComponentSelection("inspector", laterReferences.value(), 32);
    REQUIRE(laterSelection.ok());
    auto rebound = registry.operationTarget(laterSelection.value());
    REQUIRE(rebound.ok());
    REQUIRE(rebound.value() == operationTarget.value());  // 同一个目标对象，只是身份换了
    const auto appliedToOther = operationTarget.value()->applyDomainOperation(stale.value());
    REQUIRE_EQ(static_cast<int>(appliedToOther.code()), static_cast<int>(EditorStatus::Conflict));
    REQUIRE_EQ(storedNumber("UntypedProbe", "count"), 7.0);

    // ③ 事务进行中发生热重载（同名同版本换了一代）：旧候选的发布必须拒绝，也不改动已迁移的数据。
    auto rebindProbe = registry.operationTarget(selection.value());
    REQUIRE(rebindProbe.ok());
    auto acrossReload =
        properties.value()->makeSet(selection.value(), PropertyPath("x"), 2.0, PropertySetMode::Absolute);
    REQUIRE(acrossReload.ok());
    auto reloadCandidate = staging->cloneDomainState();
    REQUIRE(reloadCandidate != nullptr);
    REQUIRE(reloadCandidate->applyDomainOperation(acrossReload.value()).ok());
    REQUIRE(callScript("advanceInspectorGeneration"));
    const auto reloadCommit = staging->commitDomainState(std::move(reloadCandidate));
    REQUIRE_EQ(static_cast<int>(reloadCommit.code()), static_cast<int>(EditorStatus::Conflict));
    REQUIRE_EQ(storedNumber("InspectProbe", "x"), 99.0);
}

/**
 * 数据在事务中途消失（实体销毁走同一条路）：候选发布与正向写入都必须拒绝，而且**不复活**存储条目。
 * 单独一条用例是因为热重载那一步已经推进了代际，"数据没了"与"换代了"两条拒绝理由会互相掩盖。
 */
TEST_CASE_FIXTURE(ScriptComponentPayloadTest, "editor.scene.script_component_write_is_refused_when_the_data_is_gone") {
    REQUIRE(callScript("buildInspectorScene"));

    eve::scene_editor::ScriptComponentPayloadProvider provider;
    SceneComponentPayloadRegistry                     registry;
    REQUIRE(registry.registerProvider(&provider).ok());
    const TargetId sceneTarget("editorprobe");

    auto references = registry.componentPayloads(sceneTarget, ObjectId("probe"));
    REQUIRE(references.ok());
    auto selection = makeSceneComponentSelection("inspector", references.value(), 33);
    REQUIRE(selection.ok());
    auto properties      = registry.propertyProvider(selection.value());
    auto operationTarget = registry.operationTarget(selection.value());
    REQUIRE(properties.ok());
    REQUIRE(operationTarget.ok());
    auto* staging = dynamic_cast<IDomainOperationTargetStaging*>(operationTarget.value());
    REQUIRE(staging != nullptr);

    eve::editing::LocalWorldAuthority    authority(operationTarget.value());
    eve::editor::LocalTransactionBackend transactions(&authority);

    // 候选是在数据还在时克隆的，发布时数据已经没了。
    auto beforeRemoval =
        properties.value()->makeSet(selection.value(), PropertyPath("x"), 2.0, PropertySetMode::Absolute);
    REQUIRE(beforeRemoval.ok());
    auto orphanCandidate = staging->cloneDomainState();
    REQUIRE(orphanCandidate != nullptr);
    REQUIRE(orphanCandidate->applyDomainOperation(beforeRemoval.value()).ok());
    const std::uint64_t entityId = entityIdOf("InspectProbe");
    REQUIRE(entityId != 0);
    REQUIRE(eve::removeScriptComponentValues(entityId) > 0);
    // 带副作用的调用必须先落到局部变量：`REQUIRE_EQ` 会把两个实参各求值两次（第二次为了拼
    // "expands to" 文本），把 `commitDomainState(std::move(...))` 直接写进断言会调用两次——
    // 第二次拿到已经搬空的候选，于是"拒绝"看起来通过了，而数据其实已经被第一次发布复活。
    const auto commitCode = staging->commitDomainState(std::move(orphanCandidate));
    REQUIRE_EQ(static_cast<int>(commitCode.code()), static_cast<int>(EditorStatus::Conflict));
    REQUIRE_EQ(entityIdOf("InspectProbe"), std::uint64_t{0});

    // 正向写入同样拒绝：提交时存储里已经没有这份数据。
    auto afterRemoval =
        properties.value()->makeSet(selection.value(), PropertyPath("x"), 2.0, PropertySetMode::Absolute);
    REQUIRE(afterRemoval.ok());
    auto rejected = commitOperation(*operationTarget.value(), transactions, afterRemoval.value(),
                                    operationTarget.value()->revision(), "script.gone");
    REQUIRE_EQ(static_cast<int>(rejected.code()), static_cast<int>(EditorStatus::NotFound));
    REQUIRE_EQ(entityIdOf("InspectProbe"), std::uint64_t{0});
    discardPending(transactions);
}

TEST_CASE_FIXTURE(ScriptComponentPayloadTest, "editor.scene.script_component_reset_restores_the_declared_default") {
    REQUIRE(callScript("buildInspectorScene"));

    eve::scene_editor::ScriptComponentPayloadProvider provider;
    SceneComponentPayloadRegistry                     registry;
    REQUIRE(registry.registerProvider(&provider).ok());
    const TargetId sceneTarget("editorprobe");

    auto references = registry.componentPayloads(sceneTarget, ObjectId("probe"));
    REQUIRE(references.ok());
    auto selection = makeSceneComponentSelection("inspector", references.value(), 41);
    REQUIRE(selection.ok());
    auto properties      = registry.propertyProvider(selection.value());
    auto operationTarget = registry.operationTarget(selection.value());
    REQUIRE(properties.ok());
    REQUIRE(operationTarget.ok());
    eve::editing::LocalWorldAuthority    authority(operationTarget.value());
    eve::editor::LocalTransactionBackend transactions(&authority);

    auto reset = properties.value()->makeReset(selection.value(), PropertyPath("x"));
    REQUIRE(reset.ok());
    REQUIRE_EQ(storedNumber("InspectProbe", "x"), 42.0);
    const auto* resetValue = operationField(reset.value().payload, "value");
    REQUIRE(resetValue != nullptr);
    REQUIRE(resetValue->getIf<double>() != nullptr);
    REQUIRE_EQ(*resetValue->getIf<double>(), 1.5);  // 声明默认值
    REQUIRE(commitOperation(*operationTarget.value(), transactions, reset.value(), operationTarget.value()->revision(),
                            "script.x.reset")
                .ok());
    REQUIRE_EQ(storedNumber("InspectProbe", "x"), 1.5);
    REQUIRE_EQ(scriptNumber("probeX"), 1.5);

    REQUIRE(transactions.undo().ok());
    REQUIRE_EQ(storedNumber("InspectProbe", "x"), 42.0);

    // 整数、字符串以外的类型同样按声明默认值重置；`Reset` 模式只是转给同一条计划。
    auto untypedReferences = registry.componentPayloads(sceneTarget, ObjectId("untyped"));
    REQUIRE(untypedReferences.ok());
    auto untypedSelection = makeSceneComponentSelection("inspector", untypedReferences.value(), 42);
    REQUIRE(untypedSelection.ok());
    auto untypedProperties = registry.propertyProvider(untypedSelection.value());
    auto untypedTarget     = registry.operationTarget(untypedSelection.value());
    REQUIRE(untypedProperties.ok());
    REQUIRE(untypedTarget.ok());
    eve::editing::LocalWorldAuthority    untypedAuthority(untypedTarget.value());
    eve::editor::LocalTransactionBackend untypedTransactions(&untypedAuthority);

    auto resetCount =
        untypedProperties.value()->makeSet(untypedSelection.value(), PropertyPath("count"), 2, PropertySetMode::Reset);
    REQUIRE(resetCount.ok());
    const auto* countTarget = operationField(resetCount.value().payload, "value");
    REQUIRE(countTarget != nullptr);
    REQUIRE(countTarget->getIf<std::int64_t>() != nullptr);
    REQUIRE_EQ(*countTarget->getIf<std::int64_t>(), 3);
    const auto* countInverse = operationField(resetCount.value().inverse, "value");
    REQUIRE(countInverse != nullptr);
    REQUIRE(countInverse->getIf<std::int64_t>() != nullptr);
    REQUIRE_EQ(*countInverse->getIf<std::int64_t>(), 7);
    REQUIRE(commitOperation(*untypedTarget.value(), untypedTransactions, resetCount.value(),
                            untypedTarget.value()->revision(), "script.count.reset")
                .ok());
    REQUIRE_EQ(storedNumber("UntypedProbe", "count"), 3.0);
    REQUIRE(untypedTransactions.undo().ok());
    REQUIRE_EQ(storedNumber("UntypedProbe", "count"), 7.0);

    // 字符串字段的重置也走同一条路（`label` 声明 "probe"）。
    auto resetLabel = properties.value()->makeReset(selection.value(), PropertyPath("label"));
    REQUIRE(resetLabel.ok());
    const auto* labelValue = operationField(resetLabel.value().payload, "value");
    REQUIRE(labelValue != nullptr);
    REQUIRE(labelValue->getIf<std::string>() != nullptr);
    REQUIRE_EQ(*labelValue->getIf<std::string>(), std::string("probe"));
}

/**
 * 生产接线：自动化创建的编辑器目标 id 是**调用方选的**（不等于宿主名），所以"目标 id → 宿主名"
 * 的映射由创建目标的模块（`SceneEditorModule`）注入。这条用例走的就是那条路：
 * `SceneEditorModule` 创建 `scene-host` 目标 → 目标上查组件 payload 能力 → 注册表里的脚本组件
 * 提供方按映射（`scene-live` → 宿主 `editorprobe`）列出组件并读到值。
 */
TEST_CASE_FIXTURE(ScriptComponentPayloadTest, "editor.scene.script_component_payload_is_wired_by_the_editor_module") {
    REQUIRE(callScript("buildInspectorScene"));

    // 场景编辑模块的构造要求命令注册表已经在位（它把自己的编辑命令注册进去）。
    eve::editor::Editor editor;

    auto* module = eve::scene_editor::SceneEditorModule::create();
    REQUIRE(module != nullptr);

    eve::editor::IEditorAutomationTargetFactory* factory = nullptr;
    const bool handled = eve::cap::forEachUntil<eve::editor::IEditorAutomationTargetFactory>(
        [&](eve::editor::IEditorAutomationTargetFactory* candidate) {
            if (!candidate->supports("scene-host")) return false;
            factory = candidate;
            return true;
        });
    REQUIRE(handled);
    REQUIRE(factory != nullptr);

    eve::editor::EditorValue::Object request;
    request.emplace("host", eve::editor::EditorValue(std::string("editorprobe")));
    auto created = factory->create(eve::editing::TargetId("scene-live"), "scene-host", request);
    REQUIRE(created.ok());
    REQUIRE(created.value().target != nullptr);

    auto* payloads = static_cast<ISceneComponentPayloadTarget*>(
        created.value().target->queryCapability(ISceneComponentPayloadTarget::editorCapabilityId()));
    REQUIRE(payloads != nullptr);

    // 目标 id 是 `scene-live`，宿主是 `editorprobe`：只有注入的映射能把它们连起来。
    auto references =
        payloads->componentPayloads(eve::editing::TargetId("scene-live"), eve::editing::ObjectId("probe"));
    REQUIRE(references.ok());
    REQUIRE_EQ(references.value().size(), std::size_t{1});
    REQUIRE_EQ(references.value().front().component.value(), std::string("probe|InspectProbe@1"));

    auto selection = makeSceneComponentSelection("inspector", references.value(), 7);
    REQUIRE(selection.ok());
    auto properties = payloads->propertyProvider(selection.value());
    REQUIRE(properties.ok());
    const PropertyReadResult x = properties.value()->read(selection.value(), PropertyPath("x"));
    REQUIRE(x.value.getIf<double>() != nullptr);
    REQUIRE_EQ(*x.value.getIf<double>(), 42.0);

    // 同一条通路上的写：编辑器的域操作目标也必须能从这条生产接线上拿到，并真的写到存储里。
    auto operationTarget = payloads->operationTarget(selection.value());
    REQUIRE(operationTarget.ok());
    auto change = properties.value()->makeSet(selection.value(), PropertyPath("y"), 6.25, PropertySetMode::Absolute);
    REQUIRE(change.ok());
    eve::editing::LocalWorldAuthority    authority(operationTarget.value());
    eve::editor::LocalTransactionBackend transactions(&authority);
    REQUIRE(commitOperation(*operationTarget.value(), transactions, change.value(), operationTarget.value()->revision(),
                            "script.live.y")
                .ok());
    REQUIRE_EQ(storedNumber("InspectProbe", "y"), 6.25);

    // 文档目标（`scene`）没有活宿主：同一个提供方给空表，而不是报错或串到别的宿主。
    auto documentTarget = factory->create(eve::editing::TargetId("scene-doc"), "scene", {});
    REQUIRE(documentTarget.ok());
    auto* documentPayloads = static_cast<ISceneComponentPayloadTarget*>(
        documentTarget.value().target->queryCapability(ISceneComponentPayloadTarget::editorCapabilityId()));
    REQUIRE(documentPayloads != nullptr);
    auto none =
        documentPayloads->componentPayloads(eve::editing::TargetId("scene-doc"), eve::editing::ObjectId("probe"));
    REQUIRE(none.ok());
    REQUIRE(none.value().empty());
}
