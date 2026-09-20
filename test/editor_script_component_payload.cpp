#include "ScriptTest.h"

#include "common/ScriptComponentStore.h"
#include "scene/editor/ScriptComponentPayloadProvider.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// 脚本组件在 inspector 里的只读属性（J-3 编辑器通路，注册表级端到端）。
//
// 三段接线：场景节点 → 脚本实体（§9.72 的原生读路径）→ 已登记组件的字段值（引擎侧权威存储）。
// 值不来自脚本实例的槽位，所以脚本内外读到的是同一份数据。
//
// 单独一个 TU：`ScriptTest.h` 带 `using namespace eve;`，和 `editor_scene_component_payload.cpp`
// 里的 `using namespace eve::editor;` 混在一起会让 `Revision`/`TransactionId` 变成二义符号。
// ---------------------------------------------------------------------------

namespace {

using namespace eve;
using namespace eve::scene_editing;

const char* kScriptComponentPayloadScript = R"SQ(
class InspectProbe extends eve.Component { x = 1.5; y = 2.5 }
class InspectEntity extends eve.SceneEntity { probe = InspectProbe }

function buildInspectorScene() {
    local registered = eve.component("InspectProbe", 1, InspectProbe).ok
    local scene = eve.Scene()
    scene.beginBuild()
    scene.beginNode("root", "")
    scene.addNode("probe", "")
    scene.end()
    scene.mountBuildAs("editorprobe")
    local e = scene.attachEntity("probe", InspectEntity)
    if (e == null) return false
    e.probe.x = 42.0
    return registered
}

// 同名同版本再登记 = 换了一代：用来看"引用过期"能不能被发现。
function advanceInspectorGeneration() {
    class InspectProbeV2 extends eve.Component { x = 1.5; y = 2.5 }
    return eve.component("InspectProbe", 1, InspectProbeV2).ok
}
)SQ";

class ScriptComponentPayloadTest : public ScriptTest {
public:
    ScriptComponentPayloadTest() : ScriptTest(kScriptComponentPayloadScript) {}
};

}  // namespace

TEST_CASE_FIXTURE(ScriptComponentPayloadTest, "editor.scene.script_component_payload_reads_declared_fields") {
    REQUIRE(callScript("buildInspectorScene"));

    eve::scene_editor::ScriptComponentPayloadProvider provider;
    SceneComponentPayloadRegistry                          registry;
    REQUIRE(registry.registerProvider(&provider).ok());
    // 一类槽只允许一个提供方（脚本组件刻意共用一个类型槽）。
    CHECK_EQ(static_cast<int>(registry.registerProvider(&provider).code()),
             static_cast<int>(EditorStatus::Conflict));

    const TargetId sceneTarget("editorprobe");
    const ObjectId objectId("probe");
    auto           references = registry.componentPayloads(sceneTarget, objectId);
    REQUIRE(references.ok());
    REQUIRE_EQ(references.value().size(), std::size_t{1});
    const auto& reference = references.value().front();
    CHECK_EQ(reference.type, eve::scene_editor::ScriptComponentPayloadProvider::payloadType());
    // 稳定组件 id 把节点编进去：`SelectionItem` 不带节点，`read`/`schema` 只能从这里恢复它。
    CHECK_EQ(reference.component.value(), std::string("probe|InspectProbe@1"));
    CHECK(reference.generation >= 1);

    // 没有脚本的节点、未知宿主都返回空引用表（编辑器据此显示"这个对象没有脚本组件"）。
    CHECK(registry.componentPayloads(sceneTarget, ObjectId("root")).value().empty());
    CHECK(registry.componentPayloads(TargetId("nope"), objectId).value().empty());

    auto selection = makeSceneComponentSelection("inspector", references.value(), 3);
    REQUIRE(selection.ok());
    auto properties = registry.propertyProvider(selection.value());
    REQUIRE(properties.ok());

    const PropertySchema schema = properties.value()->schema(selection.value());
    CHECK_EQ(schema.typeId, eve::scene_editor::ScriptComponentPayloadProvider::payloadType());
    REQUIRE_EQ(schema.properties.size(), std::size_t{2});  // x, y
    for (const PropertyDescriptor& descriptor : schema.properties)
        CHECK(hasPropertyFlag(descriptor.flags, PropertyFlag::ReadOnly));

    const PropertyReadResult x = properties.value()->read(selection.value(), PropertyPath("x"));
    CHECK_EQ(static_cast<int>(x.state), static_cast<int>(PropertyReadState::Value));
    REQUIRE(x.value.getIf<double>() != nullptr);
    CHECK_EQ(*x.value.getIf<double>(), 42.0);
    const PropertyReadResult y = properties.value()->read(selection.value(), PropertyPath("y"));
    REQUIRE(y.value.getIf<double>() != nullptr);
    CHECK_EQ(*y.value.getIf<double>(), 2.5);
    // 没声明的字段是 Missing，不是"读到空值"。
    CHECK_EQ(static_cast<int>(properties.value()->read(selection.value(), PropertyPath("nope")).state),
             static_cast<int>(PropertyReadState::Missing));

    // 只读 v1：写与操作目标明确拒绝（`Unsupported`），而不是"计划成功但什么都没发生"。
    CHECK_EQ(static_cast<int>(registry.operationTarget(selection.value()).code()),
             static_cast<int>(EditorStatus::Unsupported));
    CHECK_EQ(static_cast<int>(properties.value()
                                  ->makeSet(selection.value(), PropertyPath("x"), 1.0, PropertySetMode::Absolute)
                                  .code()),
             static_cast<int>(EditorStatus::Unsupported));

    // 引用过期：同名同版本换了一代之后，同一个引用必须被报成 Conflict（不是继续显示旧值）。
    CHECK(registry.validatePayload(reference).ok());
    REQUIRE(callScript("advanceInspectorGeneration"));
    auto stale = registry.validatePayload(reference);
    CHECK(!stale.ok());
    CHECK_EQ(static_cast<int>(stale.code()), static_cast<int>(EditorStatus::Conflict));
}
