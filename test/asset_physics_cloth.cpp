#include "asset/RuntimeDefinition.h"
#include "asset/physics/EvpackClothModel.h"
#include "physics/Cloth3D.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve;

namespace {

PersistentId persistentId(std::string_view text) {
    auto parsed = PersistentId::parse(text);
    REQUIRE(parsed);
    return *parsed;
}

AssetRef assetRef(std::string_view text) {
    auto parsed = AssetRef::parse(text);
    REQUIRE(parsed.hasValue());
    return std::move(parsed).takeValue();
}

std::shared_ptr<const asset::Evpack> makeClothPack(const Value& definition, bool addUnexpectedChunk = false) {
    auto encoded = asset::encodeRuntimeDefinition(definition);
    REQUIRE(encoded.hasValue());
    const auto         id = persistentId("550e8400-e29b-41d4-a716-446655440020");
    asset::EvpackBuild build;
    build.packageId = persistentId("018f6f22-2490-7ad2-bf58-4f1dbca31040");
    build.buildId   = persistentId("018f6f22-2490-7ad2-bf58-4f1dbca31041");
    build.variants  = {{"windows", "x86_64", "vulkan", {"bc"}, "spirv-1.6", "high", {}}};
    build.chunks    = {{id,
                        "eve.cloth-model",
                        SchemaVersion(1),
                        0,
                        asset::EvpackChunkKind::Definition,
                        0,
                        asset::EvpackCodec::None,
                        8,
                        {},
                        std::move(encoded).takeValue()}};
    if (addUnexpectedChunk)
        build.chunks.push_back({id,
                                "eve.cloth-model",
                                SchemaVersion(1),
                                0,
                                asset::EvpackChunkKind::Bulk,
                                1,
                                asset::EvpackCodec::None,
                                8,
                                {},
                                {1}});
    auto bytes = asset::buildEvpack(std::move(build));
    REQUIRE(bytes.hasValue());
    auto parsed = asset::parseEvpack(bytes.value());
    REQUIRE(parsed.hasValue());
    return std::make_shared<const asset::Evpack>(std::move(parsed).takeValue());
}

asset::EvpackCapabilities windowsCapabilities() {
    return {"windows", "x86_64", "vulkan", {"bc"}, {"spirv-1.6"}, {"high"}, {}};
}

}  // namespace

TEST_CASE("asset.physics.clothModelLoadsFromCanonicalEvpack") {
    auto model = physics::ClothModel::grid(3, 4, 0.5f, -1.f, 2.f, 3.f);
    REQUIRE(model.hasValue());
    asset::EvpackResourceReader           reader(makeClothPack(model.value().toValue()));
    asset_physics::EvpackClothModelLoader loader(reader);
    auto loaded = loader.load(assetRef("asset://550e8400-e29b-41d4-a716-446655440020"), windowsCapabilities());
    REQUIRE(loaded.hasValue());
    CHECK(loaded.value().model.gridCols() == 3);
    CHECK(loaded.value().model.gridRows() == 4);
    CHECK(loaded.value().model.particles().size() == 12);

    physics::Cloth3D runtime(loaded.value().model);
    CHECK(runtime.getParticleCount() == 12);
    CHECK(runtime.isPinned(0));
}

TEST_CASE("asset.physics.clothModelRejectsNonDefinitionPayloadBeforeRuntimeCreation") {
    auto model = physics::ClothModel::grid(2, 2, 0.5f, 0.f, 0.f, 0.f);
    REQUIRE(model.hasValue());
    asset::EvpackResourceReader           reader(makeClothPack(model.value().toValue(), true));
    asset_physics::EvpackClothModelLoader loader(reader);
    auto loaded = loader.load(assetRef("asset://550e8400-e29b-41d4-a716-446655440020"), windowsCapabilities());
    CHECK(!loaded.hasValue());
    REQUIRE(loaded.error() != nullptr);
    CHECK(loaded.error()->code() == DiagnosticCode::ParseError);
}
