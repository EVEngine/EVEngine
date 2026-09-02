#include "level_editing/TileBuffer.h"

#include "common/Exception.h"

#include <algorithm>

namespace eve::level_editing {

TileBuffer::TileBuffer(int width, int height) {
    if (width <= 0 || height <= 0) throw Exception("TileBuffer: width/height must be > 0");
    width_  = width;
    height_ = height;
    gids_.assign(static_cast<size_t>(width_) * static_cast<size_t>(height_), 0);
}

void TileBuffer::resize(int width, int height) {
    if (width <= 0 || height <= 0) throw Exception("TileBuffer::resize: width/height must be > 0");
    std::vector<int> next(static_cast<size_t>(width) * static_cast<size_t>(height), 0);
    int              copyW = std::min(width, width_);
    int              copyH = std::min(height, height_);
    for (int y = 0; y < copyH; ++y) {
        for (int x = 0; x < copyW; ++x) {
            next[static_cast<size_t>(y * width + x)] = gids_[static_cast<size_t>(y * width_ + x)];
        }
    }
    width_  = width;
    height_ = height;
    gids_.swap(next);
}

void TileBuffer::clear() { std::fill(gids_.begin(), gids_.end(), 0); }

void TileBuffer::fill(int gid) { std::fill(gids_.begin(), gids_.end(), gid); }

bool TileBuffer::containsCell(int x, int y) const { return x >= 0 && y >= 0 && x < width_ && y < height_; }

void TileBuffer::setGid(int x, int y, int gid) {
    if (!containsCell(x, y)) throw Exception("TileBuffer::setGid: out of bounds");
    gids_[static_cast<size_t>(index(x, y))] = gid;
}

int TileBuffer::getGid(int x, int y) const {
    if (!containsCell(x, y)) throw Exception("TileBuffer::getGid: out of bounds");
    return gids_[static_cast<size_t>(index(x, y))];
}

}  // namespace eve::level_editing
