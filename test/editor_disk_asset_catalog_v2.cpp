#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "editor/EditorDiskAssetCatalog.h"

#include <chrono>
#include <filesystem>
#include <fstream>

using namespace eve::editor;

TEST_CASE("editor.v2.disk_asset_sidecar_preserves_guid_across_move_and_poll") {
    const auto root =
        std::filesystem::temp_directory_path() /
        ("eve_asset_catalog_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root / "Content" / "Trees");
    {
        std::ofstream source(root / "Content" / "Trees" / "Oak.asset", std::ios::binary);
        source << "oak-v1";
    }
    MemoryAssetDatabase database;
    DiskAssetCatalog    catalog(root, &database);
    REQUIRE(catalog.writeSidecar("Trees/Oak.asset", AssetGuid("guid-oak"), "park.tree").isAccepted());
    auto first = catalog.scan();
    REQUIRE(first.isAccepted());
    CHECK_EQ(first.value->indexed, static_cast<std::size_t>(1));
    CHECK_EQ(first.value->changed, static_cast<std::size_t>(1));
    CHECK_EQ(database.find(AssetGuid("guid-oak")).value->logicalUri, "content://Trees/Oak.asset");

    std::filesystem::create_directories(root / "Content" / "Plants");
    std::filesystem::rename(root / "Content" / "Trees" / "Oak.asset", root / "Content" / "Plants" / "Oak.asset");
    std::filesystem::rename(root / "Content" / "Trees" / "Oak.asset.evmeta",
                            root / "Content" / "Plants" / "Oak.asset.evmeta");
    auto moved = catalog.poll();
    REQUIRE(moved.isAccepted());
    CHECK_EQ(moved.value->changed, static_cast<std::size_t>(1));
    CHECK_EQ(database.find(AssetGuid("guid-oak")).value->logicalUri, "content://Plants/Oak.asset");

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}
