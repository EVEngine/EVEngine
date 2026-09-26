#pragma once

#include "common/Export.h"
#include "common/Result.h"
#include "graphics/RayTracingCaps.h"

#include <glm/mat4x4.hpp>

namespace eve::graphics {

class Canvas;
class Graphics;
class Texture;

/**
 * @brief Optional hardware ray-tracing backend (Vulkan KHR RT).
 *
 * Provided by the `graphics_raytracing` module when linked. Consumers must
 * tolerate `eve::cap::query<IRayTracing>() == nullptr` (module trimmed) and
 * `isAvailable() == false` (GPU lacks the extensions) as supported states.
 *
 * @lifetime The provider is registered at static init and outlives modules.
 * @thread Render thread only.
 */
class EVENGINE_API_BACKENDS_INLINE IRayTracing {
public:
    static constexpr const char* capabilityName = "IRayTracing";

    virtual ~IRayTracing() = default;

    /** @brief True when the active Graphics device enabled the KHR RT path. */
    virtual bool isAvailable() const = 0;

    /** @brief Borrowed device caps; valid while Graphics remains initialized. */
    virtual RayTracingCaps caps() const = 0;

    /**
     * @brief Trace mirror-style reflections into `dest` (RGBA16F, A = hit mask).
     * @param gfx Active Graphics backend (Vulkan).
     * @param sceneColor Lit scene color to use as a hit fallback sample.
     * @param hwDepth Hardware depth (D32) matching the scene projection.
     * @param worldNormal World-space normals (RGB).
     * @param dest Output canvas; size drives the launch dimensions.
     * @param invViewProj Inverse view-projection (RH + ZO) for primary rays.
     * @param eyeWorld Camera eye in world space.
     * @return Unsupported when RT is unavailable; Failed on GPU errors.
     * @ownership All pointers are borrowed for the call; none are retained.
     */
    [[nodiscard]] virtual Result<void> applyReflections(Graphics* gfx, Texture* sceneColor, Texture* hwDepth,
                                                        Texture* worldNormal, Canvas* dest,
                                                        const glm::mat4& invViewProj, const glm::vec3& eyeWorld) = 0;

    /**
     * @brief Rebuild the TLAS from meshes previously registered with the scene.
     * @return Unsupported when RT is unavailable; Failed on build errors.
     */
    [[nodiscard]] virtual Result<void> rebuildScene() = 0;

    /**
     * @brief Register a triangle mesh instance for ray tracing.
     * @param positionsXYZ Packed xyz positions (vertexCount * 3).
     * @param indices Triangle indices (indexCount, multiple of 3).
     * @param transform Object-to-world transform for the TLAS instance.
     * @return Instance id on success, or Unsupported / InvalidArgument.
     * @ownership Input spans are borrowed only during this call.
     */
    [[nodiscard]] virtual Result<uint32_t> addTriangleMesh(const float* positionsXYZ, int vertexCount,
                                                           const uint32_t* indices, int indexCount,
                                                           const glm::mat4& transform) = 0;

    /** @brief Drop every registered mesh and acceleration structure. */
    virtual void clearScene() = 0;
};

}  // namespace eve::graphics
