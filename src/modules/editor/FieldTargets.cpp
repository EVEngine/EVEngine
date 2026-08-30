#include "editor/FieldTargets.h"

#include "editor/TileBuffer.h"
#ifdef EVENGINE_HAS_MAP
#include "map/TileLayer.h"
#endif
#ifdef EVENGINE_HAS_PROCGEN
#include "procgen/heightmap/Heightmap.h"
#endif
#ifdef EVENGINE_HAS_SNOW
#include "snow/SnowField.h"
#endif

#include <algorithm>
#include <cmath>
#include <utility>

namespace eve::editor {

TileBufferTarget::TileBufferTarget(std::string id, TileBuffer *buffer)
    : id_(std::move(id)), buffer_(buffer) {}
int TileBufferTarget::width() const { return buffer_ ? buffer_->getWidth() : 0; }
int TileBufferTarget::height() const { return buffer_ ? buffer_->getHeight() : 0; }
bool TileBufferTarget::containsCell(int x, int y) const { return buffer_ && buffer_->inBounds(x, y); }
int TileBufferTarget::readInt(int x, int y) const { return containsCell(x, y) ? buffer_->getGid(x, y) : 0; }
FieldWriteStatus TileBufferTarget::writeInt(int x, int y, int value) {
    if (!containsCell(x, y)) return FieldWriteStatus::Rejected;
    if (buffer_->getGid(x, y) == value) return FieldWriteStatus::Unchanged;
    buffer_->setGid(x, y, value);
    ++revision_;
    dirty_.include(x, y);
    return FieldWriteStatus::Applied;
}

#ifdef EVENGINE_HAS_MAP
TileLayerTarget::TileLayerTarget(std::string id, map::TileLayer *layer)
    : id_(std::move(id)), layer_(layer) {}
unsigned long long TileLayerTarget::revision() const {
    return layer_ ? layer_->tiles()->revision : 0;
}
int TileLayerTarget::width() const { return layer_ ? layer_->getMapWidth() : 0; }
int TileLayerTarget::height() const { return layer_ ? layer_->getMapHeight() : 0; }
bool TileLayerTarget::containsCell(int x, int y) const {
    return layer_ && x >= 0 && y >= 0 && x < width() && y < height();
}
int TileLayerTarget::readInt(int x, int y) const { return containsCell(x, y) ? layer_->getTile(x, y) : 0; }
FieldWriteStatus TileLayerTarget::writeInt(int x, int y, int value) {
    if (!containsCell(x, y)) return FieldWriteStatus::Rejected;
    if (layer_->getTile(x, y) == value) return FieldWriteStatus::Unchanged;
    layer_->setTile(x, y, value);
    dirty_.include(x, y);
    return FieldWriteStatus::Applied;
}
#endif

#ifdef EVENGINE_HAS_PROCGEN
HeightmapTarget::HeightmapTarget(std::string id, procgen::Heightmap *heightmap)
    : id_(std::move(id)), heightmap_(heightmap) {}
int HeightmapTarget::width() const { return heightmap_ ? heightmap_->getWidth() : 0; }
int HeightmapTarget::height() const { return heightmap_ ? heightmap_->getHeight() : 0; }
bool HeightmapTarget::containsCell(int x, int y) const { return heightmap_ && heightmap_->inBounds(x, y); }
float HeightmapTarget::readScalar(int x, int y) const {
    return containsCell(x, y) ? heightmap_->height(x, y) : 0.f;
}
FieldWriteStatus HeightmapTarget::writeScalar(int x, int y, float value) {
    if (!containsCell(x, y)) return FieldWriteStatus::Rejected;
    if (heightmap_->height(x, y) == value) return FieldWriteStatus::Unchanged;
    heightmap_->setHeight(x, y, value);
    ++revision_;
    dirty_.include(x, y);
    return FieldWriteStatus::Applied;
}
float HeightmapTarget::sampleScalar(float x, float y) const {
    return heightmap_ ? heightmap_->sampleBilinear(x, y) : 0.f;
}
#endif

#ifdef EVENGINE_HAS_SNOW
SnowFieldTarget::SnowFieldTarget(std::string id, snow::SnowField* field)
    : id_(std::move(id)), field_(field) {}
int SnowFieldTarget::width() const { return field_ ? field_->getWidth() : 0; }
int SnowFieldTarget::height() const { return field_ ? field_->getHeight() : 0; }
bool SnowFieldTarget::containsCell(int x, int y) const { return field_ && field_->inBounds(x, y); }
float SnowFieldTarget::readScalar(int x, int y) const { return containsCell(x, y) ? field_->height(x, y) : 0.0F; }
FieldWriteStatus SnowFieldTarget::writeScalar(int x, int y, float value) {
    if (!containsCell(x, y) || !std::isfinite(value)) return FieldWriteStatus::Rejected;
    value = std::clamp(value, 0.0F, 1.0F);
    if (field_->height(x, y) == value) return FieldWriteStatus::Unchanged;
    field_->setHeight(x, y, value); ++revision_; dirty_.include(x, y); return FieldWriteStatus::Applied;
}
float SnowFieldTarget::sampleScalar(float x, float y) const {
    if (!field_ || width() <= 0 || height() <= 0 || !std::isfinite(x) || !std::isfinite(y)) return 0.0F;
    x = std::clamp(x, 0.0F, static_cast<float>(width() - 1));
    y = std::clamp(y, 0.0F, static_cast<float>(height() - 1));
    const int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
    const int x1 = std::min(x0 + 1, width() - 1), y1 = std::min(y0 + 1, height() - 1);
    const float tx = x - x0, ty = y - y0;
    const float top = readScalar(x0, y0) + (readScalar(x1, y0) - readScalar(x0, y0)) * tx;
    const float bottom = readScalar(x0, y1) + (readScalar(x1, y1) - readScalar(x0, y1)) * tx;
    return top + (bottom - top) * ty;
}
#endif

}  // namespace eve::editor
