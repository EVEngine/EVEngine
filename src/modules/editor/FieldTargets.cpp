#include "editor/FieldTargets.h"

#include "editor/TileBuffer.h"
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
