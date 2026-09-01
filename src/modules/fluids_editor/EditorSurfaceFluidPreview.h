#pragma once

#include "fluids_editor/EditorSurfaceFluidTarget.h"
#include "graphics_editor/EditorOffscreenPreview.h"

#include <array>
#include <cstdint>
#include <vector>

namespace eve::editor {

/** @brief One stable droplet seed in material-space triangle coordinates. */
struct SurfaceFluidPreviewSeed {
    std::uint32_t         triangle = 0;
    std::array<double, 3> barycentric{1.0, 0.0, 0.0};
    double                volume = 1.0;
    std::array<double, 3> velocity{0.0, 0.0, 0.0};
    auto                  operator<=>(const SurfaceFluidPreviewSeed&) const = default;
};

/** @brief Complete deterministic input needed to replay a surface-fluid scrub. */
struct SurfaceFluidPreviewRequest {
    StableId                             previewId;
    Revision                             documentRevision = 0;
    int                                  width            = 512;
    int                                  height           = 512;
    double                               seconds          = 0.0;
    double                               fixedStep        = 1.0 / 60.0;
    std::vector<std::array<double, 3>>   positions;
    std::vector<std::uint32_t>           indices;
    std::vector<std::array<double, 2>>   uvs;
    std::vector<SurfaceFluidPreviewSeed> seeds;
    std::size_t                          maximumVertices = 1000000;
    std::size_t                          maximumDroplets = 100000;
    std::size_t                          maximumSteps    = 1000000;
};

/** @brief Renderer-neutral world-space droplet cap produced by one scrub replay. */
struct SurfaceFluidPreviewDroplet {
    std::uint64_t         id = 0;
    std::array<double, 3> position{};
    std::array<double, 3> normal{};
    std::array<double, 3> majorAxis{};
    std::array<double, 3> minorAxis{};
    double                capHeight                                            = 0.0;
    double                wetness                                              = 0.0;
    auto                  operator<=>(const SurfaceFluidPreviewDroplet&) const = default;
};

/** @brief Revision-bound result containing droplet geometry and per-vertex wetness. */
struct SurfaceFluidPreviewSnapshot {
    EditorStatus                            status           = EditorStatus::Failed;
    Revision                                documentRevision = 0;
    double                                  simulatedSeconds = 0.0;
    std::vector<SurfaceFluidPreviewDroplet> droplets;
    std::vector<double>                     vertexWetness;
    std::vector<EditorDiagnostic>           diagnostics;
};

/** @brief Rebuilds an isolated surface simulation for deterministic forward/backward scrub. */
class SurfaceFluidPreviewService {
public:
    /** @brief Validate, replay and capture a renderer-neutral surface-fluid frame. */
    SurfaceFluidPreviewSnapshot build(const SurfaceFluidTarget&         target,
                                      const SurfaceFluidPreviewRequest& request) const;
};

/** @brief Narrow host boundary for drawing a validated fluid preview snapshot. */
class ISurfaceFluidPreviewRenderer {
public:
    virtual ~ISurfaceFluidPreviewRenderer() = default;
    /** @brief Draw droplets and wetness using the host's chosen 2D or 3D presentation. */
    virtual EditorResult<void> draw(const SurfaceFluidPreviewSnapshot& snapshot) = 0;
};

/** @brief Builds and rasterizes surface-fluid scrub frames through shared Canvas readback. */
class SurfaceFluidOffscreenPreviewService {
public:
    SurfaceFluidOffscreenPreviewService(GraphicsOffscreenPreviewService* previews,
                                        ISurfaceFluidPreviewRenderer*    renderer)
        : previews_(previews), renderer_(renderer) {}
    /** @brief Replay, validate and render one revision-bound scrub frame. */
    EditorResult<OffscreenPreviewArtifact> render(const SurfaceFluidTarget&         target,
                                                  const SurfaceFluidPreviewRequest& request) const;

private:
    GraphicsOffscreenPreviewService* previews_ = nullptr;
    ISurfaceFluidPreviewRenderer*    renderer_ = nullptr;
};

}  // namespace eve::editor
