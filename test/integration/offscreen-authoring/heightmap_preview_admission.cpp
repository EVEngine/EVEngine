#include "common/Module.h"
#include "graphics/Graphics.h"
#include "procgen/graphics_editing/HeightmapMesh.h"
#include "procgen/heightmap/Heightmap.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

TEST_CASE("authoringPreview.heightmapMissingProviderPreservesSource") {
    REQUIRE(eve::ModuleManager::getInstance<eve::graphics::Graphics>("Graphics") == nullptr);
    eve::procgen::Heightmap heightmap(3, 3);
    heightmap.setHeight(1, 1, 4.0F);
    const auto before  = heightmap.data();
    auto       created = eve::procgen_graphics_editing::createHeightmapMesh(&heightmap, 1.0F, 2.0F, true);
    REQUIRE(!created.ok());
    CHECK(created.status().code() == eve::StatusCode::Unsupported);
    CHECK(created.status().hasDiagnostics());
    CHECK(heightmap.data() == before);
    CHECK(eve::ModuleManager::getInstance<eve::graphics::Graphics>("Graphics") == nullptr);

    auto updated = eve::procgen_graphics_editing::updateHeightmapMesh(nullptr, nullptr, &heightmap, 1.0F, 2.0F, true);
    REQUIRE(!updated.ok());
    CHECK(updated.status().code() == eve::StatusCode::Rejected);
    CHECK(updated.status().hasDiagnostics());
    CHECK(heightmap.data() == before);
}
