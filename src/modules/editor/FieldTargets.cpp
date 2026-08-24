#include "editor/FieldTargets.h"

#include "editor/TileBuffer.h"
#ifdef EVENGINE_HAS_MAP
#include "map/TileLayer.h"
#endif
#ifdef EVENGINE_HAS_PROCGEN
#include "procgen/heightmap/Heightmap.h"
#endif

#include <utility>

namespace eve::editor {

TileBufferTarget::TileBufferTarget(std::string id, TileBuffer *buffer)
    : id_(std::move(id)), buffer_(buffer) {}
int TileBufferTarget::width() const { return buffer_ ? buffer_->getWidth() : 0; }
int TileBufferTarget::height() const { return buffer_ ? buffer_->getHeight() : 0; }
bool TileBufferTarget::inBounds(int x, int y) const { return buffer_ && buffer_->inBounds(x, y); }
int TileBufferTarget::readInt(int x, int y) const { return inBounds(x, y) ? buffer_->getGid(x, y) : 0; }
bool TileBufferTarget::writeInt(int x, int y, int value) {
    if (!inBounds(x, y) || buffer_->getGid(x, y) == value) return false;
    buffer_->setGid(x, y, value);
    ++revision_;
    dirty_.include(x, y);
    return true;
}

#ifdef EVENGINE_HAS_MAP
TileLayerTarget::TileLayerTarget(std::string id, map::TileLayer *layer)
    : id_(std::move(id)), layer_(layer) {}
unsigned long long TileLayerTarget::revision() const {
    return layer_ ? layer_->tiles()->revision : 0;
}
int TileLayerTarget::width() const { return layer_ ? layer_->getMapWidth() : 0; }
int TileLayerTarget::height() const { return layer_ ? layer_->getMapHeight() : 0; }
bool TileLayerTarget::inBounds(int x, int y) const {
    return layer_ && x >= 0 && y >= 0 && x < width() && y < height();
}
int TileLayerTarget::readInt(int x, int y) const { return inBounds(x, y) ? layer_->getTile(x, y) : 0; }
bool TileLayerTarget::writeInt(int x, int y, int value) {
    if (!inBounds(x, y)) return false;
    if (layer_->getTile(x, y) == value) return true;
    layer_->setTile(x, y, value);
    dirty_.include(x, y);
    return true;
}
#endif

#ifdef EVENGINE_HAS_PROCGEN
HeightmapTarget::HeightmapTarget(std::string id, procgen::Heightmap *heightmap)
    : id_(std::move(id)), heightmap_(heightmap) {}
int HeightmapTarget::width() const { return heightmap_ ? heightmap_->getWidth() : 0; }
int HeightmapTarget::height() const { return heightmap_ ? heightmap_->getHeight() : 0; }
bool HeightmapTarget::inBounds(int x, int y) const { return heightmap_ && heightmap_->inBounds(x, y); }
float HeightmapTarget::readScalar(int x, int y) const {
    return inBounds(x, y) ? heightmap_->height(x, y) : 0.f;
}
bool HeightmapTarget::writeScalar(int x, int y, float value) {
    if (!inBounds(x, y) || heightmap_->height(x, y) == value) return false;
    heightmap_->setHeight(x, y, value);
    ++revision_;
    dirty_.include(x, y);
    return true;
}
float HeightmapTarget::sampleScalar(float x, float y) const {
    return heightmap_ ? heightmap_->sampleBilinear(x, y) : 0.f;
}
#endif

}  // namespace eve::editor
