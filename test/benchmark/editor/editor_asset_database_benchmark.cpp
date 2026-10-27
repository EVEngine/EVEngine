// eve-test-policy:
// {"schema":"evengine.test-policy","version":1,"role":"tests","default":"performance","module":"editor"}
#include "editor/EditorAssetDatabase.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::editor;

TEST_CASE("editor.v2.asset_database_indexes_and_pages_100k_records") {
    MemoryAssetDatabase database;
    constexpr int       count = 100000;
    for (int index = 0; index < count; ++index) {
        AssetRecord record;
        record.guid       = AssetGuid("asset-" + std::to_string(index));
        record.logicalUri = "content://Bulk/asset-" + std::to_string(index);
        record.typeId     = index % 2 == 0 ? "bulk.even" : "bulk.odd";
        REQUIRE(database.publish(std::move(record)).ok());
    }
    CHECK_EQ(database.generation(), static_cast<std::uint64_t>(count));
    AssetQuery query;
    query.typeIds = {"bulk.odd"};
    auto page     = database.query(query, 1000, 128, database.generation());
    REQUIRE(page.ok());
    CHECK_EQ(page.value().values.size(), static_cast<std::size_t>(128));
    CHECK(page.value().hasMore);
    CHECK(database.findByUri("content://Bulk/asset-99999").ok());
}
