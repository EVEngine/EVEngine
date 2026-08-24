#pragma once

#include "graphics/Canvas.h"
#include "graphics/BlendMode.h"
#include "graphics/Quad.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace eve::graphics {

class Camera2D;

/** @brief Shared 2D draw queue item (sprites + tiles). */
struct DrawItem2D {
    float x = 0.f;
    float y = 0.f;
    float w = 0.f;
    float h = 0.f;
    float depthY = 0.f;
    /** @brief Degrees, clockwise, around the rectangle center (screen Y-down). */
    float rotation = 0.f;
    /** @brief Normalized transform pivot; (0,0) top-left, (0.5,0.5) center. */
    float anchorX = 0.5f, anchorY = 0.5f;
    bool flipX = false, flipY = false;
    /** Explicit back-to-front order (e.g. Spine slot draw order). When set on
     *  both items it takes priority over depthY. */
    int  order = 0;
    bool hasOrder = false;
    Color color{1.f, 1.f, 1.f, 1.f};
    int layer = 0;
    /** @brief 2D quad blending (alpha / additive / opaque). */
    BlendMode blend = BlendMode::Alpha;
    Texture *texture = nullptr;
    Texture *normal = nullptr;
    Quad *quad = nullptr;
    Shader *shader = nullptr;
    Canvas *canvas = nullptr;
    Camera2D *camera = nullptr;
    bool receiveLight = true;
    bool litPath = false;
    bool hasUV = false;
    /** @brief Atlas-packed rotated region: corner UVs are remapped (rotated 90°). */
    bool rotatedUV = false;
    float u0 = 0.f, v0 = 0.f, u1 = 1.f, v1 = 1.f;
    /** @brief Screen-space camera already resolved; if cameraEntity set, RenderSystem resolves. */
    float camX = 0.f, camY = 0.f, camZoom = 1.f;
    bool camValid = false;
    Color camClear{0.1f, 0.1f, 0.12f, 1.f};
    float camAmbientR = 0.15f, camAmbientG = 0.15f, camAmbientB = 0.18f;
};

inline void sortDrawItems2D(std::vector<DrawItem2D> &items) {
    std::stable_sort(items.begin(), items.end(), [](const DrawItem2D &a, const DrawItem2D &b) {
        const bool aOff = a.canvas != nullptr;
        const bool bOff = b.canvas != nullptr;
        if (aOff != bOff) return aOff && !bOff;
        if (a.canvas != b.canvas) return a.canvas < b.canvas;
        if (a.layer != b.layer) return a.layer < b.layer;
        if (a.hasOrder && b.hasOrder && a.order != b.order) return a.order < b.order;
        if (a.depthY != b.depthY) return a.depthY < b.depthY;
        // Do not use pipeline/texture state as a tie-breaker: alpha compositing is
        // order-dependent. stable_sort preserves submission order for equal visual keys,
        // while the backend still batches adjacent compatible items.
        return false;
    });
}

}  // namespace eve::graphics
