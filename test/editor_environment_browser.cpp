#include "editor/EditorEnvironmentBrowser.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::editor;

namespace {
AssetRecord environment(const char* id, const char* uri, const char* layout,
                        int width, int height, int faces = 1) {
    AssetRecord record;
    record.guid = AssetGuid(id); record.logicalUri = uri; record.typeId = "environment-map";
    record.sourceUri = std::string("source/") + id; record.sourceHash = "hash";
    record.importerId = "environment-test"; record.artifacts = {std::string("artifact/") + id};
    record.metadata = EditorValue::Object{{"layout", layout}, {"width", int64_t{width}},
                                           {"height", int64_t{height}}, {"faceCount", int64_t{faces}}};
    return record;
}
}

TEST_CASE("editor.environment.browser_pages_and_rejects_stale_generation") {
    MemoryAssetDatabase database;
    CHECK(database.publish(environment("sky-a", "env/sky-a", "equirectangular", 2048, 1024)).accepted());
    const auto generation = database.generation();
    EnvironmentAssetBrowser browser(&database);
    auto page = browser.query("sky", 0, 20, generation);
    REQUIRE(page.value);
    CHECK_EQ(page.value->values.size(), 1U);
    CHECK_EQ(page.value->values.front().layout, std::string("equirectangular"));
    CHECK(database.publish(environment("sky-b", "env/sky-b", "cubemap", 512, 512, 6)).accepted());
    CHECK_EQ(static_cast<int>(browser.query({}, 0, 20, generation).status),
             static_cast<int>(EditorStatus::Conflict));
}

TEST_CASE("editor.environment.browser_surfaces_invalid_import_metadata") {
    MemoryAssetDatabase database;
    CHECK(database.publish(environment("broken", "env/broken", "cubemap", 512, 512, 5)).accepted());
    EnvironmentAssetBrowser browser(&database);
    auto selected = browser.select(AssetGuid("broken"));
    CHECK_EQ(static_cast<int>(selected.status), static_cast<int>(EditorStatus::Rejected));
    REQUIRE(selected.value);
    CHECK(!selected.value->diagnostics.empty());
}
