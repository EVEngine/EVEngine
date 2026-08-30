#include "asset/EvaManifest.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

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
    std::string json = kManifest;
    const std::string from = "\"schemaVersion\":1";
    json.replace(json.find(from), from.size(), "\"schemaVersion\":2");
    auto parsed = parseEvaManifest(json);
    REQUIRE(!parsed.ok());
    CHECK_EQ(parsed.error()->code(), DiagnosticCode::UnknownVersion);
    CHECK_EQ(parsed.error()->path(), std::string("$.schemaVersion"));
}

TEST_CASE("asset.eva.manifest.rejectsDuplicateAssetIdentity") {
    std::string json = kManifest;
    const std::string needle = "}],\n  \"dependencies\"";
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
    std::string json = kManifest;
    const std::string from = "\"dependencies\":[]";
    const std::string to = R"json("dependencies":[{
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
    std::string json = kManifest;
    const std::string from = "\"dependencies\":[]";
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
    CHECK_EQ(roundTrip.value().dependencies.front().fallback,
             parsed.value().dependencies.front().fallback);

    std::string missingJson = kManifest;
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
