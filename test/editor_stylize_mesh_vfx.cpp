#include "stylize_editing/MeshVfxTarget.h"

#include "stylize/MeshVfxAsset.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::editing;
using namespace eve::stylize_editing;

namespace {
SelectionSnapshot meshVfxSelection(const char* target) {
    SelectionSnapshot value;
    value.items.push_back({SelectionDomain::Asset, TargetId(target), StableId("mesh-vfx"), "stylize.mesh-vfx"});
    return value;
}
}

TEST_CASE("editor.stylize_mesh_vfx edits undoes and persists canonical assets") {
    MeshVfxAssetTarget target("slash");
    const std::string json = R"({
      "schema":"eve.stylize.mesh-vfx","schemaVersion":1,
      "layers":[{"style":"rim","playback":{"hold":0.3}}],
      "events":[{"time":0.5,"name":"hit"}]
    })";
    auto operation = target.makeSet(meshVfxSelection("slash"), PropertyPath("asset.json"), json,
                                    PropertySetMode::Absolute);
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
    REQUIRE_EQ(target.asset().events.front().name, "hit");
    DomainOperation undo = operation.value();
    undo.payload = operation.value().inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    REQUIRE(target.asset().events.empty());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
    MeshVfxAssetTarget restored("copy");
    REQUIRE(restored.loadSnapshot(target.snapshotValue()).ok());
    REQUIRE_EQ(restored.asset().events.front().name, "hit");
}

TEST_CASE("editor.stylize_mesh_vfx rejects invalid edits atomically") {
    MeshVfxAssetTarget target("slash");
    const auto before = target.snapshotValue();
    auto operation = target.makeSet(meshVfxSelection("slash"), PropertyPath("asset.json"),
                                    R"({"schemaVersion":99})", PropertySetMode::Absolute);
    REQUIRE(!operation.ok());
    REQUIRE_EQ(target.snapshotValue(), before);
}

TEST_CASE("editor.stylize_mesh_vfx preview publication preserves the prior generation") {
    MeshVfxAssetTarget target("slash");
    MeshVfxPreviewRuntime preview;
    REQUIRE(preview.publish(target).ok());
    auto* first = preview.instance();
    REQUIRE(first != nullptr);
    const auto revision = preview.revision();
    auto operation = target.makeSet(meshVfxSelection("slash"), PropertyPath("asset.json"),
        R"({"schema":"eve.stylize.mesh-vfx","schemaVersion":1,"layers":[{"style":"missing-style"}]})",
        PropertySetMode::Absolute);
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
    REQUIRE(!preview.publish(target).ok());
    REQUIRE(preview.instance() == first);
    REQUIRE_EQ(preview.revision(), revision);
}
