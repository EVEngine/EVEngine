#include "asset/AssetPackageStore.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <chrono>
#include <fstream>

using namespace eve;
using namespace eve::asset;

namespace {

PersistentId packageStoreId(std::string_view text) {
    auto parsed = PersistentId::parse(text);
    REQUIRE(parsed.has_value());
    return *parsed;
}

std::filesystem::path packageStoreRoot() {
    return std::filesystem::temp_directory_path() /
           ("eve_package_store_" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
}

EvpackBuild packageBuild(std::string_view buildId, std::uint8_t payload) {
    EvpackBuild build;
    build.packageId = packageStoreId("018f6f22-2490-7ad2-bf58-4f1dbca31040");
    build.buildId = packageStoreId(buildId);
    build.variants.push_back({"linux", "x86_64", "vulkan", {"rgba8"}, "spirv-1.6", "high", {}});
    build.chunks.push_back({packageStoreId("550e8400-e29b-41d4-a716-446655440000"),
                            "eve.image", SchemaVersion(1), 0, EvpackChunkKind::Bulk, 0,
                            EvpackCodec::None, 8, {}, {payload}});
    return build;
}

std::vector<std::uint8_t> readFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

}  // namespace

TEST_CASE("asset.packageStore.reopensBeforeAtomicPublishAndFailureKeepsOldEvpack") {
    const auto root = packageStoreRoot();
    const auto destination = root / "terrain.evpack";
    auto oldBytes = buildEvpack(packageBuild("018f6f22-2490-7ad2-bf58-4f1dbca31041", 1));
    REQUIRE(oldBytes.ok());
    AtomicAssetPackageStore initialStore;
    auto initial = initialStore.publishEvpack(destination, oldBytes.value());
    REQUIRE(initial.ok());
    REQUIRE(std::filesystem::is_regular_file(destination));
    const auto oldDiskBytes = readFile(destination);

    auto replacement = buildEvpack(packageBuild("018f6f22-2490-7ad2-bf58-4f1dbca31042", 2));
    REQUIRE(replacement.ok());
    AtomicAssetPackageStore rejectingStore([](const auto&, const auto&) {
        return PackagePublishGateDecision::Reject;
    });
    auto rejected = rejectingStore.publishEvpack(destination, replacement.value());
    REQUIRE(!rejected.ok());
    CHECK_EQ(rejected.error()->code(), DiagnosticCode::Cancelled);
    CHECK_EQ(readFile(destination), oldDiskBytes);
    auto stillValid = parseEvpack(readFile(destination));
    REQUIRE(stillValid.ok());
    CHECK_EQ(stillValid.value().buildId(),
             packageStoreId("018f6f22-2490-7ad2-bf58-4f1dbca31041"));

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("asset.packageStore.publishesAndReopensEva") {
    constexpr std::string_view json = R"json({
      "schema":"eve.asset-archive","schemaVersion":1,
      "packageId":"018f6f22-2490-7ad2-bf58-4f1dbca31040",
      "packageName":"store.test","packageVersion":"1.0.0","unknownFields":"preserve",
      "assets":[],"dependencies":[],"entrypoints":{},"provenance":{}
    })json";
    auto manifest = parseEvaManifest(json);
    REQUIRE(manifest.ok());
    const auto root = packageStoreRoot();
    const auto destination = root / "source.eva";
    AtomicAssetPackageStore store;
    auto receipt = store.publishEva(destination, manifest.value(), {{"sources/readme.txt", {'o', 'k'}}});
    REQUIRE(receipt.ok());
    auto parsed = parseEvaArchive(readFile(destination));
    REQUIRE(parsed.ok());
    CHECK_EQ(parsed.value().manifest.packageName, std::string("store.test"));
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}
