#pragma once

#include "graphics/Canvas.h"
#include <vector>
#include <glm/glm.hpp>

namespace eve::graphics {

class Texture;

/** CPU-side vertex for 2D batching (logical or NDC depending on stage). */
struct BatchVertex {
    glm::vec2 pos;
    glm::vec4 color;
    glm::vec2 uv;
};

/**
 * Accumulates solid / textured quads in logical (Y-down) coordinates.
 * Used by RenderSystem; not a public script API.
 */
class Batcher {
public:
    void clear();
    void addRect(float x, float y, float w, float h, const Color &color);
    void addTexturedRect(float x, float y, float w, float h, const Color &color,
                         float u0, float v0, float u1, float v1);
    const std::vector<BatchVertex> &vertices() const { return verts; }
    bool empty() const { return verts.empty(); }

    /** Convert stored logical Y-down positions into Vulkan NDC in-place
     *  ((-1,-1)=top-left, (+1,+1)=bottom-right). */
    void toNDC(int logicalW, int logicalH);

private:
    std::vector<BatchVertex> verts;
};

}  // namespace eve::graphics
