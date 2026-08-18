#pragma once

namespace eve::graphics {

/**
 * @brief Pixel-space sub-rectangle of a Texture (atlas cell / sprite frame).
 * UV conversion uses the texture's logical width/height.
 */
class Quad {
public:
    Quad();
    Quad(int x, int y, int w, int h);
    ~Quad();

    void setViewport(int x, int y, int w, int h);
    int getX() const { return x; }
    int getY() const { return y; }
    int getWidth() const { return w; }
    int getHeight() const { return h; }

    /** @brief Convert pixel rect to normalized UVs for a texture of size texW×texH. */
    void getUV(int texW, int texH, float &u0, float &v0, float &u1, float &v1) const;

    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

}  // namespace eve::graphics
