#include "biome_editing/BiomeTarget.h"
#include "procgen/PointSet.h"
#include "procgen/SpatialData.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
#include <map>
using namespace eve::biome_editing;
using namespace eve::editing;
namespace {
SelectionSnapshot select(const BiomeDocumentTarget& target, const ObjectId& id,
                         const std::string& type) {
    SelectionSnapshot selection;
    selection.channel = "biome";
    selection.items.push_back(
        {SelectionDomain::Asset, TargetId(target.targetId()), StableId(id.value()), type});
    return selection;
}
void apply(BiomeDocumentTarget& target, EditorResult<DomainOperation> operation) {
    REQUIRE(operation.value);
    REQUIRE(target.applyDomainOperation(*operation.value).isAccepted());
}
class SpatialResolver final : public IBiomeSpatialResolver {
public:
    EditorResult<eve::procgen::SpatialData*> resolve(const std::string& asset) const override {
        const auto found = assets.find(asset);
        if (found == assets.end())
            return EditorResult<eve::procgen::SpatialData*>::error(
                EditorStatus::NotFound, RuleId("test.biome.spatial"), "missing spatial asset");
        return EditorResult<eve::procgen::SpatialData*>::applied(found->second);
    }
    std::map<std::string, eve::procgen::SpatialData*> assets;
};
}  // namespace

TEST_CASE("editor.biome.properties_are_reversible_and_snapshot_load_is_atomic") {
    BiomeDocumentTarget target("world.biome");
    BiomeLayerValue layer{ObjectId("forest"), "forest", "areas/forest.spatial", 1, 1.f, {}};
    apply(target, target.makeCreateLayer(layer));
    apply(target, target.makeCreateAsset(
                      layer.id, {ObjectId("oak"), "models/oak.prefab", 2.f, .8f, 1.2f, true}));
    const auto selection = select(target, ObjectId("oak"), "biome.asset");
    const auto scale = target.makeSet(selection, PropertyPath("asset.scale"),
                                      EditorValue::Array{.5, 1.5}, PropertySetMode::Absolute);
    REQUIRE(scale.value);
    REQUIRE(target.applyDomainOperation(*scale.value).isAccepted());
    CHECK_EQ(target.layers()[0].assets[0].minScale, .5f);
    DomainOperation undo = *scale.value;
    undo.payload = scale.value->inverse;
    REQUIRE(target.applyDomainOperation(undo).isAccepted());
    CHECK_EQ(target.layers()[0].assets[0].minScale, .8f);

    const EditorValue before = target.snapshotValue();
    EditorValue invalid = before;
    auto* root = invalid.getIf<EditorValue::Object>();
    auto* content = (*root)["content"].getIf<EditorValue::Object>();
    auto* layers = (*content)["layers"].getIf<EditorValue::Array>();
    auto* first = (*layers)[0].getIf<EditorValue::Object>();
    (*first)["density"] = 2.0;
    CHECK_EQ(static_cast<int>(target.loadSnapshot(invalid).status),
             static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(target.snapshotValue(), before);
}

TEST_CASE("editor.biome.runtime_publishes_candidates_and_previews_deterministically") {
    BiomeDocumentTarget target("world.biome");
    apply(target, target.makeCreateLayer(
                      {ObjectId("forest"), "forest", "areas/forest.spatial", 1, 1.f, {}}));
    apply(target, target.makeCreateAsset(
                      ObjectId("forest"),
                      {ObjectId("oak"), "models/oak.prefab", 1.f, 1.f, 1.f, false}));
    eve::procgen::SpatialData domain =
        eve::procgen::SpatialData::box(0.f, 0.f, 0.f, 4.f, 0.f, 4.f);
    SpatialResolver resolver;
    resolver.assets["areas/forest.spatial"] = &domain;
    BiomeDocumentRuntime runtime;
    REQUIRE(runtime.publish(target, resolver).isAccepted());
    const Revision published = runtime.revision();
    auto first = runtime.preview(&domain, 2.f, 42, 0.f, published);
    auto second = runtime.preview(&domain, 2.f, 42, 0.f, published);
    REQUIRE(first.value.has_value());
    REQUIRE(second.value.has_value());
    REQUIRE(bool(*first.value));
    REQUIRE(bool(*second.value));
    CHECK_EQ((*first.value)->getCount(), (*second.value)->getCount());
    for (int i = 0; i < (*first.value)->getCount(); ++i) {
        CHECK_EQ((*first.value)->getStringAttribute(i, "asset", ""),
                 (*second.value)->getStringAttribute(i, "asset", ""));
        CHECK_EQ((*first.value)->getPointSeed(i), (*second.value)->getPointSeed(i));
    }
    CHECK_EQ(static_cast<int>(runtime.preview(&domain, 2.f, 42, 0.f, published + 1).status),
             static_cast<int>(EditorStatus::Conflict));
    SpatialResolver missing;
    CHECK_EQ(static_cast<int>(runtime.publish(target, missing).status),
             static_cast<int>(EditorStatus::NotFound));
    CHECK_EQ(runtime.revision(), published);
}
