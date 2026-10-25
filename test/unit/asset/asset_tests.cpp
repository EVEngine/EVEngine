// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "asset_dependency.cpp",
    "asset_diff.cpp",
    "asset_eva_archive.cpp",
    "asset_evpack.cpp",
    "asset_runtime_definition.cpp"
  ]
}
*/
// clang-format on

#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_asset_dependency)
#include "PrivateTestScope.h"
#include "asset/AssetDependency.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// asset_dependency.cpp
namespace test_asset_dependency {
using namespace eve;
using namespace eve::asset;

namespace {
AssetRef ref(std::string_view value) {
    auto parsed = AssetRef::parse(value);
    REQUIRE(parsed.ok());
    return std::move(parsed).takeValue();
}
EvaAssetEntry entry(AssetRef asset, std::string type) {
    return {std::move(asset),
            std::move(type),
            SchemaVersion(1),
            "definition.json",
            "sha256:0000000000000000000000000000000000000000000000000000000000000000",
            {}};
}
}  // namespace

TEST_CASE("asset.dependency.requiredOptionalAndProviderPresenceAreExplicit") {
    const auto  root     = ref("asset://550e8400-e29b-41d4-a716-446655440000");
    const auto  required = ref("asset://550e8400-e29b-41d4-a716-446655440001");
    const auto  optional = ref("asset://550e8400-e29b-41d4-a716-446655440002");
    EvaManifest manifest;
    manifest.assets.push_back(entry(root, "eve.material"));
    manifest.entrypoints.emplace("default", root);
    manifest.dependencies.push_back(
        {root, required, EvaDependencyKind::RuntimeRequired, "baseColor", {}, "eve.image/1"});
    manifest.dependencies.push_back(
        {root,
         optional,
         EvaDependencyKind::RuntimeOptional,
         "detail",
         {},
         "eve.image/1",
         {{"behavior", Value("use-default")}, {"observableCode", Value("material.detail-defaulted")}}});

    auto absent = validateEvaDependencies(manifest);
    REQUIRE(!absent.ok());
    CHECK_EQ(absent.error()->code(), DiagnosticCode::NotFound);

    const std::vector<AvailableAssetDependency> available = {{required, "eve.image", SchemaVersion(1)}};
    auto                                        present   = validateEvaDependencies(manifest, available);
    REQUIRE(present.ok());
    REQUIRE_EQ(present.value().omittedOptionalAssets.size(), std::size_t(1));
    CHECK_EQ(present.value().omittedOptionalAssets.front(), optional.id());

    const std::vector<AvailableAssetDependency> wrong     = {{required, "eve.mesh", SchemaVersion(1)}};
    auto                                        wrongType = validateEvaDependencies(manifest, wrong);
    REQUIRE(!wrongType.ok());
    CHECK_EQ(wrongType.error()->code(), DiagnosticCode::TypeMismatch);
}

TEST_CASE("asset.dependency.requiredCycleAndForeignSourceAreRejected") {
    const auto  first  = ref("asset://550e8400-e29b-41d4-a716-446655440000");
    const auto  second = ref("asset://550e8400-e29b-41d4-a716-446655440001");
    EvaManifest manifest;
    manifest.assets = {entry(first, "eve.material"), entry(second, "eve.image")};
    manifest.entrypoints.emplace("default", first);
    manifest.dependencies.push_back({first, second, EvaDependencyKind::RuntimeRequired, "image", {}, "eve.image/1"});
    manifest.dependencies.push_back({second, first, EvaDependencyKind::RuntimeRequired, "owner", {}, "eve.material/1"});
    auto cycle = validateEvaDependencies(manifest);
    REQUIRE(!cycle.ok());
    CHECK_EQ(cycle.error()->code(), DiagnosticCode::Conflict);

    manifest.dependencies.clear();
    const auto foreign = ref("asset://550e8400-e29b-41d4-a716-446655440009");
    manifest.dependencies.push_back(
        {foreign,
         first,
         EvaDependencyKind::RuntimeOptional,
         "bad",
         {},
         {},
         {{"behavior", Value("omit-feature")}, {"observableCode", Value("test.foreign-missing")}}});
    auto badSource = validateEvaDependencies(manifest);
    REQUIRE(!badSource.ok());
    CHECK_EQ(badSource.error()->code(), DiagnosticCode::NotFound);
}

TEST_CASE("asset.dependency.optionalRequiresExplicitObservableFallback") {
    const auto  root     = ref("asset://550e8400-e29b-41d4-a716-446655440000");
    const auto  optional = ref("asset://550e8400-e29b-41d4-a716-446655440002");
    EvaManifest manifest;
    manifest.assets.push_back(entry(root, "eve.material"));
    manifest.dependencies.push_back({root, optional, EvaDependencyKind::RuntimeOptional, "detail", {}, "eve.image/1"});
    auto missingPolicy = validateEvaDependencies(manifest);
    REQUIRE(!missingPolicy.ok());
    CHECK_EQ(missingPolicy.error()->code(), DiagnosticCode::InvalidArgument);
}
}  // namespace test_asset_dependency
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_asset_diff)
#include "PrivateTestScope.h"
#include "asset/AssetDiff.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// asset_diff.cpp
namespace test_asset_diff {
using namespace eve;
using namespace eve::asset;

namespace {
AssetRef ref(std::string_view value) {
    auto result = AssetRef::parse(value);
    REQUIRE(result.ok());
    return std::move(result).takeValue();
}
EvaAssetEntry entry(AssetRef asset, std::string type, std::uint64_t version, char hashDigit) {
    return {std::move(asset),
            std::move(type),
            SchemaVersion(version),
            "asset.json",
            "sha256:" + std::string(64, hashDigit),
            {}};
}
}  // namespace

TEST_CASE("asset.diff.isIdentityBasedDeterministicAndDependencyAware") {
    const auto  first  = ref("asset://550e8400-e29b-41d4-a716-446655440000");
    const auto  second = ref("asset://550e8400-e29b-41d4-a716-446655440001");
    const auto  third  = ref("asset://550e8400-e29b-41d4-a716-446655440002");
    EvaManifest before, after;
    before.assets = {entry(second, "eve.image", 1, '1'), entry(first, "eve.material", 1, '2')};
    after.assets  = {entry(third, "eve.mesh", 1, '3'), entry(first, "eve.material", 2, '4')};
    before.entrypoints.emplace("default", first);
    after.entrypoints.emplace("default", third);
    before.dependencies.push_back({first, second, EvaDependencyKind::RuntimeRequired, "baseColor", {}, "eve.image/1"});
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
    REQUIRE(same.ok());
    CHECK(same.value().empty());
}

TEST_CASE("asset.diff.detectsOptionalFallbackPolicyChanges") {
    const auto  first  = ref("asset://550e8400-e29b-41d4-a716-446655440000");
    const auto  second = ref("asset://550e8400-e29b-41d4-a716-446655440001");
    EvaManifest before, after;
    before.assets = after.assets = {entry(first, "eve.material", 1, '1')};
    before.dependencies.push_back({first,
                                   second,
                                   EvaDependencyKind::RuntimeOptional,
                                   "detail",
                                   {},
                                   "eve.image/1",
                                   {{"behavior", Value("omit-feature")}, {"observableCode", Value("detail.omitted")}}});
    after                                           = before;
    after.dependencies.front().fallback["behavior"] = Value("use-default");
    auto difference                                 = diffEvaManifests(before, after);
    REQUIRE(difference.ok());
    CHECK_EQ(difference.value().removedDependencies, std::uint32_t(1));
    CHECK_EQ(difference.value().addedDependencies, std::uint32_t(1));
}
}  // namespace test_asset_diff
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_asset_eva_archive)
#include "PrivateTestScope.h"
#include <algorithm>
#include "asset/EvaArchive.h"
#include "asset/EvaManifest.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// asset_eva_archive.cpp
namespace test_asset_eva_archive {
// asset_eva_archive.cpp
using namespace eve;
using namespace eve::asset;

namespace {

EvaManifest manifest() {
    constexpr std::string_view json   = R"json({
      "schema":"eve.asset-archive","schemaVersion":1,
      "packageId":"018f6f22-2490-7ad2-bf58-4f1dbca31040",
      "packageName":"test.archive","packageVersion":"1.0.0","unknownFields":"preserve",
      "assets":[],"dependencies":[],"entrypoints":{},"provenance":{}
    })json";
    auto                       parsed = parseEvaManifest(json);
    REQUIRE(parsed.ok());
    return std::move(parsed).takeValue();
}

}  // namespace

TEST_CASE("asset.eva.archive.isDeterministicAndRoundTrips") {
    std::vector<EvaArchiveEntry> entries = {{"sources/terrain.raw", {4, 5, 6}}, {"assets/definition.json", {'{', '}'}}};
    std::vector<EvaArchiveEntry> reversed(entries.rbegin(), entries.rend());
    auto                         first  = buildEvaArchive(manifest(), entries);
    auto                         second = buildEvaArchive(manifest(), std::move(reversed));
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    CHECK_EQ(first.value(), second.value());

    auto parsed = parseEvaArchive(first.value());
    REQUIRE(parsed.ok());
    CHECK_EQ(parsed.value().manifest.packageName, std::string("test.archive"));
    REQUIRE_EQ(parsed.value().entries.size(), std::size_t(2));
    CHECK_EQ(parsed.value().entries.front().path, std::string("assets/definition.json"));
}

TEST_CASE("asset.eva.archive.deterministicallyDeflatesCompressibleEntries") {
    std::vector<std::uint8_t> repeated(4096, 0x5a);
    auto                      first  = buildEvaArchive(manifest(), {{"payload.bin", repeated}});
    auto                      second = buildEvaArchive(manifest(), {{"payload.bin", repeated}});
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    CHECK_EQ(first.value(), second.value());
    CHECK(first.value().size() < repeated.size());
    auto parsed = parseEvaArchive(first.value());
    REQUIRE(parsed.ok());
    REQUIRE_EQ(parsed.value().entries.size(), std::size_t(1));
    CHECK_EQ(parsed.value().entries.front().bytes, repeated);
}

TEST_CASE("asset.eva.archive.ignoresBoundedZipCommentWithoutChangingContent") {
    auto built = buildEvaArchive(manifest(), {{"notes.txt", {'o', 'k'}}});
    REQUIRE(built.ok());
    auto commented = built.value();
    REQUIRE(commented.size() >= 22);
    commented[commented.size() - 2] = 3;
    commented[commented.size() - 1] = 0;
    commented.insert(commented.end(), {'e', 'v', 'e'});
    auto parsed = parseEvaArchive(commented);
    REQUIRE(parsed.ok());
    REQUIRE_EQ(parsed.value().entries.size(), std::size_t(1));
    CHECK_EQ(parsed.value().entries.front().bytes, std::vector<std::uint8_t>({'o', 'k'}));
}

TEST_CASE("asset.eva.archive.rejectsTraversalAndDuplicatePaths") {
    auto traversal = buildEvaArchive(manifest(), {{"../secret", {1}}});
    REQUIRE(!traversal.ok());
    CHECK_EQ(traversal.error()->code(), DiagnosticCode::InvalidArgument);

    auto duplicate = buildEvaArchive(manifest(), {{"same.bin", {1}}, {"same.bin", {2}}});
    REQUIRE(!duplicate.ok());
    CHECK_EQ(duplicate.error()->code(), DiagnosticCode::Conflict);

    auto reserved = buildEvaArchive(manifest(), {{"manifest.json", {1}}});
    REQUIRE(!reserved.ok());
    CHECK_EQ(reserved.error()->code(), DiagnosticCode::Conflict);
}

TEST_CASE("asset.eva.archive.acceptsNfcAndRejectsCanonicallyEquivalentDecomposedPaths") {
    const std::string composed   = "sources/caf\xc3\xa9.txt";
    const std::string decomposed = "sources/cafe\xcc\x81.txt";
    auto              accepted   = buildEvaArchive(manifest(), {{composed, {1}}});
    REQUIRE(accepted.ok());
    auto parsed = parseEvaArchive(accepted.value());
    REQUIRE(parsed.ok());
    REQUIRE_EQ(parsed.value().entries.size(), std::size_t(1));
    CHECK_EQ(parsed.value().entries.front().path, composed);

    auto rejected = buildEvaArchive(manifest(), {{decomposed, {1}}});
    REQUIRE(!rejected.ok());
    CHECK_EQ(rejected.error()->code(), DiagnosticCode::InvalidArgument);

    auto unicodeCollision =
        buildEvaArchive(manifest(), {{"sources/\xc3\x89tage.txt", {1}}, {"sources/\xc3\xa9tage.txt", {2}}});
    REQUIRE(!unicodeCollision.ok());
    CHECK_EQ(unicodeCollision.error()->code(), DiagnosticCode::Conflict);
}

TEST_CASE("asset.eva.archive.rejectsCorruptionBeforeReturningPayloads") {
    const std::vector<std::uint8_t> marker = {0xde, 0xad, 0xbe, 0xef};
    auto                            built  = buildEvaArchive(manifest(), {{"payload.bin", marker}});
    REQUIRE(built.ok());
    auto bytes = std::move(built).takeValue();
    auto found = std::search(bytes.begin(), bytes.end(), marker.begin(), marker.end());
    REQUIRE(found != bytes.end());
    *found ^= 0xff;

    auto parsed = parseEvaArchive(bytes);
    REQUIRE(!parsed.ok());
    CHECK_EQ(parsed.error()->code(), DiagnosticCode::HashMismatch);
    CHECK_EQ(parsed.error()->path(), std::string("payload.bin"));
}

TEST_CASE("asset.eva.archive.enforcesDecodedBudgetsBeforeAllocation") {
    EvaArchiveLimits writeLimits;
    writeLimits.maximumEntryBytes = 3;
    auto tooLarge                 = buildEvaArchive(manifest(), {{"payload.bin", {1, 2, 3, 4}}}, writeLimits);
    REQUIRE(!tooLarge.ok());
    CHECK_EQ(tooLarge.error()->code(), DiagnosticCode::InvalidArgument);

    auto built = buildEvaArchive(manifest(), {{"payload.bin", {1, 2, 3, 4}}});
    REQUIRE(built.ok());
    EvaArchiveLimits readLimits;
    readLimits.maximumEntryBytes = 3;
    auto rejected                = parseEvaArchive(built.value(), readLimits);
    REQUIRE(!rejected.ok());
}

TEST_CASE("asset.eva.archive.enforcesContentAddressedBlobIdentity") {
    const std::string emptyDigest = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    auto              accepted    = buildEvaArchive(manifest(), {{"blobs/sha256/" + emptyDigest, {}}});
    REQUIRE(accepted.ok());
    auto parsed = parseEvaArchive(accepted.value());
    REQUIRE(parsed.ok());

    auto mismatch = buildEvaArchive(manifest(), {{"blobs/sha256/" + emptyDigest, {'x'}}});
    REQUIRE(!mismatch.ok());
    CHECK_EQ(mismatch.error()->code(), DiagnosticCode::HashMismatch);
}

TEST_CASE("asset.eva.archive.definitionSchemaMustMatchManifestIdentity") {
    EvaManifest declared = manifest();
    auto        asset    = AssetRef::parse("asset://550e8400-e29b-41d4-a716-446655440000");
    REQUIRE(asset.ok());
    const std::string definition = "{\"schema\":\"eve.image\",\"schemaVersion\":1}";
    declared.assets.push_back({std::move(asset).takeValue(),
                               "eve.mesh",
                               SchemaVersion(1),
                               "assets/mismatch.json",
                               "sha256:e6be396209d71db3430110f80e1b4cbb27ed10f94799af78a1909bfc22e83c70",
                               {}});
    auto rejected = buildEvaArchive(declared, {{"assets/mismatch.json", {definition.begin(), definition.end()}}});
    REQUIRE(!rejected.ok());
    CHECK_EQ(rejected.error()->code(), DiagnosticCode::TypeMismatch);
}

// asset_eva_manifest.cpp
using namespace eve;
using namespace eve::asset;

namespace {

constexpr const char* kManifest = R"json({
  "schema":"eve.asset-archive",
  "schemaVersion":1,
  "packageId":"018f6f22-2490-7ad2-bf58-4f1dbca31040",
  "packageName":"test.terrain",
  "packageVersion":"1.0.0",
  "unknownFields":"preserve",
  "assets":[{
    "asset":"asset://550e8400-e29b-41d4-a716-446655440000",
    "type":"eve.texture",
    "schemaVersion":2,
    "definition":"assets/550e8400-e29b-41d4-a716-446655440000/asset.json",
    "contentHash":"sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
    "tags":["terrain"]
  }],
  "dependencies":[],
  "entrypoints":{"default":"asset://550e8400-e29b-41d4-a716-446655440000"},
  "provenance":{"provider":"local"},
  "vendorExtension":{"kept":true}
})json";

}  // namespace

TEST_CASE("asset.eva.manifest.roundTripPreservesTypedAndUnknownFields") {
    auto parsed = parseEvaManifest(kManifest);
    REQUIRE(parsed.ok());
    EvaManifest manifest = std::move(parsed).takeValue();
    CHECK_EQ(manifest.packageName, std::string("test.terrain"));
    REQUIRE_EQ(manifest.assets.size(), std::size_t(1));
    CHECK_EQ(manifest.assets.front().type, std::string("eve.texture"));
    CHECK_EQ(manifest.assets.front().schemaVersion.value(), std::uint64_t(2));
    CHECK(manifest.extensions.contains("vendorExtension"));

    auto encoded = serializeEvaManifest(manifest);
    REQUIRE(encoded.ok());
    auto reparsed = parseEvaManifest(std::move(encoded).takeValue());
    REQUIRE(reparsed.ok());
    CHECK(reparsed.value().extensions.contains("vendorExtension"));
    CHECK_EQ(reparsed.value().entrypoints.at("default"), manifest.assets.front().asset);
}

TEST_CASE("asset.eva.manifest.rejectsUnknownVersion") {
    std::string       json = kManifest;
    const std::string from = "\"schemaVersion\":1";
    json.replace(json.find(from), from.size(), "\"schemaVersion\":2");
    auto parsed = parseEvaManifest(json);
    REQUIRE(!parsed.ok());
    CHECK_EQ(parsed.error()->code(), DiagnosticCode::UnknownVersion);
    CHECK_EQ(parsed.error()->path(), std::string("$.schemaVersion"));
}

TEST_CASE("asset.eva.manifest.rejectsDuplicateAssetIdentity") {
    std::string       json      = kManifest;
    const std::string needle    = "}],\n  \"dependencies\"";
    const std::string duplicate = R"json(},{
    "asset":"asset://550e8400-e29b-41d4-a716-446655440000",
    "type":"eve.image",
    "schemaVersion":1,
    "definition":"duplicate.json",
    "contentHash":"sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
  }],
  "dependencies")json";
    json.replace(json.find(needle), needle.size(), duplicate);
    auto parsed = parseEvaManifest(json);
    REQUIRE(!parsed.ok());
    CHECK_EQ(parsed.error()->code(), DiagnosticCode::Conflict);
}

TEST_CASE("asset.eva.manifest.platformDependencyRequiresPredicate") {
    std::string       json = kManifest;
    const std::string from = "\"dependencies\":[]";
    const std::string to   = R"json("dependencies":[{
    "from":"asset://550e8400-e29b-41d4-a716-446655440000",
    "to":"asset://550e8400-e29b-41d4-a716-446655440000",
    "kind":"platform"
  }])json";
    json.replace(json.find(from), from.size(), to);
    auto parsed = parseEvaManifest(json);
    REQUIRE(!parsed.ok());
    CHECK_EQ(parsed.error()->code(), DiagnosticCode::InvalidArgument);
    CHECK_EQ(parsed.error()->path(), std::string("$.dependencies[0].predicate"));
}

TEST_CASE("asset.eva.manifest.optionalFallbackRoundTripsAndIsRequired") {
    std::string       json       = kManifest;
    const std::string from       = "\"dependencies\":[]";
    const std::string dependency = R"json("dependencies":[{
    "from":"asset://550e8400-e29b-41d4-a716-446655440000",
    "to":"asset://550e8400-e29b-41d4-a716-446655440001",
    "kind":"runtime-optional","path":"detail","expectedType":"eve.image/2",
    "fallback":{"behavior":"use-default","observableCode":"texture.detail-defaulted"}
  }])json";
    json.replace(json.find(from), from.size(), dependency);
    auto parsed = parseEvaManifest(json);
    REQUIRE(parsed.ok());
    REQUIRE_EQ(parsed.value().dependencies.size(), std::size_t(1));
    CHECK_EQ(parsed.value().dependencies.front().fallback.at("observableCode").asString(),
             std::string("texture.detail-defaulted"));
    auto encoded = serializeEvaManifest(parsed.value());
    REQUIRE(encoded.ok());
    auto roundTrip = parseEvaManifest(encoded.value());
    REQUIRE(roundTrip.ok());
    CHECK_EQ(roundTrip.value().dependencies.front().fallback, parsed.value().dependencies.front().fallback);

    std::string       missingJson       = kManifest;
    const std::string missingDependency = R"json("dependencies":[{
    "from":"asset://550e8400-e29b-41d4-a716-446655440000",
    "to":"asset://550e8400-e29b-41d4-a716-446655440001",
    "kind":"runtime-optional","path":"detail","expectedType":"eve.image/2"
  }])json";
    missingJson.replace(missingJson.find(from), from.size(), missingDependency);
    auto missing = parseEvaManifest(missingJson);
    REQUIRE(!missing.ok());
    CHECK_EQ(missing.error()->code(), DiagnosticCode::InvalidArgument);
}
}  // namespace test_asset_eva_archive
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_asset_evpack)
#include "PrivateTestScope.h"
#include <algorithm>
#include "asset/Evpack.h"
#include "asset/EvpackRange.h"
#include "asset/EvpackRegistry.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// asset_evpack.cpp
namespace test_asset_evpack {
using namespace eve;
using namespace eve::asset;

namespace {

PersistentId id(std::string_view text) {
    auto parsed = PersistentId::parse(text);
    REQUIRE(parsed.has_value());
    return *parsed;
}

EvpackVariant desktopVariant(std::string quality) {
    return {"windows", "x86_64", "vulkan", {"bc", "rgba8"}, "spirv-1.6", std::move(quality), {}};
}

EvpackBuild buildInput() {
    EvpackBuild build;
    build.packageId = id("018f6f22-2490-7ad2-bf58-4f1dbca31040");
    build.buildId   = id("018f6f22-2490-7ad2-bf58-4f1dbca31041");
    build.variants  = {desktopVariant("high"), desktopVariant("medium")};
    build.chunks    = {{id("550e8400-e29b-41d4-a716-446655440001"),
                        "eve.mesh",
                        SchemaVersion(2),
                        0,
                        EvpackChunkKind::Bulk,
                        0,
                        EvpackCodec::None,
                        16,
                        {},
                        {1, 2, 3, 4}},
                       {id("550e8400-e29b-41d4-a716-446655440000"),
                        "eve.material",
                        SchemaVersion(1),
                        1,
                        EvpackChunkKind::Definition,
                        0,
                        EvpackCodec::None,
                        8,
                        {id("550e8400-e29b-41d4-a716-446655440001")},
                        {'m', 'a', 't'}}};
    return build;
}

class CountingRangeSource final : public EvpackRangeSource {
public:
    explicit CountingRangeSource(std::vector<std::uint8_t> value) : bytes(std::move(value)) {}
    Result<std::uint64_t>             size() const override { return Result<std::uint64_t>::success(bytes.size()); }
    Result<std::vector<std::uint8_t>> read(std::uint64_t offset, std::uint64_t count) const override {
        ++reads;
        ranges.emplace_back(offset, count);
        if (offset > bytes.size() || count > bytes.size() - offset)
            return Result<std::vector<std::uint8_t>>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "test range outside source"));
        return Result<std::vector<std::uint8_t>>::success({bytes.begin() + static_cast<std::size_t>(offset),
                                                           bytes.begin() + static_cast<std::size_t>(offset + count)});
    }
    mutable std::size_t                                          reads = 0;
    mutable std::vector<std::pair<std::uint64_t, std::uint64_t>> ranges;
    mutable std::vector<std::uint8_t>                            bytes;
};

class TestSigner final : public EvpackSigner {
public:
    Result<EvpackSignature> sign(std::span<const std::uint8_t, 32> digest) const override {
        EvpackSignature signature;
        signature.algorithm = "test-digest";
        signature.keyId.fill(0x2a);
        for (std::size_t index = 0; index < 64; ++index) signature.bytes[index] = digest[index % 32] ^ 0x5a;
        return Result<EvpackSignature>::success(std::move(signature));
    }
};

class TestVerifier final : public EvpackSignatureVerifier {
public:
    Result<void> verify(std::span<const std::uint8_t, 32> digest, const EvpackSignature& signature) const override {
        if (signature.algorithm != "test-digest" || !std::all_of(signature.keyId.begin(), signature.keyId.end(),
                                                                 [](std::uint8_t byte) { return byte == 0x2a; }))
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::PreconditionViolation, "untrusted test key"));
        for (std::size_t index = 0; index < 64; ++index)
            if (signature.bytes[index] != (digest[index % 32] ^ 0x5a))
                return Result<void>::failure(
                    Diagnostic::error(DiagnosticCode::HashMismatch, "test signature mismatch"));
        return Result<void>::success();
    }
};

}  // namespace

TEST_CASE("asset.evpack.deterministicBuildAndRandomAccessRoundTrip") {
    auto firstInput  = buildInput();
    auto secondInput = buildInput();
    std::reverse(secondInput.chunks.begin(), secondInput.chunks.end());
    auto first  = buildEvpack(std::move(firstInput));
    auto second = buildEvpack(std::move(secondInput));
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    CHECK_EQ(first.value(), second.value());

    auto parsed = parseEvpack(first.value());
    REQUIRE(parsed.ok());
    CHECK_EQ(parsed.value().packageId(), id("018f6f22-2490-7ad2-bf58-4f1dbca31040"));
    REQUIRE_EQ(parsed.value().chunks().size(), std::size_t(2));
    CHECK_EQ(parsed.value().chunks().front().type, std::string("eve.material"));
    auto bytes = parsed.value().chunkBytes(0);
    REQUIRE(bytes.ok());
    CHECK_EQ(std::vector<std::uint8_t>(bytes.value().begin(), bytes.value().end()),
             std::vector<std::uint8_t>({'m', 'a', 't'}));
}

TEST_CASE("asset.evpack.rejectsHeaderAndChunkHashCorruption") {
    auto built = buildEvpack(buildInput());
    REQUIRE(built.ok());
    auto headerCorrupt = built.value();
    headerCorrupt[20] ^= 0x80;
    auto badHeader = parseEvpack(headerCorrupt);
    REQUIRE(!badHeader.ok());
    CHECK_EQ(badHeader.error()->code(), DiagnosticCode::HashMismatch);

    auto chunkCorrupt = std::move(built).takeValue();
    auto marker       = std::find(chunkCorrupt.begin(), chunkCorrupt.end(), std::uint8_t('m'));
    REQUIRE(marker != chunkCorrupt.end());
    *marker ^= 1;
    auto badChunk = parseEvpack(chunkCorrupt);
    REQUIRE(!badChunk.ok());
}

TEST_CASE("asset.evpack.variantSelectionIsCapabilityBasedAndObservable") {
    auto built = buildEvpack(buildInput());
    REQUIRE(built.ok());
    auto parsed = parseEvpack(built.value());
    REQUIRE(parsed.ok());
    EvpackCapabilities medium{"windows", "x86_64", "vulkan", {"bc"}, {"spirv-1.6"}, {"medium"}, {}};
    auto               selected = selectEvpackVariant(parsed.value(), medium);
    REQUIRE(selected.ok());
    CHECK_EQ(selected.value().index, std::uint32_t(1));
    CHECK(selected.value().usedFallback);

    medium.graphics  = "webgpu";
    auto unsupported = selectEvpackVariant(parsed.value(), medium);
    REQUIRE(!unsupported.ok());
    CHECK_EQ(unsupported.error()->code(), DiagnosticCode::Unsupported);
}

TEST_CASE("asset.evpack.rejectsDuplicateChunkKeysAndBudgets") {
    auto duplicate = buildInput();
    duplicate.chunks.push_back(duplicate.chunks.front());
    auto conflict = buildEvpack(std::move(duplicate));
    REQUIRE(!conflict.ok());
    CHECK_EQ(conflict.error()->code(), DiagnosticCode::Conflict);

    EvpackLimits limits;
    limits.maximumChunkBytes = 2;
    auto tooLarge            = buildEvpack(buildInput(), limits);
    REQUIRE(!tooLarge.ok());

    auto invalidText                     = buildInput();
    invalidText.variants.front().quality = std::string("\xc0\x80", 2);
    auto badUtf8                         = buildEvpack(std::move(invalidText));
    REQUIRE(!badUtf8.ok());
    CHECK_EQ(badUtf8.error()->code(), DiagnosticCode::InvalidArgument);
}

TEST_CASE("asset.evpack.registry.prepareFailurePreservesOldGenerationAndReplacementStalesHandles") {
    auto oldBytes = buildEvpack(buildInput());
    REQUIRE(oldBytes.ok());
    auto oldCandidate = prepareEvpackMount(oldBytes.value());
    REQUIRE(oldCandidate.ok());
    EvpackRegistry registry;
    auto           oldReceipt = registry.commit(std::move(oldCandidate).takeValue());
    REQUIRE(oldReceipt.ok());

    auto corrupt = oldBytes.value();
    corrupt[30] ^= 0x40;
    auto rejected = prepareEvpackMount(corrupt);
    REQUIRE(!rejected.ok());
    auto stillCurrent = registry.resolve(oldReceipt.value().handle);
    REQUIRE(stillCurrent.ok());

    auto replacementInput                 = buildInput();
    replacementInput.buildId              = id("018f6f22-2490-7ad2-bf58-4f1dbca31042");
    replacementInput.chunks.front().bytes = {9, 8, 7};
    auto replacementBytes                 = buildEvpack(std::move(replacementInput));
    REQUIRE(replacementBytes.ok());
    auto replacementCandidate = prepareEvpackMount(replacementBytes.value());
    REQUIRE(replacementCandidate.ok());
    auto replacementReceipt = registry.commit(std::move(replacementCandidate).takeValue());
    REQUIRE(replacementReceipt.ok());
    CHECK(replacementReceipt.value().replacedExisting);

    auto stale = registry.resolve(oldReceipt.value().handle);
    REQUIRE(!stale.ok());
    CHECK_EQ(stale.error()->code(), DiagnosticCode::StaleHandle);
    auto current = registry.resolve(replacementReceipt.value().handle);
    REQUIRE(current.ok());
}

TEST_CASE("asset.evpack.registryDispatchesOutsideLockAndAllowsSelfCancellation") {
    EvpackRegistry                       registry;
    EvpackRegistrySubscription           self;
    std::vector<EvpackRegistryEventKind> observed;
    auto subscribed = registry.subscribe([&](const EvpackRegistryEvent& event) -> Result<void> {
        observed.push_back(event.kind);
        return registry.unsubscribe(self);
    });
    REQUIRE(subscribed.ok());
    self         = subscribed.value();
    auto failing = registry.subscribe([](const EvpackRegistryEvent&) -> Result<void> {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::CallbackFailure, "injected subscriber failure"));
    });
    REQUIRE(failing.ok());

    auto firstBytes = buildEvpack(buildInput());
    REQUIRE(firstBytes.ok());
    auto firstCandidate = prepareEvpackMount(firstBytes.value());
    REQUIRE(firstCandidate.ok());
    auto first = registry.commit(std::move(firstCandidate).takeValue());
    REQUIRE(first.ok());
    REQUIRE_EQ(observed.size(), std::size_t(1));
    CHECK_EQ(static_cast<int>(observed.front()), static_cast<int>(EvpackRegistryEventKind::Mounted));
    REQUIRE_EQ(first.value().callbackDiagnostics.size(), std::size_t(1));
    CHECK_EQ(first.value().callbackDiagnostics.front().code(), DiagnosticCode::CallbackFailure);

    auto replacement      = buildInput();
    replacement.buildId   = id("018f6f22-2490-7ad2-bf58-4f1dbca31042");
    auto replacementBytes = buildEvpack(std::move(replacement));
    REQUIRE(replacementBytes.ok());
    auto replacementCandidate = prepareEvpackMount(replacementBytes.value());
    REQUIRE(replacementCandidate.ok());
    auto second = registry.commit(std::move(replacementCandidate).takeValue());
    REQUIRE(second.ok());
    CHECK_EQ(observed.size(), std::size_t(1));
    CHECK(second.value().replacedExisting);
    REQUIRE_EQ(second.value().callbackDiagnostics.size(), std::size_t(1));

    auto unmounted = registry.unmount(second.value().handle);
    REQUIRE(unmounted.ok());
    REQUIRE_EQ(unmounted.value().callbackDiagnostics.size(), std::size_t(1));
    auto cancelled = registry.unsubscribe(failing.value());
    REQUIRE(cancelled.ok());
}

TEST_CASE("asset.evpack.rangeMountReadsMetadataThenVerifiesOnlyRequestedChunk") {
    auto built = buildEvpack(buildInput());
    REQUIRE(built.ok());
    auto source = std::make_shared<CountingRangeSource>(built.value());
    auto mount  = prepareEvpackRangeMount(source);
    REQUIRE(mount.ok());
    CHECK_EQ(source->reads, std::size_t(2));
    REQUIRE_EQ(mount.value().index().chunks().size(), std::size_t(2));
    auto invalidBorrow = mount.value().index().chunkBytes(0);
    REQUIRE(!invalidBorrow.ok());
    CHECK_EQ(invalidBorrow.error()->code(), DiagnosticCode::PreconditionViolation);

    auto chunk = mount.value().readChunk(0);
    REQUIRE(chunk.ok());
    CHECK_EQ(source->reads, std::size_t(3));
    CHECK_EQ(chunk.value(), std::vector<std::uint8_t>({'m', 'a', 't'}));

    const auto& second = mount.value().index().chunks()[1];
    source->bytes[static_cast<std::size_t>(second.offset)] ^= 0xff;
    auto corrupt = mount.value().readChunk(1);
    REQUIRE(!corrupt.ok());
    CHECK_EQ(corrupt.error()->code(), DiagnosticCode::HashMismatch);
}

TEST_CASE("asset.evpack.zstdIsDeterministicBoundedAndDecodedBeforeExposure") {
    auto                      input = buildInput();
    std::vector<std::uint8_t> decoded(64 * 1024, 0x5a);
    input.chunks.front().codec = EvpackCodec::Zstd;
    input.chunks.front().bytes = decoded;
    auto first                 = buildEvpack(input);
    auto second                = buildEvpack(std::move(input));
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    CHECK_EQ(first.value(), second.value());

    auto parsed = parseEvpack(first.value());
    REQUIRE(parsed.ok());
    const auto compressed = std::find_if(parsed.value().chunks().begin(), parsed.value().chunks().end(),
                                         [](const EvpackChunk& chunk) { return chunk.codec == EvpackCodec::Zstd; });
    REQUIRE(compressed != parsed.value().chunks().end());
    CHECK(compressed->storedSize < compressed->decodedSize);
    const std::size_t index    = static_cast<std::size_t>(compressed - parsed.value().chunks().begin());
    auto              borrowed = parsed.value().chunkBytes(index);
    REQUIRE(!borrowed.ok());
    auto restored = parsed.value().decodeChunk(index, decoded.size());
    REQUIRE(restored.ok());
    CHECK_EQ(restored.value(), decoded);
    auto overBudget = parsed.value().decodeChunk(index, decoded.size() - 1);
    REQUIRE(!overBudget.ok());

    auto source = std::make_shared<CountingRangeSource>(first.value());
    auto mount  = prepareEvpackRangeMount(source);
    REQUIRE(mount.ok());
    auto ranged = mount.value().readChunk(index);
    REQUIRE(ranged.ok());
    CHECK_EQ(ranged.value(), decoded);
}

TEST_CASE("asset.evpack.registryRequiresProviderBeforeConsumerCommit") {
    const auto externalId    = id("550e8400-e29b-41d4-a716-446655440099");
    auto       consumerInput = buildInput();
    consumerInput.chunks.front().dependencies.push_back(externalId);
    auto consumerBytes = buildEvpack(std::move(consumerInput));
    REQUIRE(consumerBytes.ok());
    auto consumer = prepareEvpackMount(consumerBytes.value());
    REQUIRE(consumer.ok());
    EvpackRegistry registry;
    auto           absent = registry.commit(std::move(consumer).takeValue());
    REQUIRE(!absent.ok());
    CHECK_EQ(absent.error()->code(), DiagnosticCode::NotFound);

    EvpackBuild providerInput;
    providerInput.packageId = id("018f6f22-2490-7ad2-bf58-4f1dbca31090");
    providerInput.buildId   = id("018f6f22-2490-7ad2-bf58-4f1dbca31091");
    providerInput.variants  = {desktopVariant("high")};
    providerInput.chunks    = {{externalId,
                                "eve.image",
                                SchemaVersion(2),
                                0,
                                EvpackChunkKind::Definition,
                                0,
                                EvpackCodec::None,
                                8,
                                {},
                                {'{', '}'}}};
    auto providerBytes      = buildEvpack(std::move(providerInput));
    REQUIRE(providerBytes.ok());
    auto provider = prepareEvpackMount(providerBytes.value());
    REQUIRE(provider.ok());
    auto providerReceipt = registry.commit(std::move(provider).takeValue());
    REQUIRE(providerReceipt.ok());

    auto externalRef = AssetRef::fromId(externalId);
    REQUIRE(externalRef.ok());
    auto resolvedAsset = registry.resolveAsset(externalRef.value(), "eve.image/2",
                                               {"windows", "x86_64", "vulkan", {"bc"}, {"spirv-1.6"}, {"high"}, {}});
    REQUIRE(resolvedAsset.ok());
    auto externalPayload = registry.readAsset(resolvedAsset.value(), 1024);
    REQUIRE(externalPayload.ok());
    CHECK_EQ(externalPayload.value().asset, externalRef.value());

    auto retry = prepareEvpackMount(consumerBytes.value());
    REQUIRE(retry.ok());
    auto mounted = registry.commit(std::move(retry).takeValue());
    REQUIRE(mounted.ok());

    EvpackBuild incompatible;
    incompatible.packageId = id("018f6f22-2490-7ad2-bf58-4f1dbca31090");
    incompatible.buildId   = id("018f6f22-2490-7ad2-bf58-4f1dbca31092");
    incompatible.variants  = {desktopVariant("high")};
    incompatible.chunks    = {{id("550e8400-e29b-41d4-a716-446655440098"),
                               "eve.image",
                               SchemaVersion(2),
                               0,
                               EvpackChunkKind::Definition,
                               0,
                               EvpackCodec::None,
                               8,
                               {},
                               {'{', '}'}}};
    auto incompatibleBytes = buildEvpack(std::move(incompatible));
    REQUIRE(incompatibleBytes.ok());
    auto incompatibleMount = prepareEvpackMount(incompatibleBytes.value());
    REQUIRE(incompatibleMount.ok());
    auto rejectedReplacement = registry.commit(std::move(incompatibleMount).takeValue());
    REQUIRE(!rejectedReplacement.ok());
    CHECK_EQ(rejectedReplacement.error()->code(), DiagnosticCode::NotFound);
    REQUIRE(registry.resolve(providerReceipt.value().handle).ok());
}

TEST_CASE("asset.evpack.assetHandlesDetectProviderReplacementUnloadAndConflicts") {
    EvpackRegistry registry;
    auto           initialBytes = buildEvpack(buildInput());
    REQUIRE(initialBytes.ok());
    auto initial = prepareEvpackMount(initialBytes.value());
    REQUIRE(initial.ok());
    auto mounted = registry.commit(std::move(initial).takeValue());
    REQUIRE(mounted.ok());
    auto asset = AssetRef::fromId(id("550e8400-e29b-41d4-a716-446655440001"));
    REQUIRE(asset.ok());
    const EvpackCapabilities capabilities{"windows", "x86_64", "vulkan", {"bc"}, {"spirv-1.6"}, {"high"}, {}};
    auto                     handle = registry.resolveAsset(asset.value(), "eve.mesh/2", capabilities);
    REQUIRE(handle.ok());

    auto replacement      = buildInput();
    replacement.buildId   = id("018f6f22-2490-7ad2-bf58-4f1dbca31042");
    auto replacementBytes = buildEvpack(std::move(replacement));
    REQUIRE(replacementBytes.ok());
    auto replacementMount = prepareEvpackMount(replacementBytes.value());
    REQUIRE(replacementMount.ok());
    auto replaced = registry.commit(std::move(replacementMount).takeValue());
    REQUIRE(replaced.ok());
    auto stale = registry.readAsset(handle.value(), 1024);
    REQUIRE(!stale.ok());
    CHECK_EQ(stale.error()->code(), DiagnosticCode::StaleHandle);

    auto current = registry.resolveAsset(asset.value(), "eve.mesh/2", capabilities);
    REQUIRE(current.ok());
    auto unmounted = registry.unmount(replaced.value().handle);
    REQUIRE(unmounted.ok());
    auto missing = registry.readAsset(current.value(), 1024);
    REQUIRE(!missing.ok());
    CHECK_EQ(missing.error()->code(), DiagnosticCode::NotFound);

    auto firstBytes = buildEvpack(buildInput());
    REQUIRE(firstBytes.ok());
    auto firstMount = prepareEvpackMount(firstBytes.value());
    REQUIRE(firstMount.ok());
    REQUIRE(registry.commit(std::move(firstMount).takeValue()).ok());
    auto duplicate      = buildInput();
    duplicate.packageId = id("018f6f22-2490-7ad2-bf58-4f1dbca31090");
    duplicate.buildId   = id("018f6f22-2490-7ad2-bf58-4f1dbca31091");
    auto duplicateBytes = buildEvpack(std::move(duplicate));
    REQUIRE(duplicateBytes.ok());
    auto duplicateMount = prepareEvpackMount(duplicateBytes.value());
    REQUIRE(duplicateMount.ok());
    auto conflict = registry.commit(std::move(duplicateMount).takeValue());
    REQUIRE(!conflict.ok());
    CHECK_EQ(conflict.error()->code(), DiagnosticCode::Conflict);
}

TEST_CASE("asset.evpack.signaturePolicySeparatesTrustFromContentIntegrity") {
    EvpackTrust required{EvpackSignaturePolicy::RequireTrustedSignature, std::make_shared<TestVerifier>()};
    auto        unsignedBytes = buildEvpack(buildInput());
    REQUIRE(unsignedBytes.ok());
    auto unsignedRejected = parseEvpack(unsignedBytes.value(), {}, required);
    REQUIRE(!unsignedRejected.ok());
    CHECK_EQ(unsignedRejected.error()->code(), DiagnosticCode::PreconditionViolation);

    auto signedInput   = buildInput();
    signedInput.signer = std::make_shared<TestSigner>();
    auto signedBytes   = buildEvpack(std::move(signedInput));
    REQUIRE(signedBytes.ok());
    auto noTrust = parseEvpack(signedBytes.value());
    REQUIRE(!noTrust.ok());
    CHECK_EQ(noTrust.error()->code(), DiagnosticCode::PreconditionViolation);
    auto trusted = parseEvpack(signedBytes.value(), {}, required);
    REQUIRE(trusted.ok());
    CHECK(trusted.value().hasTrustedSignature());
    REQUIRE(trusted.value().signature().has_value());
    CHECK_EQ(trusted.value().signature()->algorithm, std::string("test-digest"));

    auto rangeSource = std::make_shared<CountingRangeSource>(signedBytes.value());
    auto range       = prepareEvpackRangeMount(rangeSource, {}, required);
    REQUIRE(range.ok());
    CHECK(range.value().index().hasTrustedSignature());

    auto                              corrupt = signedBytes.value();
    const std::array<std::uint8_t, 8> magic   = {'E', 'V', 'S', 'I', 'G', 0, 1, 0};
    const auto                        block   = std::search(corrupt.begin(), corrupt.end(), magic.begin(), magic.end());
    REQUIRE(block != corrupt.end());
    *(block + 56) ^= 1;
    auto rejected = parseEvpack(corrupt, {}, required);
    REQUIRE(!rejected.ok());
    CHECK_EQ(rejected.error()->code(), DiagnosticCode::HashMismatch);
}
}  // namespace test_asset_evpack
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_asset_runtime_definition)
#include "PrivateTestScope.h"
#include "asset/RuntimeDefinition.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// asset_runtime_definition.cpp
namespace test_asset_runtime_definition {
using namespace eve;
using namespace eve::asset;

TEST_CASE("asset.runtimeDefinition.isDeterministicAndRoundTripsAllValueKinds") {
    Value::Object root;
    root["array"]   = Value(Value::Array{Value(), Value(false), Value(std::int64_t(-7)), Value(1.25), Value("text")});
    root["enabled"] = Value(true);
    root["nested"]  = Value(Value::Object{{"z", Value(std::int64_t(9))}});
    auto first      = encodeRuntimeDefinition(Value(root));
    auto second     = encodeRuntimeDefinition(Value(root));
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    CHECK_EQ(first.value(), second.value());
    REQUIRE(first.value().size() > 8);
    CHECK_EQ(std::string(reinterpret_cast<const char*>(first.value().data()), 5), std::string("EVDEF"));
    auto decoded = decodeRuntimeDefinition(first.value());
    REQUIRE(decoded.ok());
    auto json     = decoded.value().toJson();
    auto expected = Value(std::move(root)).toJson();
    REQUIRE(json.ok());
    REQUIRE(expected.ok());
    CHECK_EQ(json.value(), expected.value());
}

TEST_CASE("asset.runtimeDefinition.rejectsUnknownTagsTrailingBytesAndBudgets") {
    auto encoded = encodeRuntimeDefinition(Value(Value::Object{{"key", Value("value")}}));
    REQUIRE(encoded.ok());
    auto unknown = encoded.value();
    unknown[8]   = 0xff;
    auto badTag  = decodeRuntimeDefinition(unknown);
    REQUIRE(!badTag.ok());
    CHECK_EQ(badTag.error()->code(), DiagnosticCode::ParseError);

    auto trailing = encoded.value();
    trailing.push_back(0);
    auto badTrailing = decodeRuntimeDefinition(trailing);
    REQUIRE(!badTrailing.ok());
    CHECK_EQ(badTrailing.error()->code(), DiagnosticCode::ParseError);

    RuntimeDefinitionLimits limits;
    limits.maximumBytes = encoded.value().size() - 1;
    auto tooLarge       = decodeRuntimeDefinition(encoded.value(), limits);
    REQUIRE(!tooLarge.ok());
    CHECK_EQ(tooLarge.error()->code(), DiagnosticCode::ParseError);

    limits                    = {};
    limits.maximumStringBytes = 3;
    auto stringBudget         = decodeRuntimeDefinition(encoded.value(), limits);
    REQUIRE(!stringBudget.ok());

    auto invalidUtf8 = encodeRuntimeDefinition(Value("x"));
    REQUIRE(invalidUtf8.ok());
    invalidUtf8.value().back() = 0xff;
    auto utf8Rejected          = decodeRuntimeDefinition(invalidUtf8.value());
    REQUIRE(!utf8Rejected.ok());
    CHECK_EQ(utf8Rejected.error()->code(), DiagnosticCode::ParseError);
}
}  // namespace test_asset_runtime_definition
#endif
