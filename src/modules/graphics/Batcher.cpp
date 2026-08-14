#include "graphics/Batcher.h"
#include "zeroerr/assert.h"

#include <cmath>

namespace eve::graphics {

void Batcher::clear() { verts.clear(); }

void Batcher::addRect(float x, float y, float w, float h, const Color &color) {
    addTexturedRect(x, y, w, h, color, 0, 0, 1, 1);
}

void Batcher::addTexturedRect(float x, float y, float w, float h, const Color &color,
                              float u0, float v0, float u1, float v1, bool rotatedUV) {
    BatchVertex v0p, v1p, v2p, v3p;
    if (rotatedUV) {
        // Atlas-packed 90°-rotated region. Following spine-runtimes UV layout:
        // quad top edge (TL→TR) samples the texture's left column (v1→v0),
        // and quad left edge (TL→BL) samples the texture's U axis (u0→u1),
        // i.e. the quad renders the packed region un-rotated 90°.
        //   TL←(u0,v1), TR←(u0,v0), BL←(u1,v1), BR←(u1,v0)
        v0p = BatchVertex{{x, y}, color, {u0, v1}};
        v1p = BatchVertex{{x + w, y}, color, {u0, v0}};
        v2p = BatchVertex{{x, y + h}, color, {u1, v1}};
        v3p = BatchVertex{{x + w, y + h}, color, {u1, v0}};
    } else {
        v0p = BatchVertex{{x, y}, color, {u0, v0}};
        v1p = BatchVertex{{x + w, y}, color, {u1, v0}};
        v2p = BatchVertex{{x, y + h}, color, {u0, v1}};
        v3p = BatchVertex{{x + w, y + h}, color, {u1, v1}};
    }
    verts.push_back(v0p);
    verts.push_back(v1p);
    verts.push_back(v2p);
    verts.push_back(v1p);
    verts.push_back(v3p);
    verts.push_back(v2p);
}

void Batcher::addTexturedRectRotated(float cx, float cy, float w, float h, float degrees,
                                     const Color &color, float u0, float v0, float u1, float v1,
                                     bool rotatedUV) {
    const float rad = degrees * static_cast<float>(3.14159265358979323846) / 180.0f;
    const float cos = std::cos(rad);
    const float sin = std::sin(rad);
    const float hw = w * 0.5f;
    const float hh = h * 0.5f;

    auto corner = [&](float dx, float dy) -> glm::vec2 {
        return {cx + dx * cos - dy * sin, cy + dx * sin + dy * cos};
    };
    const glm::vec2 tl = corner(-hw, -hh);
    const glm::vec2 tr = corner(hw, -hh);
    const glm::vec2 bl = corner(-hw, hh);
    const glm::vec2 br = corner(hw, hh);

    BatchVertex v0p, v1p, v2p, v3p;
    if (rotatedUV) {
        // Atlas-packed 90°-rotated region: same corner→UV swizzle as
        // addTexturedRect (see comment there).
        v0p = BatchVertex{tl, color, {u0, v1}};
        v1p = BatchVertex{tr, color, {u0, v0}};
        v2p = BatchVertex{bl, color, {u1, v1}};
        v3p = BatchVertex{br, color, {u1, v0}};
    } else {
        v0p = BatchVertex{tl, color, {u0, v0}};
        v1p = BatchVertex{tr, color, {u1, v0}};
        v2p = BatchVertex{bl, color, {u0, v1}};
        v3p = BatchVertex{br, color, {u1, v1}};
    }
    verts.push_back(v0p);
    verts.push_back(v1p);
    verts.push_back(v2p);
    verts.push_back(v1p);
    verts.push_back(v3p);
    verts.push_back(v2p);
}

void Batcher::toNDC(int logicalW, int logicalH) {
    ASSERT_GT(logicalW, 0);
    ASSERT_GT(logicalH, 0);
    if (logicalW <= 0 || logicalH <= 0) return;
    const float sx = 2.0f / float(logicalW);
    const float sy = 2.0f / float(logicalH);
    for (auto &v : verts) {
        // Vulkan NDC: (-1,-1) = top-left, (+1,+1) = bottom-right (Y down).
        // Logical coords are also Y-down with origin at top-left.
        v.pos.x = v.pos.x * sx - 1.0f;
        v.pos.y = v.pos.y * sy - 1.0f;
    }
}

}  // namespace eve::graphics
