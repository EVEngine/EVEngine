#include "graphics/Quad.h"

namespace eve::graphics {

Quad::Quad() = default;

Quad::Quad(int x_, int y_, int w_, int h_) { setViewport(x_, y_, w_, h_); }

Quad::~Quad() = default;

void Quad::setViewport(int x_, int y_, int w_, int h_) {
    x = x_;
    y = y_;
    w = w_ > 0 ? w_ : 0;
    h = h_ > 0 ? h_ : 0;
}

void Quad::getUV(int texW, int texH, float &u0, float &v0, float &u1, float &v1) const {
    if (texW <= 0 || texH <= 0 || w <= 0 || h <= 0) {
        u0 = 0.f;
        v0 = 0.f;
        u1 = 1.f;
        v1 = 1.f;
        return;
    }
    const float tw = float(texW);
    const float th = float(texH);
    u0 = float(x) / tw;
    v0 = float(y) / th;
    u1 = float(x + w) / tw;
    v1 = float(y + h) / th;
}

}  // namespace eve::graphics
