#include "zeroerr/assert.h"

#include "common/Module.h"
#include "graphics/Graphics.h"
#include "procgen/graphics_editing/HeightmapMesh.h"
#include "procgen/heightmap/Heightmap.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// Responsibility: heightmap_preview_admission
namespace test_heightmap_preview_admission {
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
}  // namespace test_heightmap_preview_admission

#include "stylize/editor/EditorStylizeTarget.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// Responsibility: stylize_preview_admission
namespace test_stylize_preview_admission {
TEST_CASE("authoringPreview.stylizeMissingProviderPreservesDocument") {
    eve::editor::StylizeRecipeTarget            document("recipe");
    const auto                                  revision = document.revision();
    const auto                                  before   = document.snapshotValue();
    eve::editor::StylizeOffscreenPreviewService previews(nullptr, nullptr);
    auto rejected = previews.render(document, nullptr, eve::editor::StableId("preview"), 32, 32);
    REQUIRE(!rejected.ok());
    CHECK(rejected.status().code() == eve::StatusCode::Rejected);
    CHECK(rejected.status().hasDiagnostics());
    CHECK(document.revision() == revision);
    CHECK(document.snapshotValue() == before);
}
}  // namespace test_stylize_preview_admission
