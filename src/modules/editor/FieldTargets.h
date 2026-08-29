#pragma once

#include "editor/EditorTarget.h"

#include <string>

namespace eve::editor { class TileBuffer; }
#ifdef EVENGINE_HAS_MAP
namespace eve::map { class TileLayer; }
#endif
#ifdef EVENGINE_HAS_PROCGEN
namespace eve::procgen { class Heightmap; }
#endif

namespace eve::editor {

/** @brief Non-owning IIntFieldTarget adapter for TileBuffer. */
class TileBufferTarget final : public IEditableTarget, public IIntFieldTarget {
public:
    TileBufferTarget(std::string id, TileBuffer *buffer);
    const std::string &targetId() const override { return id_; }
    unsigned long long revision() const override { return revision_; }
    EditRegion dirtyRegion() const override { return dirty_; }
    void clearDirtyRegion() override { dirty_.clear(); }
    int width() const override;
    int height() const override;
    bool inBounds(int x, int y) const override;
    int readInt(int x, int y) const override;
    bool writeInt(int x, int y, int value) override;
    TileBuffer *buffer() const { return buffer_; }
private:
    std::string id_;
    TileBuffer *buffer_ = nullptr;
    unsigned long long revision_ = 0;
    EditRegion dirty_;
};

#ifdef EVENGINE_HAS_MAP
/** @brief Non-owning editable adapter for a live map::TileLayer. */
class TileLayerTarget final : public IEditableTarget, public IIntFieldTarget {
public:
    TileLayerTarget(std::string id, map::TileLayer *layer);
    const std::string &targetId() const override { return id_; }
    unsigned long long revision() const override;
    EditRegion dirtyRegion() const override { return dirty_; }
    void clearDirtyRegion() override { dirty_.clear(); }
    int width() const override;
    int height() const override;
    bool inBounds(int x, int y) const override;
    int readInt(int x, int y) const override;
    bool writeInt(int x, int y, int value) override;
    map::TileLayer *layer() const { return layer_; }
private:
    std::string id_;
    map::TileLayer *layer_ = nullptr;
    EditRegion dirty_;
};
#endif

#ifdef EVENGINE_HAS_PROCGEN
/** @brief Non-owning IScalarFieldTarget adapter for procgen::Heightmap. */
class HeightmapTarget final : public IEditableTarget, public IScalarFieldTarget {
public:
    HeightmapTarget(std::string id, procgen::Heightmap *heightmap);
    const std::string &targetId() const override { return id_; }
    unsigned long long revision() const override { return revision_; }
    EditRegion dirtyRegion() const override { return dirty_; }
    void clearDirtyRegion() override { dirty_.clear(); }
    int width() const override;
    int height() const override;
    bool inBounds(int x, int y) const override;
    float readScalar(int x, int y) const override;
    bool writeScalar(int x, int y, float value) override;
    float sampleScalar(float x, float y) const override;
    procgen::Heightmap *heightmap() const { return heightmap_; }
private:
    std::string id_;
    procgen::Heightmap *heightmap_ = nullptr;
    unsigned long long revision_ = 0;
    EditRegion dirty_;
};
#endif

}  // namespace eve::editor
