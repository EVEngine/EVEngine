#include "editor/FieldTargets.h"

#include "editor/TileBuffer.h"
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

}  // namespace eve::editor
