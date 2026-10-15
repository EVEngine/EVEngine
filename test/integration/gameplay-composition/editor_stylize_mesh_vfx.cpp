#include "stylize/MeshVfxAsset.h"
#include "stylize/StyleInstance.h"
#include "stylize/editing/MeshVfxTarget.h"
#include "stylize/editing/StylizeTarget.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// editor_stylize_mesh_vfx.cpp
using namespace eve::editing;
using namespace eve::stylize_editing;

namespace {
SelectionSnapshot meshVfxSelection(const char* target) {
    SelectionSnapshot value;
    value.items.push_back({SelectionDomain::Asset, TargetId(target), StableId("mesh-vfx"), "stylize.mesh-vfx"});
    return value;
}
}  // namespace

TEST_CASE("editor.stylize_mesh_vfx edits undoes and persists canonical assets") {
    MeshVfxAssetTarget target("slash");
    const std::string  json = R"({
      "schema":"eve.stylize.mesh-vfx","schemaVersion":1,
      "layers":[{"style":"rim","playback":{"hold":0.3}}],
      "events":[{"time":0.5,"name":"hit"}]
    })";
    auto               operation =
        target.makeSet(meshVfxSelection("slash"), PropertyPath("asset.json"), json, PropertySetMode::Absolute);
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
    REQUIRE_EQ(target.asset().events.front().name, "hit");
    DomainOperation undo = operation.value();
    undo.payload         = operation.value().inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    REQUIRE(target.asset().events.empty());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
    MeshVfxAssetTarget restored("copy");
    REQUIRE(restored.loadSnapshot(target.snapshotValue()).ok());
    REQUIRE_EQ(restored.asset().events.front().name, "hit");
}

TEST_CASE("editor.stylize_mesh_vfx rejects invalid edits atomically") {
    MeshVfxAssetTarget target("slash");
    const auto         before = target.snapshotValue();
    auto operation = target.makeSet(meshVfxSelection("slash"), PropertyPath("asset.json"), R"({"schemaVersion":99})",
                                    PropertySetMode::Absolute);
    REQUIRE(!operation.ok());
    REQUIRE_EQ(target.snapshotValue(), before);
}

TEST_CASE("editor.stylize_mesh_vfx preview publication preserves the prior generation") {
    MeshVfxAssetTarget    target("slash");
    MeshVfxPreviewRuntime preview;
    REQUIRE(preview.publish(target).ok());
    auto* first = preview.instance();
    REQUIRE(first != nullptr);
    const auto revision = preview.revision();
    auto       operation =
        target.makeSet(meshVfxSelection("slash"), PropertyPath("asset.json"),
                       R"({"schema":"eve.stylize.mesh-vfx","schemaVersion":1,"layers":[{"style":"missing-style"}]})",
                       PropertySetMode::Absolute);
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
    REQUIRE(!preview.publish(target).ok());
    REQUIRE(preview.instance() == first);
    REQUIRE_EQ(preview.revision(), revision);
}

// editor_stylize_target.cpp
using namespace eve::stylize_editing;
using namespace eve::editing;

namespace {
SelectionSnapshot select(const StylizeRecipeTarget& target, std::initializer_list<const char*> ids) {
    SelectionSnapshot s;
    s.channel = "stylize";
    for (const char* id : ids)
        s.items.push_back({SelectionDomain::Asset, TargetId(target.targetId()), StableId(id), "stylize.pass"});
    return s;
}
void apply(StylizeRecipeTarget& target, Result<DomainOperation> operation) {
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
}
}  // namespace

TEST_CASE("editor.stylize.recipe_supports_dynamic_parameters_order_and_undo") {
    StylizeRecipeTarget target("look");
    apply(target, target.makeCreate(ObjectId("water"), "watercolor"));
    apply(target, target.makeCreate(ObjectId("pixels"), "pixel"));
    CHECK_EQ(target.passes().size(), static_cast<std::size_t>(2));
    const auto selection = select(target, {"pixels"});
    CHECK(target.schema(selection).find(PropertyPath("parameter.pixelSize")) != nullptr);
    auto parameter = target.makeSet(selection, PropertyPath("parameter.pixelSize"), 12.0, PropertySetMode::Absolute);
    REQUIRE(parameter.ok());
    REQUIRE(target.applyDomainOperation(parameter.value()).ok());
    CHECK_EQ(target.passes()[1].overrides.at("pixelSize"), 12.0);
    DomainOperation undo = parameter.value();
    undo.payload         = parameter.value().inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    CHECK(target.passes()[1].overrides.empty());
    apply(target, target.makeMove(ObjectId("pixels"), 0));
    CHECK_EQ(target.passes()[0].id.value(), std::string("pixels"));
    CHECK_EQ(target.passes()[0].priority, 0);
    CHECK_EQ(target.passes()[1].priority, 1);
    CHECK_EQ(static_cast<int>(target
                                  .makeSet(select(target, {"pixels"}), PropertyPath("parameter.pixelSize"), 100.0,
                                           PropertySetMode::Absolute)
                                  .code()),
             static_cast<int>(EditorStatus::Rejected));
}

TEST_CASE("editor.stylize.recipe_rejects_mixed_stages_and_loads_atomically") {
    StylizeRecipeTarget target("look");
    apply(target, target.makeCreate(ObjectId("cartoon"), "cartoon"));
    CHECK_EQ(static_cast<int>(target.makeCreate(ObjectId("water"), "watercolor").code()),
             static_cast<int>(EditorStatus::Rejected));
    const auto  before  = target.snapshotValue();
    EditorValue invalid = before;
    auto*       root    = invalid.getIf<EditorValue::Object>();
    auto*       content = (*root)["content"].getIf<EditorValue::Object>();
    auto*       passes  = (*content)["passes"].getIf<EditorValue::Array>();
    auto*       pass    = (*passes)[0].getIf<EditorValue::Object>();
    (*pass)["style"]    = "missing";
    CHECK_EQ(static_cast<int>(target.loadSnapshot(invalid).code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(target.snapshotValue(), before);
    StylizeRecipeTarget restored("copy");
    REQUIRE(restored.loadSnapshot(before).ok());
    CHECK_EQ(restored.passes()[0].style, std::string("cartoon"));
}

TEST_CASE("stylize.instance_accepts_authored_priority_override") {
    eve::stylize::StyleInstance instance("pixel");
    const int                   builtIn = instance.getPriority();
    instance.setPriority(-7);
    CHECK_EQ(instance.getPriority(), -7);
    instance.resetPriority();
    CHECK_EQ(instance.getPriority(), builtIn);
}

TEST_CASE("editor.stylize.runtime_rejects_stale_generation_before_graphics_use") {
    StylizeRecipeRuntime runtime;
    CHECK_EQ(static_cast<int>(runtime.apply(nullptr, nullptr, nullptr, Revision(1)).code()),
             static_cast<int>(EditorStatus::Conflict));
}
