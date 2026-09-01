#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "editor/EditCommand.h"
#include "editor/Editor.h"
#include "map/Map.h"
#include "map/TileLayer.h"
#include "map_editing/TileLayerTarget.h"

#include <memory>

using namespace eve::editor;

TEST_CASE("editor.map.tileLayerTargetUsesLiveRevision") {
    auto* map   = eve::map::Map::create();
    auto* layer = map->newLayer(4, 3, 8.f, 8.f);
    std::unique_ptr<eve::map_editing::TileLayerTarget> target =
        eve::map_editing::createTileLayerTarget("ground", layer);
    const auto before = target->revision();
    CHECK_EQ(static_cast<int>(target->writeInt(2, 1, 9)), static_cast<int>(FieldWriteStatus::Applied));
    CHECK_EQ(layer->getTile(2, 1), 9);
    CHECK_GT(target->revision(), before);
    CHECK_EQ(target->dirtyRegion().minX, 2);
    CHECK_EQ(target->dirtyRegion().maxY, 1);
    IntFieldEditCommand command("paint", target.get());
    CHECK(command.record(2, 1, 4));
    CHECK(command.apply());
    CHECK_EQ(layer->getTile(2, 1), 4);
    command.revert();
    CHECK_EQ(layer->getTile(2, 1), 9);
}
