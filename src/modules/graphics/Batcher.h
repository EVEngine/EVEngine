#pragma once

#include "graphics/Canvas.h"
#include <vector>
#include <glm/glm.hpp>

namespace eve::graphics {

class Texture;

/** @brief CPU-side vertex for 2D batching (logical or NDC depending on stage). */
struct BatchVertex {
    glm::vec2 pos;
    glm::vec4 color;
    glm::vec2 uv;
};

/**
 * @brief Accumulates solid / textured quads in logical (Y-down) coordinates.
 * Used by RenderSystem; not a public script API.
 */
class Batcher {
public:
    void clear();
    void addRect(float x, float y, float w, float h, const Color &color);
    /** @brief Adds one arbitrary solid triangle in logical Canvas coordinates. */
    void addTriangle(glm::vec2 a, glm::vec2 b, glm::vec2 c, const Color &color);
    /** @brief Adds one arbitrary solid triangle with independent vertex colors. */
    void addTriangle(glm::vec2 a, glm::vec2 b, glm::vec2 c, const Color &colorA, const Color &colorB,
                     const Color &colorC);
    void addRectRotated(float cx, float cy, float w, float h, float degrees, const Color &color);
    void addTexturedRect(float x, float y, float w, float h, const Color &color,
                         float u0, float v0, float u1, float v1,
                         bool rotatedUV = false);
    /** @brief Textured quad rotated `degrees` (clockwise, screen Y-down) around (cx, cy). */
    void addTexturedRectRotated(float cx, float cy, float w, float h, float degrees,
                                const Color &color, float u0, float v0, float u1, float v1,
                                bool rotatedUV = false);
    const std::vector<BatchVertex> &vertices() const { return verts; }
    bool empty() const { return verts.empty(); }

    /** Convert stored logical Y-down positions into Vulkan NDC in-place
     *  ((-1,-1)=top-left, (+1,+1)=bottom-right). */
    void toNDC(int logicalW, int logicalH);

private:
    std::vector<BatchVertex> verts;
};

}  // namespace eve::graphics
