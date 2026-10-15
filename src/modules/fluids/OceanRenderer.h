#pragma once

#include "common/Export.h"

#include "fluids/OceanGpuSpectrum.h"
#include "fluids/OceanMesh.h"

#include <memory>

namespace eve::graphics {
class Graphics;
}
namespace eve::fluids {
class OceanWake;
class OceanHullMaskFrame;
class OceanHullMaskPublication;
class OceanGpuRenderFrameBuilder;
class OceanRendererBuilder;
/** @brief Explicit projection backend; GPU mode never silently substitutes CPU vertex uploads. */
enum class OceanRenderBackend { CpuReference, GpuResident };
/**
 * @brief Graphics projection of an ocean; never owns or advances simulation state.
 * Render-thread only, non-reentrant, no callbacks. Graphics owns GPU resources; a weak lifetime
 * token makes provider-first destruction safe. Destroying this object first releases its resources.
 * Do not retain frame builders across this renderer or water-owner mutation/destruction.
 */
class OceanRenderer {
public:
    EVENGINE_API_DOMAINS ~OceanRenderer();
    OceanRenderer(const OceanRenderer&)            = delete;
    OceanRenderer& operator=(const OceanRenderer&) = delete;
    /** @brief Prepare a cheap borrowed CPU projection builder; set center/origin before building. */
    [[nodiscard]] EVENGINE_API_DOMAINS OceanMeshFrameBuilder frameBuilder(const OceanWaterView& water) const;
    /** @brief Prepare a borrowed GPU publication; owners must remain alive and unchanged through publish(). */
    [[nodiscard]] EVENGINE_API_DOMAINS OceanGpuRenderFrameBuilder gpuFrameBuilder(const OceanGpuSpectrum&   spectrum,
                                                                                  const OceanGpuFrameToken& token);
    /**
     * @brief Publish a prepared owning vertex frame; input is borrowed only for this call.
     * @return Success, StaleHandle, or validation/upload diagnostic; invalid input preserves the old frame.
     * @cost Linear vertex upload with backend synchronization; once per render frame on the CPU reference path.
     */
    [[nodiscard]] EVENGINE_API_DOMAINS Result<void> publish(const OceanMeshFrame& frame);
    /** @brief Borrow a mask frame for synchronous upload; keep frame and renderer alive until publish returns. */
    [[nodiscard]] EVENGINE_API_DOMAINS OceanHullMaskPublication hullMaskBuilder(const OceanHullMaskFrame& frame);
    /** @brief Draw the published frame in the current 3D pass; camera/scene must use its render origin.
     * @return Success, StaleHandle, or Conflict when no frame has been published.
     */
    [[nodiscard]] EVENGINE_API_DOMAINS Result<void> draw();

private:
    friend class OceanHullMaskPublication;
    friend class OceanRendererBuilder;
    friend class OceanGpuRenderFrameBuilder;
    OceanRenderer();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/** @brief Prepare renderer quality; borrows Graphics only until build returns. */
class OceanRendererBuilder {
public:
    /** @brief Select the live graphics provider on its owner thread. */
    explicit OceanRendererBuilder(graphics::Graphics& graphics) : graphics_(graphics) {}
    OceanMeshSettings  mesh;
    OceanRenderBackend backend = OceanRenderBackend::CpuReference;
    /** @brief Allocate shader and fixed topology, publishing only a complete renderer.
     * @return Owning renderer or diagnostic; no callbacks. GPU resources follow Graphics lifetime.
     * @cost Shader/pipeline creation plus O(segments * rings) upload; amortize across renderer lifetime.
     */
    [[nodiscard]] EVENGINE_API_DOMAINS Result<std::unique_ptr<OceanRenderer>> build() const;

private:
    graphics::Graphics& graphics_;
};
/** @brief Borrowed GPU rendering publication, with optional synchronous local wake upload.
 * Does not retain inputs after publish. Do not retain this builder across owner mutation/destruction.
 */
class OceanGpuRenderFrameBuilder {
public:
    glm::dvec2 center{0.0};
    glm::dvec3 origin{0.0};
    /** @brief Borrow wake until publish returns; nullptr explicitly renders the open sea without a local wake. */
    const OceanWake* wake = nullptr;
    /** @brief Copy the completed resident atlas and optional wake to inactive textures, then publish atomically.
     * @cost O(atlas texels) GPU copy and O(wake cells) CPU upload, no spectral readback or per-vertex CPU work.
     * @return Success or validation/backend diagnostic; failure preserves the previous visible frame.
     * @thread Render-thread-only, non-reentrant, no callbacks; copy completes before the source is released.
     */
    [[nodiscard]] EVENGINE_API_DOMAINS Result<void> publish() const;

private:
    friend class OceanRenderer;
    OceanGpuRenderFrameBuilder(OceanRenderer& renderer, const OceanGpuSpectrum& spectrum,
                               const OceanGpuFrameToken& token)
        : renderer_(renderer), spectrum_(spectrum), token_(token) {}
    OceanRenderer&          renderer_;
    const OceanGpuSpectrum& spectrum_;
    OceanGpuFrameToken      token_;
};
/** @brief Borrowed mask upload; no frame or vessel pointer is retained after publication. */
class OceanHullMaskPublication {
public:
    /** @brief Upload into the inactive texture and publish only on success; empty clears the mask.
     * @return Success or provider/upload diagnostic. Invalidated Graphics is never dereferenced.
     * @cost Bounded 4 by 545 RGBA8 upload with backend synchronization, once per changed vessel pose.
     * @thread Render-thread-only, non-reentrant; no callbacks. Draw requires matching mask/ocean origins.
     */
    [[nodiscard]] EVENGINE_API_DOMAINS Result<void> publish() const;

private:
    friend class OceanRenderer;
    OceanHullMaskPublication(OceanRenderer& renderer, const OceanHullMaskFrame& frame)
        : renderer_(renderer), frame_(frame) {}
    OceanRenderer&            renderer_;
    const OceanHullMaskFrame& frame_;
};
}  // namespace eve::fluids
