#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace eve::graphics {
class Quad;
}

namespace eve::animation {

/**
 * Sprite-sheet / texture-atlas frame table (pixel rects).
 *
 * Frames may be added by name or generated from a uniform grid.
 * Does not own a Texture — pair with Graphics textures in script.
 * Script type: `SpriteSheet`.
 */
class SpriteSheet {
public:
    SpriteSheet() = default;
    ~SpriteSheet() = default;

    SpriteSheet(const SpriteSheet &)            = delete;
    SpriteSheet &operator=(const SpriteSheet &) = delete;

    /** Append a named frame. Returns frame index. Empty name → "frameN". */
    int addFrame(const std::string &name, int x, int y, int w, int h);

    /**
     * Generate frames in row-major order from a grid.
     * frameW/frameH are cell sizes; margin is outer padding; spacing is gap between cells.
     * Returns number of frames added.
     */
    int setGrid(int columns, int rows, int frameW, int frameH, int margin = 0, int spacing = 0,
                int originX = 0, int originY = 0);

    void clear();

    int         getFrameCount() const { return static_cast<int>(frames_.size()); }
    int         findFrame(const std::string &name) const;
    std::string getFrameName(int index) const;
    int         getFrameX(int index) const;
    int         getFrameY(int index) const;
    int         getFrameWidth(int index) const;
    int         getFrameHeight(int index) const;

    /** Write pixel viewport into an existing Quad (does not allocate). */
    void applyToQuad(graphics::Quad *quad, int frameIndex) const;

private:
    struct Frame {
        std::string name;
        int         x = 0, y = 0, w = 0, h = 0;
    };

    void        checkIndex(int index) const;
    std::vector<Frame>                     frames_;
    std::unordered_map<std::string, int>   byName_;
};

}  // namespace eve::animation
