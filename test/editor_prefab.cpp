#include "editor/EditorPrefab.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::editor;

namespace {

void applyAll(SceneTargetBase& scene, const std::vector<DomainOperation>& operations) {
    for (const DomainOperation& operation : operations) REQUIRE(scene.applyDomainOperation(operation).accepted());
}

SceneDocumentTarget sourceScene() {
    SceneDocumentTarget scene("source");
    auto root = scene.makeCreate({ObjectId("vehicle"), {}, "Vehicle", {1.0, 2.0, 3.0}});
    REQUIRE(root.value);
    REQUIRE(scene.applyDomainOperation(*root.value).accepted());
    auto wheel = scene.makeCreate({ObjectId("wheel"), ObjectId("vehicle"), "Wheel",
                                   {0.5, 0.0, 0.0, 0.0, 0.0, 0.0, 0.8, 0.8, 0.8}});
    REQUIRE(wheel.value);
    REQUIRE(scene.applyDomainOperation(*wheel.value).accepted());
    return scene;
}

}  // namespace

TEST_CASE("editor.prefab.capture_and_instantiate_preserve_stable_mapping") {
    ScenePrefabService prefabs;
    SceneDocumentTarget source = sourceScene();
    auto captured = prefabs.capture(AssetGuid("asset://prefabs/vehicle"), source, ObjectId("vehicle"));
    REQUIRE(captured.value);
    CHECK_EQ(captured.value->objects.size(), size_t{2});

    SceneDocumentTarget level("level");
    auto parent = level.makeCreate({ObjectId("garage"), {}, "Garage", {}});
    REQUIRE(parent.value);
    REQUIRE(level.applyDomainOperation(*parent.value).accepted());
    auto instance = prefabs.instantiate(*captured.value, "vehicle-01", ObjectId("garage"), level);
    REQUIRE(instance.value);
    CHECK_EQ(instance.value->sourceRevision, captured.value->revision);
    CHECK_EQ(instance.value->operations.size(), size_t{2});
    applyAll(level, instance.value->operations);
    CHECK_EQ(level.sceneObject(ObjectId("vehicle-01/vehicle")).value->parent, ObjectId("garage"));
    CHECK_EQ(level.sceneObject(ObjectId("vehicle-01/wheel")).value->parent,
             ObjectId("vehicle-01/vehicle"));

    const auto collision = prefabs.instantiate(*captured.value, "vehicle-01", ObjectId("garage"), level);
    CHECK_EQ(static_cast<int>(collision.status), static_cast<int>(EditorStatus::Conflict));
}

TEST_CASE("editor.prefab_apply_and_revert_overrides_are_revisioned_and_reversible") {
    ScenePrefabService prefabs;
    SceneDocumentTarget source = sourceScene();
    auto captured = prefabs.capture(AssetGuid("vehicle"), source, ObjectId("vehicle"));
    REQUIRE(captured.value);
    SceneDocumentTarget level("level-overrides");
    auto instance = prefabs.instantiate(*captured.value, "instance", {}, level);
    REQUIRE(instance.value);
    applyAll(level, instance.value->operations);

    auto rename = level.makeRename(ObjectId("instance/wheel"), "Big Wheel");
    REQUIRE(rename.value);
    REQUIRE(level.applyDomainOperation(*rename.value).accepted());
    SceneTransformValue changed{2.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.5, 1.5, 1.5};
    auto transform = level.makeSetTransform(ObjectId("instance/wheel"), changed);
    REQUIRE(transform.value);
    REQUIRE(level.applyDomainOperation(*transform.value).accepted());

    auto badges = prefabs.inspectOverrides(*captured.value, "instance", {}, level);
    REQUIRE(badges.value);
    CHECK_EQ(badges.value->size(), size_t{2});
    CHECK_EQ(badges.value->at(0).instanceObject, ObjectId("instance/wheel"));

    auto applied = prefabs.applyOverrides(*captured.value, "instance", {}, level);
    REQUIRE(applied.value);
    CHECK_EQ(applied.value->revision, captured.value->revision + 1);
    CHECK_EQ(applied.value->objects[1].name, "Big Wheel");
    CHECK_EQ(applied.value->objects[1].transform, changed);

    auto revert = prefabs.revertOverrides(*captured.value, "instance", {}, level);
    REQUIRE(revert.value);
    CHECK_EQ(revert.value->size(), size_t{2});
    applyAll(level, *revert.value);
    CHECK_EQ(level.sceneObject(ObjectId("instance/wheel")).value->name, "Wheel");
    CHECK_EQ(level.readTransform(ObjectId("instance/wheel")).value,
             captured.value->objects[1].transform);
}

TEST_CASE("editor.prefab_nested_dependencies_detect_cycles_and_refresh_revision_pins") {
    ScenePrefabService prefabs;
    auto parent = prefabs.capture(AssetGuid("parent"), sourceScene(), ObjectId("vehicle"));
    auto child = prefabs.capture(AssetGuid("wheel-prefab"), sourceScene(), ObjectId("wheel"));
    REQUIRE(parent.value); REQUIRE(child.value);
    child.value->revision = 3;
    parent.value->objects[1].nestedPrefab = child.value->asset;
    parent.value->objects[1].nestedRevision = 1;
    const auto resolver = [&](const AssetGuid& asset) {
        if (asset == child.value->asset) return EditorResult<PrefabAssetSnapshot>::applied(*child.value);
        return EditorResult<PrefabAssetSnapshot>::error(EditorStatus::NotFound,
            RuleId("test.missing"), "missing");
    };
    const auto dependencies = prefabs.inspectDependencies(*parent.value, resolver);
    CHECK_EQ(static_cast<int>(dependencies.status), static_cast<int>(EditorStatus::Applied));
    REQUIRE(!dependencies.diagnostics.empty());
    auto refreshed = prefabs.refreshNestedRevisions(*parent.value, resolver);
    REQUIRE(refreshed.value);
    CHECK_EQ(refreshed.value->revision, parent.value->revision + 1);
    CHECK_EQ(refreshed.value->objects[1].nestedRevision, 3U);

    child.value->objects[0].nestedPrefab = parent.value->asset;
    child.value->objects[0].nestedRevision = parent.value->revision;
    const auto cyclicResolver = [&](const AssetGuid& asset) {
        if (asset == child.value->asset) return EditorResult<PrefabAssetSnapshot>::applied(*child.value);
        if (asset == parent.value->asset) return EditorResult<PrefabAssetSnapshot>::applied(*parent.value);
        return EditorResult<PrefabAssetSnapshot>::error(EditorStatus::NotFound,
            RuleId("test.missing"), "missing");
    };
    CHECK_EQ(static_cast<int>(prefabs.inspectDependencies(*parent.value, cyclicResolver).status),
             static_cast<int>(EditorStatus::Rejected));
}

TEST_CASE("editor.prefab_snapshot_round_trip_rejects_invalid_hierarchy") {
    ScenePrefabService prefabs;
    SceneDocumentTarget source = sourceScene();
    auto captured = prefabs.capture(AssetGuid("vehicle"), source, ObjectId("vehicle"));
    REQUIRE(captured.value);
    const EditorValue snapshot = prefabs.snapshotValue(*captured.value);
    auto restored = prefabs.loadSnapshot(snapshot);
    REQUIRE(restored.value);
    CHECK_EQ(prefabs.snapshotValue(*restored.value), snapshot);

    EditorValue broken = snapshot;
    auto* object = broken.getIf<EditorValue::Object>();
    REQUIRE(object);
    auto* rows = object->at("objects").getIf<EditorValue::Array>();
    REQUIRE(rows);
    auto* wheel = rows->at(1).getIf<EditorValue::Object>();
    REQUIRE(wheel);
    (*wheel)["parentSourceId"] = "missing";
    CHECK_EQ(static_cast<int>(prefabs.loadSnapshot(broken).status), static_cast<int>(EditorStatus::Rejected));
}
