#include "map/Map.h"
#include "map/TileLayer.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cstdint>

TEST_CASE("resourceFormats.map.tiledFlipBitsSurviveAllWritePaths") {
    auto          *layer   = eve::map::Map::create()->newLayer(2, 2, 16.f, 16.f);
    const uint32_t flipped = 0x80000001u;
    layer->setTile(0, 0, static_cast<int>(flipped));
    REQUIRE_EQ(static_cast<uint32_t>(layer->getTile(0, 0)), flipped);
    REQUIRE_EQ(layer->getNonEmptyChunkCount(), 1);
    layer->clear();
    layer->fillRect(0, 0, 2, 1, static_cast<int>(flipped));
    REQUIRE_EQ(static_cast<uint32_t>(layer->getTile(1, 0)), flipped);
    REQUIRE_EQ(layer->getTile(1, 1), 0);
    layer->fill(static_cast<int>(flipped));
    REQUIRE_EQ(static_cast<uint32_t>(layer->getTile(1, 1)), flipped);
    REQUIRE_EQ(layer->getNonEmptyChunkCount(), 1);
    layer->fill(static_cast<int>(0x80000000u));
    REQUIRE_EQ(layer->getNonEmptyChunkCount(), 0);
    layer->clear();
    layer->setVisible(false);
}
