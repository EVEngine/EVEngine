#pragma once

#include "graphics/Drawable.h"
#include "graphics/Quad.h"

namespace eve::graphics {

class Texture : public Drawable {
public:
    void draw(Graphics* gfx, const glm::mat4& matrix) const override;

    // If you just want to draw part of the texture
    // void draw(Graphics *gfx, Quad *quad, const Matrix4 &m);

    int width;
    int height;

    int depth;
    int layers;
    int mipmapCount;

    int pixelWidth;
    int pixelHeight;

    // SamplerState samplerState;
};

}  // namespace eve::graphics
