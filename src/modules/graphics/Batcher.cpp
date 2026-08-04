#include "graphics/Batcher.h"
#include "zeroerr/assert.h"

namespace eve::graphics {

void Batcher::clear() { verts.clear(); }

void Batcher::addRect(float x, float y, float w, float h, const Color &color) {
    addTexturedRect(x, y, w, h, color, 0, 0, 1, 1);
}

void Batcher::addTexturedRect(float x, float y, float w, float h, const Color &color,
                              float u0, float v0, float u1, float v1) {
    const BatchVertex v0p{{x, y}, color, {u0, v0}};
    const BatchVertex v1p{{x + w, y}, color, {u1, v0}};
    const BatchVertex v2p{{x, y + h}, color, {u0, v1}};
    const BatchVertex v3p{{x + w, y + h}, color, {u1, v1}};
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
        v.pos.x = v.pos.x * sx - 1.0f;
        v.pos.y = 1.0f - v.pos.y * sy;
    }
}

}  // namespace eve::graphics
