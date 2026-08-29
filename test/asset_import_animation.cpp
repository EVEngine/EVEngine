#include "asset/AssetCooker.h"
#include "asset_import/LegacyAnimationImporter.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <bit>
#include <fstream>
#include <sstream>

using namespace eve;
using namespace eve::asset;
using namespace eve::asset_import;

namespace {
ImportPackageIdentity identity() {
    auto id = PersistentId::parse("018f6f22-2490-7ad2-bf58-4f1dbca31040");
    REQUIRE(id.has_value());
    return {*id, "mixamo.idle", "1.0.0",
            {{"license", Value(Value::Object{{"redistribution", Value("project-only")}})}}};
}
std::string fixture() {
    std::ifstream input("test/assets/mixamo/Idle.anim.txt");
    REQUIRE(input.good());
    std::ostringstream text; text << input.rdbuf(); return text.str();
}
AssetCookProfile target(std::string os, std::string arch, std::string graphics,
                        std::string shader) {
    AssetCookProfile result;
    result.variant = {std::move(os), std::move(arch), std::move(graphics), {"rgba8"},
                      std::move(shader), "high", {}};
    result.chunkCodec = EvpackCodec::Zstd;
    return result;
}
}  // namespace

TEST_CASE("asset.import.animationFixtureBecomesCanonicalEvaAndTwoRuntimeTargets") {
    auto prepared = prepareLegacyAnimationImport(
        {identity(), "Idle.anim.txt", fixture(), {}});
    REQUIRE(prepared.ok());
    REQUIRE_EQ(prepared.value().manifest.assets.size(), std::size_t(2));
    REQUIRE_EQ(prepared.value().manifest.dependencies.size(), std::size_t(1));
    CHECK_EQ(prepared.value().manifest.dependencies.front().expectedType,
             std::string("eve.skeleton/1"));
    auto archiveBytes = buildEvaArchive(prepared.value().manifest, prepared.value().entries);
    REQUIRE(archiveBytes.ok());
    auto archive = parseEvaArchive(archiveBytes.value());
    REQUIRE(archive.ok());

    const auto skeleton = std::find_if(archive.value().entries.begin(), archive.value().entries.end(),
                                       [](const EvaArchiveEntry& entry) {
                                           return entry.path.ends_with("skeleton.bin");
                                       });
    REQUIRE(skeleton != archive.value().entries.end());
    REQUIRE(skeleton->bytes.size() > 32);
    CHECK_EQ(std::string(reinterpret_cast<const char*>(skeleton->bytes.data()), 6), std::string("EVSKEL"));
    // First root bind translation remains zero after centimeter -> meter conversion.
    const std::uint32_t firstPositionBits = std::uint32_t(skeleton->bytes[28]) |
        (std::uint32_t(skeleton->bytes[29]) << 8) | (std::uint32_t(skeleton->bytes[30]) << 16) |
        (std::uint32_t(skeleton->bytes[31]) << 24);
    CHECK_EQ(std::bit_cast<float>(firstPositionBits), 0.0f);

    auto windows = cookEvaToEvpack(archive.value(), target("windows", "x86_64", "vulkan", "spirv-1.6"));
    auto web = cookEvaToEvpack(archive.value(), target("web", "wasm32", "webgpu", "wgsl-1"));
    REQUIRE(windows.ok()); REQUIRE(web.ok());
    CHECK(windows.value().buildId != web.value().buildId);
    REQUIRE(parseEvpack(windows.value().bytes).ok());
    REQUIRE(parseEvpack(web.value().bytes).ok());
}

TEST_CASE("asset.import.animationFixtureRejectsDeclaredKeyCountMismatch") {
    std::string text = fixture();
    const auto track = text.find("track ");
    REQUIRE(track != std::string::npos);
    const auto firstCount = text.find(' ', track + 6);
    REQUIRE(firstCount != std::string::npos);
    text.insert(firstCount + 1, "9");
    auto rejected = prepareLegacyAnimationImport({identity(), "corrupt.anim.txt", std::move(text), {}});
    REQUIRE(!rejected.ok());
    CHECK_EQ(rejected.error()->code(), DiagnosticCode::ParseError);
}
