#include "map_editing/TileLayerTarget.h"

#include "map/TileLayer.h"

#include <utility>

namespace eve::map_editing {

TileLayerTarget::TileLayerTarget(std::string id, map::TileLayer* layer)
    : id_(std::move(id)), layer_(layer) {}

std::uint64_t TileLayerTarget::revision() const { return layer_ ? layer_->tiles()->revision : 0; }
int TileLayerTarget::width() const { return layer_ ? layer_->getMapWidth() : 0; }
int TileLayerTarget::height() const { return layer_ ? layer_->getMapHeight() : 0; }
bool TileLayerTarget::containsCell(int x, int y) const {
    return layer_ && x >= 0 && y >= 0 && x < width() && y < height();
}
int TileLayerTarget::readInt(int x, int y) const { return containsCell(x, y) ? layer_->getTile(x, y) : 0; }
editing::FieldWriteStatus TileLayerTarget::writeInt(int x, int y, int value) {
    if (!containsCell(x, y)) return editing::FieldWriteStatus::Rejected;
    if (layer_->getTile(x, y) == value) return editing::FieldWriteStatus::Unchanged;
    layer_->setTile(x, y, value);
    dirty_.include(x, y);
    return editing::FieldWriteStatus::Applied;
}

std::unique_ptr<TileLayerTarget> createTileLayerTarget(std::string id, map::TileLayer* layer) {
    return std::make_unique<TileLayerTarget>(std::move(id), layer);
}

}  // namespace eve::map_editing
