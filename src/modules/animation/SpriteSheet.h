#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace eve::graphics {
class Quad;
class Texture;
}

namespace eve::animation {

/**
 * @brief Sprite-sheet / texture-atlas frame table (pixel rects).
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

    /** @brief Append a named frame. Returns frame index. Empty name → "frameN". */
    int addFrame(const std::string &name, int x, int y, int w, int h);
    /** @brief Append a trimmed frame with original canvas size and content offset. */
    int addFrameTrimmed(const std::string &name, int x, int y, int w, int h,
                        int sourceW, int sourceH, int offsetX, int offsetY);

    /**
     * @brief Generate frames in row-major order from a grid.
     * frameW/frameH are cell sizes; margin is outer padding; spacing is gap between cells.
     * Returns number of frames added.
     */
    int setGrid(int columns, int rows, int frameW, int frameH, int margin = 0, int spacing = 0,
                int originX = 0, int originY = 0);

    void clear();

    /** @brief Optional atlas texture produced by a sequence loader or assigned by the user. */
    void setTexture(graphics::Texture *texture) { texture_ = texture; }
    /** @brief Return the optional atlas texture without transferring ownership. */
    graphics::Texture *getTexture() const { return texture_; }

    int         getFrameCount() const { return static_cast<int>(frames_.size()); }
    int         findFrame(const std::string &name) const;
    std::string getFrameName(int index) const;
    int         getFrameX(int index) const;
    int         getFrameY(int index) const;
    int         getFrameWidth(int index) const;
    int         getFrameHeight(int index) const;
    int getFrameSourceWidth(int index) const;
    int getFrameSourceHeight(int index) const;
    int getFrameOffsetX(int index) const;
    int getFrameOffsetY(int index) const;

    /** @brief Write pixel viewport into an existing Quad (does not allocate). */
    void applyToQuad(graphics::Quad *quad, int frameIndex) const;
    /** @brief Duplicate frame metadata while sharing the borrowed atlas texture. */
    SpriteSheet *clone() const;

private:
    struct Frame {
        std::string name;
        int         x = 0, y = 0, w = 0, h = 0;
        int sourceW = 0, sourceH = 0, offsetX = 0, offsetY = 0;
    };

    void        checkIndex(int index) const;
    std::vector<Frame>                     frames_;
    std::unordered_map<std::string, int>   byName_;
    graphics::Texture                    *texture_ = nullptr;
};

}  // namespace eve::animation
