#pragma once

#include "editor/EditorTarget.h"

#include <string>

namespace eve::editor { class TileBuffer; }
#ifdef EVENGINE_HAS_MAP
#include "map_editing/TileLayerTarget.h"
#endif
#ifdef EVENGINE_HAS_PROCGEN
#include "procgen_editing/HeightmapTarget.h"
#endif
#ifdef EVENGINE_HAS_SNOW
#include "snow_editing/SnowFieldTarget.h"
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
    bool containsCell(int x, int y) const override;
    int readInt(int x, int y) const override;
    FieldWriteStatus writeInt(int x, int y, int value) override;
    TileBuffer *buffer() const { return buffer_; }
private:
    std::string id_;
    TileBuffer *buffer_ = nullptr;
    unsigned long long revision_ = 0;
    EditRegion dirty_;
};

#ifdef EVENGINE_HAS_MAP
using TileLayerTarget = eve::map_editing::TileLayerTarget;
#endif

#ifdef EVENGINE_HAS_PROCGEN
using HeightmapTarget = eve::procgen_editing::HeightmapTarget;
#endif

#ifdef EVENGINE_HAS_SNOW
using SnowFieldTarget = eve::snow_editing::SnowFieldTarget;
#endif

}  // namespace eve::editor
