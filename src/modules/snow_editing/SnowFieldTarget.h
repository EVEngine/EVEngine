#pragma once

#include "editing/EditableTarget.h"

#include <string>

namespace eve::snow { class SnowField; }

namespace eve::snow_editing {

/** @brief Non-owning scalar-field adapter for a live interactive snow field. */
class SnowFieldTarget final : public editing::IEditableTarget, public editing::IScalarFieldTarget {
public:
    /** @brief Bind a live snow field which must outlive this adapter. */
    SnowFieldTarget(std::string id, snow::SnowField* field);
    const std::string& targetId() const override { return id_; }
    unsigned long long revision() const override { return revision_; }
    editing::EditRegion dirtyRegion() const override { return dirty_; }
    void clearDirtyRegion() override { dirty_.clear(); }
    int width() const override;
    int height() const override;
    bool containsCell(int x, int y) const override;
    float readScalar(int x, int y) const override;
    editing::FieldWriteStatus writeScalar(int x, int y, float value) override;
    float sampleScalar(float x, float y) const override;
    /** @brief Return the borrowed live field.
     * @return Borrowed pointer owned by the caller that constructed this adapter.
     * @lifetime Valid only while the source field outlives this adapter.
     */
    snow::SnowField* field() const { return field_; }

private:
    std::string id_;
    snow::SnowField* field_ = nullptr;
    unsigned long long revision_ = 1;
    editing::EditRegion dirty_;
};

}  // namespace eve::snow_editing
