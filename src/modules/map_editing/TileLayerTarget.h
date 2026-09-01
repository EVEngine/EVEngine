#pragma once

#include "editing/EditableTarget.h"

#include <memory>
#include <string>

namespace eve::map { class TileLayer; }

namespace eve::map_editing {

/** @brief Non-owning editable adapter for a live map tile layer. */
class TileLayerTarget final : public editing::IEditableTarget, public editing::IIntFieldTarget {
public:
    /** @brief Bind a live layer which must outlive this adapter. */
    TileLayerTarget(std::string id, map::TileLayer* layer);
    const std::string& targetId() const override { return id_; }
    unsigned long long revision() const override;
    editing::EditRegion dirtyRegion() const override { return dirty_; }
    void clearDirtyRegion() override { dirty_.clear(); }
    int width() const override;
    int height() const override;
    bool containsCell(int x, int y) const override;
    int readInt(int x, int y) const override;
    editing::FieldWriteStatus writeInt(int x, int y, int value) override;
    /** @brief Return the borrowed live layer.
     * @return Borrowed pointer owned by the caller that constructed this adapter.
     * @lifetime Valid only while the source layer outlives this adapter.
     */
    map::TileLayer* layer() const { return layer_; }

private:
    std::string id_;
    map::TileLayer* layer_ = nullptr;
    editing::EditRegion dirty_;
};

/**
 * @brief Create a map-layer adapter.
 * @param id Stable target identity.
 * @param layer Borrowed layer that must outlive the adapter.
 * @return Independently owned adapter.
 */
[[nodiscard]] std::unique_ptr<TileLayerTarget> createTileLayerTarget(std::string id,
                                                                    map::TileLayer* layer);

}  // namespace eve::map_editing
