#include "asset/AssetMigration.h"

#include "data/HashFunction.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve;
using namespace eve::asset;

namespace {

EvaArchive legacyImage(SchemaVersion version = SchemaVersion(1)) {
    const auto package = PersistentId::parse("018f6f22-2490-7ad2-bf58-4f1dbca31040");
    const auto identity = PersistentId::parse("550e8400-e29b-41d4-a716-446655440000");
    REQUIRE(package.has_value());
    REQUIRE(identity.has_value());
    auto reference = AssetRef::parse("asset://550e8400-e29b-41d4-a716-446655440000");
    REQUIRE(reference.ok());
    const std::string definition =
        version.value() == 1
            ? R"({"blob":"assets/550e8400-e29b-41d4-a716-446655440000/source.png","colorSpace":"srgb","encoding":"png","height":1,"rowOrientation":"top-down","schema":"eve.image","schemaVersion":1,"usage":"color","width":1})"
            : R"({"schema":"eve.image","schemaVersion":3})";
    EvaManifest manifest;
    manifest.packageId = *package;
    manifest.packageName = "migration.image";
    manifest.packageVersion = "1.0.0";
    manifest.assets.push_back({reference.value(), "eve.image", version,
                               "assets/550e8400-e29b-41d4-a716-446655440000/asset.json",
                               "sha256:0000000000000000000000000000000000000000000000000000000000000000",
                               {}});
    manifest.entrypoints.emplace("default", reference.value());
    std::vector<EvaArchiveEntry> entries = {
        {manifest.assets.front().definition, {definition.begin(), definition.end()}},
        {"assets/550e8400-e29b-41d4-a716-446655440000/source.png", {1}},
    };
    // Let the archive builder/parser calculate and enforce the real definition hash.
    data::HashFunction::Value digest{};
    data::HashFunction::getHashFunction("sha256")->hash("sha256", definition.data(), definition.size(), digest);
    static constexpr char hex[] = "0123456789abcdef";
    std::string hash = "sha256:";
    for (std::size_t index = 0; index < 32; ++index) {
        const auto byte = static_cast<std::uint8_t>(digest.data[index]);
        hash.push_back(hex[byte >> 4]); hash.push_back(hex[byte & 15]);
    }
    manifest.assets.front().contentHash = std::move(hash);
    auto bytes = buildEvaArchive(manifest, std::move(entries));
    REQUIRE(bytes.ok());
    auto parsed = parseEvaArchive(bytes.value());
    REQUIRE(parsed.ok());
    return std::move(parsed).takeValue();
}

}  // namespace

TEST_CASE("asset.migration.imageNMinusOneProducesCanonicalV2Candidate") {
    auto migrated = migrateEvaArchive(legacyImage());
    REQUIRE(migrated.ok());
    REQUIRE_EQ(migrated.value().manifest.assets.size(), std::size_t(1));
    CHECK_EQ(migrated.value().manifest.assets.front().schemaVersion, SchemaVersion(2));
    const auto& bytes = migrated.value().entries.front().bytes;
    auto definition = Value::fromJson(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    REQUIRE(definition.ok());
    const auto* object = definition.value().getIf<Value::Object>();
    REQUIRE(object != nullptr);
    CHECK(object->contains("color"));
    CHECK(!object->contains("colorSpace"));
    CHECK_EQ(object->at("schemaVersion"), Value(std::int64_t(2)));
}

TEST_CASE("asset.migration.rejectsUnknownNewVersionWithoutChangingInput") {
    EvaArchive original = legacyImage(SchemaVersion(3));
    const auto before = original.entries.front().bytes;
    auto rejected = migrateEvaArchive(original);
    REQUIRE(!rejected.ok());
    CHECK_EQ(rejected.error()->code(), DiagnosticCode::UnknownVersion);
    CHECK_EQ(original.entries.front().bytes, before);
}
