#include "asset/AssetDiff.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve;
using namespace eve::asset;

namespace {
AssetRef ref(std::string_view value) {
    auto result = AssetRef::parse(value); REQUIRE(result.ok()); return std::move(result).takeValue();
}
EvaAssetEntry entry(AssetRef asset, std::string type, std::uint64_t version, char hashDigit) {
    return {std::move(asset), std::move(type), SchemaVersion(version), "asset.json",
            "sha256:" + std::string(64, hashDigit), {}};
}
}  // namespace

TEST_CASE("asset.diff.isIdentityBasedDeterministicAndDependencyAware") {
    const auto first = ref("asset://550e8400-e29b-41d4-a716-446655440000");
    const auto second = ref("asset://550e8400-e29b-41d4-a716-446655440001");
    const auto third = ref("asset://550e8400-e29b-41d4-a716-446655440002");
    EvaManifest before, after;
    before.assets = {entry(second, "eve.image", 1, '1'), entry(first, "eve.material", 1, '2')};
    after.assets = {entry(third, "eve.mesh", 1, '3'), entry(first, "eve.material", 2, '4')};
    before.entrypoints.emplace("default", first); after.entrypoints.emplace("default", third);
    before.dependencies.push_back({first, second, EvaDependencyKind::RuntimeRequired,
                                   "baseColor", {}, "eve.image/1"});
    auto result = diffEvaManifests(before, after);
    REQUIRE(result.ok());
    REQUIRE_EQ(result.value().assets.size(), std::size_t(3));
    CHECK_EQ(result.value().assets[0].assetId, first.id());
    CHECK_EQ(static_cast<int>(result.value().assets[0].kind), static_cast<int>(EvaAssetChangeKind::Changed));
    CHECK_EQ(static_cast<int>(result.value().assets[1].kind), static_cast<int>(EvaAssetChangeKind::Removed));
    CHECK_EQ(static_cast<int>(result.value().assets[2].kind), static_cast<int>(EvaAssetChangeKind::Added));
    CHECK_EQ(result.value().removedDependencies, std::uint32_t(1));
    CHECK(result.value().entrypointsChanged);
    CHECK(!result.value().empty());

    auto same = diffEvaManifests(before, before);
    REQUIRE(same.ok()); CHECK(same.value().empty());
}

TEST_CASE("asset.diff.detectsOptionalFallbackPolicyChanges") {
    const auto first = ref("asset://550e8400-e29b-41d4-a716-446655440000");
    const auto second = ref("asset://550e8400-e29b-41d4-a716-446655440001");
    EvaManifest before, after;
    before.assets = after.assets = {entry(first, "eve.material", 1, '1')};
    before.dependencies.push_back(
        {first, second, EvaDependencyKind::RuntimeOptional, "detail", {}, "eve.image/1",
         {{"behavior", Value("omit-feature")}, {"observableCode", Value("detail.omitted")}}});
    after = before;
    after.dependencies.front().fallback["behavior"] = Value("use-default");
    auto difference = diffEvaManifests(before, after);
    REQUIRE(difference.ok());
    CHECK_EQ(difference.value().removedDependencies, std::uint32_t(1));
    CHECK_EQ(difference.value().addedDependencies, std::uint32_t(1));
}
