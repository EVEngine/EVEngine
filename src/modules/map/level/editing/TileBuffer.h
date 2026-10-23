#pragma once
#include "common/Export.h"


#include <cstdint>
#include <vector>

namespace eve::level_editing {

/** @brief Independent GID grid for map brushes (no hard dependency on map.TileLayer). */
class EVENGINE_API_DOMAINS TileBuffer {
public:
    /** @brief Tile buffer. */
    TileBuffer(int width, int height);

    /** @brief Returns the width. */
    int getWidth() const { return width_; }
    /** @brief Returns the height. */
    int getHeight() const { return height_; }

    /** @brief Resize. */
    void resize(int width, int height);
    /** @brief Clears . */
    void clear();
    /** @brief Fill. */
    void fill(int gid);

    /** @brief Sets the gid. */
    void setGid(int x, int y, int gid);
    /** @brief Returns the gid. */
    int  getGid(int x, int y) const;

    /** @brief Return whether a cell belongs to this buffer. */
    bool containsCell(int x, int y) const;

private:
    int index(int x, int y) const { return y * width_ + x; }

    int              width_  = 0;
    int              height_ = 0;
    std::vector<int> gids_;
};

}  // namespace eve::level_editing
