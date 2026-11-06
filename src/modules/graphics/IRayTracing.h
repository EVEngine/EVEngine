#pragma once

#include "common/Export.h"
#include "common/Result.h"
#include "graphics/PathTracing.h"
#include "graphics/RayTracingCaps.h"

// Incomplete glm types are enough for const-reference parameters.
#include <glm/fwd.hpp>

namespace eve::graphics {

class Canvas;
class Graphics;
class Mesh;
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

    /** @brief I ray tracing. */
    virtual ~IRayTracing() = default;

    /** @brief True when the active Graphics device enabled the KHR RT path. */
    virtual bool isAvailable() const = 0;

    /** @brief Borrowed device caps; valid while Graphics remains initialized. */
    virtual RayTracingCaps caps() const = 0;

    /** @brief Prepare an independently owned path-tracing scene and publish it atomically.
     * @param gfx Initialized graphics device; borrowed until device detachment.
     * @param scene Borrowed geometry, materials, textures, instances and lights; copied during preparation.
     * @return Current token for identical values, new token for changed values, or a failure preserving the prior
     * scene.
     * @cost CPU validation/comparison proportional to input; unchanged values reuse GPU resources.
     * Geometry/material/light/texture changes upload/build; native textures copy GPU bytes proportional to texels.
     * Source content identities amortize unchanged native captures; instance-only changes reuse BLAS and update TLAS.
     * @thread Render thread, outside frame recording; no callbacks or reentrancy.
     * @lifetime Successful preparation of changed inputs invalidates prior tokens/history. Source edits/releases
     * alone leave an existing snapshot intact. Device detachment releases the owned scene.
     */
    [[nodiscard]] virtual Result<PathSceneToken> preparePathScene(Graphics* gfx, const PathSceneDescription& scene) = 0;

    /** @brief Replace instances of a prepared geometry set without rebuilding its BLAS.
     * @param token Current prepared-scene token.
     * @param instances Complete replacement instance list; geometry indices refer to the prepared geometry set.
     * @return New scene token on success; failure preserves the prior token and scene.
     * @ownership Input is borrowed for this call; geometry/material buffers remain provider-owned.
     * @lifetime Success invalidates the old token and accumulation. Detachment invalidates all tokens.
     * @thread Render thread, outside frame recording; no callbacks or reentrancy.
     * @cost TLAS build and emissive-instance upload proportional to instances/emissive triangles;
     * amortize across frames with unchanged instances. Geometry/material/light replacement requires preparation.
     */
    [[nodiscard]] virtual Result<PathSceneToken> updatePathInstances(PathSceneToken                token,
                                                                     std::span<const PathInstance> instances) = 0;

    /** @brief Render a prepared path-tracing sample batch into an HDR canvas.
     * @param token Token returned by this provider's latest successful preparation.
     * @param dest Borrowed RGBA16F output canvas; must outlive the call.
     * @param view Camera, environment and deterministic sampling/accumulation settings.
     * @return InvalidArgument for stale tokens or invalid inputs; Failed for GPU errors.
     * @cost GPU work scales with pixels, samples and bounces; currently waits for GPU completion per batch.
     * @thread Render thread, outside frame recording; no callbacks or reentrancy.
     * @ownership Provider owns accumulation; destination remains caller-owned.
     */
    [[nodiscard]] virtual Result<void> renderPaths(PathSceneToken token, Canvas* dest, const PathTraceView& view) = 0;

    /** @brief Prepare provider-owned output images for normal-frame path tracing.
     * @ownership Provider owns output images and sampled textures; no caller storage is retained.
     * @thread Render thread, outside frame recording; no callbacks.
     * @cost Allocates images proportional to pixels on resize; equal extents reuse the current output token.
     * @return Output identity, invalidated by resize, full scene replacement or device detach.
     */
    [[nodiscard]] virtual Result<PathFrameToken> preparePathFrame(PathSceneToken scene, uint32_t width,
                                                                  uint32_t height) = 0;
    /** @brief End the scene pass and record one path batch as this normal frame's HDR resolve source.
     * @ownership Returned texture is borrowed from the provider until output invalidation; use it in this frame only.
     * @thread Render thread; once per present frame, no callbacks or reentrancy.
     * @cost Records pixel/sample/bounce-proportional GPU work; does not submit or wait for this batch.
     * @return Sampleable output for same-frame composition, or a structured failure. Success is recording, not
     * completion.
     * @details The caller submits the normal frame. Reset history if a previous recording was not submitted.
     * Subsequent 3D raster draws are not permitted; 2D UI overlays remain supported.
     */
    [[nodiscard]] virtual Result<Texture*> recordPathFrame(PathSceneToken scene, PathFrameToken output,
                                                           const PathTraceView& view) = 0;

    /**
     * @brief Trace mirror-style reflections into `dest` (RGBA16F, A = hit mask).
     * @param gfx Active Graphics backend (Vulkan).
     * @param sceneColor Lit scene color to sample on a hit when no material is bound.
     * @param hwDepth Hardware depth (D32) matching the scene projection.
     * @param worldNormal World-space normals (RGB).
     * @param dest Output canvas; size drives the launch dimensions.
     * @param invViewProj Inverse view-projection (RH + ZO) for primary rays.
     * @param viewProj View-projection used to reproject hit points into scene color.
     * @param eyeWorld Camera eye in world space.
     * @return Unsupported when RT is unavailable; NoOp when the TLAS is empty;
     *         Failed on GPU errors. Callers must treat NoOp as "no usable output".
     * @ownership All pointers are borrowed for the call; none are retained.
     */
    [[nodiscard]] virtual Result<void> applyReflections(Graphics* gfx, Texture* sceneColor, Texture* hwDepth,
                                                        Texture* worldNormal, Canvas* dest,
                                                        const glm::mat4& invViewProj, const glm::mat4& viewProj,
                                                        const glm::vec3& eyeWorld) = 0;

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

    /**
     * @brief Register a GPU Mesh already uploaded by Graphics.
     * @ownership `mesh` is borrowed for the call; vertex/index bytes are copied.
     * @lifetime `mesh` and its gpuHandle must remain valid through this call.
     */
    [[nodiscard]] virtual Result<uint32_t> addMesh(Mesh* mesh, const glm::mat4& transform) = 0;

    /** @brief Drop every registered mesh and acceleration structure. */
    virtual void clearScene() = 0;

    /**
     * @brief Release device-owned RT resources before Graphics destroys the device.
     * @thread Render thread only; safe when never attached.
     */
    virtual void detachFromGraphics() {}
};

}  // namespace eve::graphics
