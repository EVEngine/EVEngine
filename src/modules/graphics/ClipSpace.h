#pragma once

#include <glm/gtc/matrix_transform.hpp>

namespace eve {
namespace graphics {

/**
 * @brief Right-handed, zero-to-one depth perspective for Vulkan swapchains.
 *
 * GLM's perspectiveRH_ZO matches Vulkan's Z range but keeps OpenGL's Y-up clip
 * space. Vulkan NDC has Y pointing down (same as our 2D Batcher), so without
 * flipping Y, world +Y appears at the bottom of the framebuffer — models look
 * upside-down. Negating proj[1][1] maps world +Y to screen up.
 *
 * Side effect: the Y flip reverses screen-space winding, so 3D pipelines that
 * use this projection must declare frontFace = Clockwise (mesh winding stays
 * CCW in RH Y-up object space).
 */
inline glm::mat4 perspectiveVulkanRH_ZO(float fovyRad, float aspect, float zNear, float zFar) {
    glm::mat4 p = glm::perspectiveRH_ZO(fovyRad, aspect, zNear, zFar);
    p[1][1] *= -1.f;
    return p;
}

/** Matching Y-flip for directional shadow ortho so clip space matches the
 *  camera (and mesh pipelines' Clockwise frontFace). */
inline glm::mat4 orthoVulkanRH_ZO(float left, float right, float bottom, float top, float zNear,
                                  float zFar) {
    glm::mat4 p = glm::orthoRH_ZO(left, right, bottom, top, zNear, zFar);
    p[1][1] *= -1.f;
    return p;
}

}  // namespace graphics
}  // namespace eve
