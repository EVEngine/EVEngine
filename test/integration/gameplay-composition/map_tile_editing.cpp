#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "map/TileLayer.h"
#include "map/editing/TileLayerTarget.h"

#include <bit>
#include <cstdint>

TEST_CASE("map.editing.preservesFlipBitsWhenRestoringFieldValue") {
    auto* layer = eve::map::TileLayer::createLayer(2, 2, 16.f, 16.f);
    layer->setVisible(false);
    const int flipped = std::bit_cast<int>(uint32_t{0x80000001});
    layer->setTile(0, 0, flipped);
    eve::map_editing::TileLayerTarget target("tile", layer);
    const int                         before = target.readInt(0, 0);
    CHECK_EQ(before, flipped);
    CHECK(target.writeInt(0, 0, 2) == eve::editing::FieldWriteStatus::Applied);
    CHECK(target.writeInt(0, 0, before) == eve::editing::FieldWriteStatus::Applied);
    CHECK_EQ(uint32_t(layer->getTile(0, 0)), uint32_t{0x80000001});
    layer->fillRect(0, 0, 2, 2, flipped);
    CHECK_EQ(layer->getTile(1, 1), flipped);
    layer->fill(flipped);
    CHECK_EQ(layer->getTile(1, 0), flipped);
}

TEST_CASE("map.editing.terrainBrushPreservesManualNeighbors") {
    auto* layer = eve::map::TileLayer::createLayer(3, 3, 16.f, 16.f);
    layer->setVisible(false);
    layer->fill(42);
    layer->setTerrainRule(7, 1, 0);
    layer->paintTerrain(1, 1, 1);
    CHECK_EQ(layer->getTile(1, 1), 7);
    CHECK_EQ(layer->getTile(0, 0), 42);
    CHECK_EQ(layer->getTile(2, 1), 42);
    layer->eraseTerrainRect(1, 1, 1, 1);
    CHECK_EQ(layer->getTile(1, 1), 0);
    CHECK_EQ(layer->getTile(0, 0), 42);
}
