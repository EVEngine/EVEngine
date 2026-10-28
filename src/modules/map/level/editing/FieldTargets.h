#pragma once
#include "common/Export.h"


#include "editing/EditableTarget.h"

#include <string>

namespace eve::level_editing { class TileBuffer; }

namespace eve::level_editing {

using editing::EditRegion;
using editing::FieldWriteStatus;
using editing::IEditableTarget;
using editing::IIntFieldTarget;
using editing::Revision;
using editing::TargetId;

/** @brief Non-owning IIntFieldTarget adapter for TileBuffer. */
class EVENGINE_API_DOMAINS TileBufferTarget final : public ::eve::editing::EditableTargetState,
                                                    public virtual IEditableTarget,
                                                    public IIntFieldTarget {
public:
    /** @brief Adapt a borrowed buffer that must outlive this target. @thread Owner-thread only. */
    TileBufferTarget(std::string id, TileBuffer *buffer);
    /** @brief Target id. */
    TargetId         targetId() const override { return TargetId(id_); }
    /** @brief Width. */
    int width() const override;
    /** @brief Height. */
    int height() const override;
    /** @brief True if cell. */
    bool containsCell(int x, int y) const override;
    /** @brief Reads int. */
    int readInt(int x, int y) const override;
    /** @brief Writes int. */
    FieldWriteStatus writeInt(int x, int y, int value) override;
    /** @brief Return the borrowed buffer; its lifetime remains owned by the target creator. */
    TileBuffer *buffer() const { return buffer_; }
private:
    std::string id_;
    TileBuffer *buffer_ = nullptr;
};

}  // namespace eve::level_editing
