#pragma once

#include <cstdint>
#include <vector>

namespace eve::editor {

/** @brief Independent GID grid for map brushes (no hard dependency on map.TileLayer). */
class TileBuffer {
public:
    TileBuffer(int width, int height);

    int getWidth() const { return width_; }
    int getHeight() const { return height_; }

    void resize(int width, int height);
    void clear();
    void fill(int gid);

    void setGid(int x, int y, int gid);
    int getGid(int x, int y) const;

    bool inBounds(int x, int y) const;

private:
    int index(int x, int y) const { return y * width_ + x; }

    int width_ = 0;
    int height_ = 0;
    std::vector<int> gids_;
};

}  // namespace eve::editor
