#pragma once

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>

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
 * Keep mesh winding outward counter-clockwise in RH Y-up object space. With the
 * engine's positive-height viewport, Vulkan/WebGPU front-face classification in
 * framebuffer coordinates still treats that winding as CounterClockwise.
 */
inline glm::mat4 perspectiveVulkanRH_ZO(float fovyRad, float aspect, float zNear, float zFar) {
    glm::mat4 p = glm::perspectiveRH_ZO(fovyRad, aspect, zNear, zFar);
    p[1][1] *= -1.f;
    return p;
}

/** Matching Y-flip for directional shadow ortho so clip space matches the camera. */
inline glm::mat4 orthoVulkanRH_ZO(float left, float right, float bottom, float top, float zNear,
                                  float zFar) {
    glm::mat4 p = glm::orthoRH_ZO(left, right, bottom, top, zNear, zFar);
    p[1][1] *= -1.f;
    return p;
}

/** @brief Camera projection shared by rendering, picking, and diagnostic passes. */
inline glm::mat4 cameraProjectionVulkanRH_ZO(bool orthographic, float fovyRad,
                                              float orthoHeight, float aspect,
                                              float zNear, float zFar) {
    if (!orthographic) return perspectiveVulkanRH_ZO(fovyRad, aspect, zNear, zFar);
    const float halfH = std::max(0.001f, orthoHeight * 0.5f);
    const float halfW = halfH * std::max(0.001f, aspect);
    return orthoVulkanRH_ZO(-halfW, halfW, -halfH, halfH, zNear, zFar);
}

}  // namespace graphics
}  // namespace eve
