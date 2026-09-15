#include "fluids/editor/FluidsEditorModule.h"

#include "fluids/editing/VolumeFluidTarget.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::editor;
using namespace eve::fluids_editor;

TEST_CASE("editor.fluids_factory_creates_all_schema_targets") {
    FluidsAutomationTargetFactory factory;
    for (const auto* type : {"fluid-simulation", "surface-fluid", "volume-fluid"}) {
        REQUIRE(factory.supports(type));
        auto created = factory.create(TargetId(std::string("target-") + type), type, {});
        REQUIRE(created.ok());
        REQUIRE(created.value().target != nullptr);
        CHECK_EQ(created.value().target->describe().type, std::string(type));
    }
    CHECK(!factory.supports("unknown"));
    CHECK_EQ(static_cast<int>(factory.create(TargetId("unknown"), "unknown", {}).code()),
             static_cast<int>(EditorStatus::Unsupported));
}

TEST_CASE("editor.fluids_factory_loads_volume_snapshot_atomically") {
    eve::fluids_editing::VolumeFluidTarget source("source");
    EditorValue::Object                    request;
    request.emplace("snapshot", source.snapshotValue());

    FluidsAutomationTargetFactory factory;
    auto                          created = factory.create(TargetId("volume"), "volume-fluid", request);
    REQUIRE(created.ok());
    auto* volume = dynamic_cast<eve::fluids_editing::VolumeFluidTarget*>(created.value().target.get());
    REQUIRE(volume != nullptr);
    CHECK_EQ(volume->snapshotValue(), source.snapshotValue());

    auto* root = request.at("snapshot").getIf<EditorValue::Object>();
    REQUIRE(root != nullptr);
    (*root)["unknown"] = true;
    CHECK_EQ(static_cast<int>(factory.create(TargetId("bad"), "volume-fluid", request).code()),
             static_cast<int>(EditorStatus::Unsupported));
}
