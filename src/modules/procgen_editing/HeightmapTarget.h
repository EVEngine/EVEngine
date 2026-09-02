#pragma once

#include "editing/EditableTarget.h"
#include "procgen_editing/HeightmapBrush.h"

#include <memory>
#include <string>

namespace eve::procgen { class Heightmap; }

namespace eve::procgen_editing {

/** @brief Non-owning scalar-field adapter for a live procedural heightmap. */
class HeightmapTarget final : public editing::IEditableTarget, public editing::IScalarFieldTarget {
public:
    /** @brief Bind a live heightmap which must outlive this adapter. */
    HeightmapTarget(std::string id, procgen::Heightmap* heightmap);
    editing::TargetId targetId() const override { return editing::TargetId(id_); }
    std::uint64_t revision() const override { return revision_; }
    editing::EditRegion dirtyRegion() const override { return dirty_; }
    void clearDirtyRegion() override { dirty_.clear(); }
    int width() const override;
    int height() const override;
    bool containsCell(int x, int y) const override;
    float readScalar(int x, int y) const override;
    editing::FieldWriteStatus writeScalar(int x, int y, float value) override;
    float sampleScalar(float x, float y) const override;
    /** @brief Return the borrowed live heightmap.
     * @return Borrowed pointer owned by the caller that constructed this adapter.
     * @lifetime Valid only while the source heightmap outlives this adapter.
     */
    procgen::Heightmap* heightmap() const { return heightmap_; }

private:
    std::string id_;
    procgen::Heightmap* heightmap_ = nullptr;
    unsigned long long revision_ = 0;
    editing::EditRegion dirty_;
};

/**
 * @brief Create a heightmap adapter.
 * @param id Stable target identity.
 * @param heightmap Borrowed heightmap that must outlive the adapter.
 * @return Independently owned adapter.
 */
[[nodiscard]] std::unique_ptr<HeightmapTarget> createHeightmapTarget(
    std::string id, procgen::Heightmap* heightmap);
}  // namespace eve::procgen_editing
