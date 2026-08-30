#include "asset/AssetCooker.h"
#include "asset/RuntimeDefinition.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve;
using namespace eve::asset;

namespace {

EvaArchive sourceArchive() {
    constexpr std::string_view json = R"json({
      "schema":"eve.asset-archive","schemaVersion":1,
      "packageId":"018f6f22-2490-7ad2-bf58-4f1dbca31040",
      "packageName":"cook.test","packageVersion":"1.0.0","unknownFields":"preserve",
      "assets":[{
        "asset":"asset://550e8400-e29b-41d4-a716-446655440000",
        "type":"eve.mesh","schemaVersion":1,
        "definition":"assets/550e8400-e29b-41d4-a716-446655440000/asset.json",
        "contentHash":"sha256:19e0e9f91631c94f804a0523113a21b29900d08e6e67ff3f41dc47214b5b4e4c",
        "tags":[]
      }],
      "dependencies":[],"entrypoints":{"default":"asset://550e8400-e29b-41d4-a716-446655440000"},
      "provenance":{"license":{"redistribution":"unknown"}}
    })json";
    auto manifest = parseEvaManifest(json);
    REQUIRE(manifest.ok());
    std::vector<EvaArchiveEntry> entries = {
        {"assets/550e8400-e29b-41d4-a716-446655440000/asset.json",
         {'{','"','s','c','h','e','m','a','"',':','"','e','v','e','.','m','e','s','h','"',',',
          '"','s','c','h','e','m','a','V','e','r','s','i','o','n','"',':','1','}'}},
        {"assets/550e8400-e29b-41d4-a716-446655440000/stream/lod0.bin", {1, 2, 3}}
    };
    auto bytes = buildEvaArchive(manifest.value(), std::move(entries));
    REQUIRE(bytes.ok());
    auto archive = parseEvaArchive(bytes.value());
    REQUIRE(archive.ok());
    return std::move(archive).takeValue();
}

AssetCookProfile profile(std::string quality = "high") {
    return {{"windows", "x86_64", "vulkan", {"bc"}, "spirv-1.6", std::move(quality), {}},
            CookPublication::LocalInspection, 16};
}

}  // namespace

TEST_CASE("asset.cook.evaToEvpackIsDeterministicAndPreservesStreamChunks") {
    auto first = cookEvaToEvpack(sourceArchive(), profile());
    auto second = cookEvaToEvpack(sourceArchive(), profile());
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    CHECK_EQ(first.value().buildId, second.value().buildId);
    CHECK_EQ(first.value().bytes, second.value().bytes);
    CHECK_EQ(first.value().chunkCount, std::uint32_t(2));

    auto parsed = parseEvpack(first.value().bytes);
    REQUIRE(parsed.ok());
    REQUIRE_EQ(parsed.value().chunks().size(), std::size_t(2));
    CHECK_EQ(static_cast<std::uint16_t>(parsed.value().chunks()[0].kind),
             static_cast<std::uint16_t>(EvpackChunkKind::Definition));
    CHECK_EQ(static_cast<std::uint16_t>(parsed.value().chunks()[1].kind),
             static_cast<std::uint16_t>(EvpackChunkKind::Stream));
    CHECK_EQ(parsed.value().chunks()[1].chunkId, std::uint32_t(1));
    auto definition = parsed.value().decodeChunk(0, 4096);
    REQUIRE(definition.ok());
    auto metadata = decodeRuntimeDefinition(definition.value());
    REQUIRE(metadata.ok());
    CHECK(metadata.value().isObject());
}

TEST_CASE("asset.cook.capabilityProfileParticipatesInBuildIdentity") {
    auto high = cookEvaToEvpack(sourceArchive(), profile("high"));
    auto medium = cookEvaToEvpack(sourceArchive(), profile("medium"));
    REQUIRE(high.ok());
    REQUIRE(medium.ok());
    CHECK(high.value().buildId != medium.value().buildId);
}

TEST_CASE("asset.cook.stableTargetsCoverAllSixRuntimeOperatingSystems") {
    const std::vector<std::pair<std::string, std::string>> targets = {
        {"windows-x86_64-vulkan", "windows"}, {"linux-x86_64-vulkan", "linux"},
        {"macos-arm64-vulkan", "macos"},       {"android-arm64-vulkan", "android"},
        {"ios-arm64-vulkan", "ios"},           {"web-wasm32-webgpu", "web"}};
    for (const auto& [name, os] : targets) {
        auto resolved = assetCookProfileForTarget(name);
        REQUIRE(resolved.ok());
        CHECK_EQ(resolved.value().variant.os, os);
        auto cooked = cookEvaToEvpack(sourceArchive(), resolved.value());
        REQUIRE(cooked.ok());
        auto pack = parseEvpack(cooked.value().bytes);
        REQUIRE(pack.ok());
        const auto& variant = resolved.value().variant;
        auto selected = selectEvpackVariant(
            pack.value(),
            {variant.os, variant.arch, variant.graphics, variant.textureFamilies,
             {variant.shaderFormat}, {variant.quality}, variant.features});
        REQUIRE(selected.ok());
        CHECK_EQ(selected.value().index, std::uint32_t(0));
    }
    auto unknown = assetCookProfileForTarget("amiga-m68k-vulkan");
    REQUIRE(!unknown.ok());
    CHECK_EQ(unknown.error()->code(), DiagnosticCode::Unsupported);
}

TEST_CASE("asset.cook.publicationRejectsUnknownLicenseWithoutMutatingSource") {
    EvaArchive source = sourceArchive();
    auto publicProfile = profile();
    publicProfile.publication = CookPublication::PublicDistribution;
    auto rejected = cookEvaToEvpack(source, publicProfile);
    REQUIRE(!rejected.ok());
    CHECK_EQ(rejected.error()->code(), DiagnosticCode::PreconditionViolation);
    CHECK_EQ(source.manifest.packageName, std::string("cook.test"));
}

TEST_CASE("asset.cook.optionalFallbackPolicyRemainsObservableAtRuntime") {
    EvaArchive source = sourceArchive();
    auto external = AssetRef::parse("asset://550e8400-e29b-41d4-a716-446655440099");
    REQUIRE(external.ok());
    source.manifest.dependencies.push_back(
        {source.manifest.assets.front().asset, std::move(external).takeValue(),
         EvaDependencyKind::RuntimeOptional, "detail", {}, "eve.image/2",
         {{"behavior", Value("use-default")},
          {"observableCode", Value("mesh.detail-defaulted")}}});
    auto cooked = cookEvaToEvpack(source, profile());
    REQUIRE(cooked.ok());
    auto pack = parseEvpack(cooked.value().bytes);
    REQUIRE(pack.ok());
    auto definition = pack.value().decodeChunk(0, 4096);
    REQUIRE(definition.ok());
    auto metadata = decodeRuntimeDefinition(definition.value());
    REQUIRE(metadata.ok());
    const auto* root = metadata.value().getIf<Value::Object>();
    REQUIRE(root != nullptr);
    const auto found = root->find("dependencyFallbacks");
    REQUIRE(found != root->end());
    const auto* policies = found->second.getIf<Value::Array>();
    REQUIRE(policies != nullptr);
    REQUIRE_EQ(policies->size(), std::size_t(1));
    const auto* policy = policies->front().getIf<Value::Object>();
    REQUIRE(policy != nullptr);
    CHECK_EQ(policy->at("path").asString(), std::string("detail"));
    CHECK_EQ(policy->at("to").asString(),
             std::string("asset://550e8400-e29b-41d4-a716-446655440099"));
}
