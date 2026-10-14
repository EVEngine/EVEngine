#include <algorithm>
#include "asset/EvaArchive.h"
#include "asset/EvaManifest.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

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
