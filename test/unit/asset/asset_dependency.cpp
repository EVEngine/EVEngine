#include "asset/AssetDependency.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve;
using namespace eve::asset;

namespace {
AssetRef ref(std::string_view value) {
    auto parsed = AssetRef::parse(value);
    REQUIRE(parsed.ok());
    return std::move(parsed).takeValue();
}
EvaAssetEntry entry(AssetRef asset, std::string type) {
    return {std::move(asset), std::move(type), SchemaVersion(1), "definition.json",
            "sha256:0000000000000000000000000000000000000000000000000000000000000000", {}};
}
}  // namespace

TEST_CASE("asset.dependency.requiredOptionalAndProviderPresenceAreExplicit") {
    const auto root = ref("asset://550e8400-e29b-41d4-a716-446655440000");
    const auto required = ref("asset://550e8400-e29b-41d4-a716-446655440001");
    const auto optional = ref("asset://550e8400-e29b-41d4-a716-446655440002");
    EvaManifest manifest;
    manifest.assets.push_back(entry(root, "eve.material"));
    manifest.entrypoints.emplace("default", root);
    manifest.dependencies.push_back({root, required, EvaDependencyKind::RuntimeRequired,
                                     "baseColor", {}, "eve.image/1"});
    manifest.dependencies.push_back({root, optional, EvaDependencyKind::RuntimeOptional,
                                     "detail", {}, "eve.image/1",
                                     {{"behavior", Value("use-default")},
                                      {"observableCode", Value("material.detail-defaulted")}}});

    auto absent = validateEvaDependencies(manifest);
    REQUIRE(!absent.ok());
    CHECK_EQ(absent.error()->code(), DiagnosticCode::NotFound);

    const std::vector<AvailableAssetDependency> available = {
        {required, "eve.image", SchemaVersion(1)}};
    auto present = validateEvaDependencies(manifest, available);
    REQUIRE(present.ok());
    REQUIRE_EQ(present.value().omittedOptionalAssets.size(), std::size_t(1));
    CHECK_EQ(present.value().omittedOptionalAssets.front(), optional.id());

    const std::vector<AvailableAssetDependency> wrong = {
        {required, "eve.mesh", SchemaVersion(1)}};
    auto wrongType = validateEvaDependencies(manifest, wrong);
    REQUIRE(!wrongType.ok());
    CHECK_EQ(wrongType.error()->code(), DiagnosticCode::TypeMismatch);
}

TEST_CASE("asset.dependency.requiredCycleAndForeignSourceAreRejected") {
    const auto first = ref("asset://550e8400-e29b-41d4-a716-446655440000");
    const auto second = ref("asset://550e8400-e29b-41d4-a716-446655440001");
    EvaManifest manifest;
    manifest.assets = {entry(first, "eve.material"), entry(second, "eve.image")};
    manifest.entrypoints.emplace("default", first);
    manifest.dependencies.push_back({first, second, EvaDependencyKind::RuntimeRequired,
                                     "image", {}, "eve.image/1"});
    manifest.dependencies.push_back({second, first, EvaDependencyKind::RuntimeRequired,
                                     "owner", {}, "eve.material/1"});
    auto cycle = validateEvaDependencies(manifest);
    REQUIRE(!cycle.ok());
    CHECK_EQ(cycle.error()->code(), DiagnosticCode::Conflict);

    manifest.dependencies.clear();
    const auto foreign = ref("asset://550e8400-e29b-41d4-a716-446655440009");
    manifest.dependencies.push_back(
        {foreign, first, EvaDependencyKind::RuntimeOptional, "bad", {}, {},
         {{"behavior", Value("omit-feature")},
          {"observableCode", Value("test.foreign-missing")}}});
    auto badSource = validateEvaDependencies(manifest);
    REQUIRE(!badSource.ok());
    CHECK_EQ(badSource.error()->code(), DiagnosticCode::NotFound);
}

TEST_CASE("asset.dependency.optionalRequiresExplicitObservableFallback") {
    const auto root = ref("asset://550e8400-e29b-41d4-a716-446655440000");
    const auto optional = ref("asset://550e8400-e29b-41d4-a716-446655440002");
    EvaManifest manifest;
    manifest.assets.push_back(entry(root, "eve.material"));
    manifest.dependencies.push_back({root, optional, EvaDependencyKind::RuntimeOptional,
                                     "detail", {}, "eve.image/1"});
    auto missingPolicy = validateEvaDependencies(manifest);
    REQUIRE(!missingPolicy.ok());
    CHECK_EQ(missingPolicy.error()->code(), DiagnosticCode::InvalidArgument);
}
