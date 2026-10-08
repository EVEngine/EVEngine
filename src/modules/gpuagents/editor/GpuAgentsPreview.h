#pragma once
#include "common/Export.h"

#include "gpuagents/AgentState.h"
#include "gpuagents/editing/GpuAgentsDocument.h"

#include <cstdint>
#include <vector>

namespace eve::gpuagents_editor {

using gpuagents_editing::EditorDiagnostic;
using gpuagents_editing::EditorStatus;
using gpuagents_editing::GpuAgentsDocumentTarget;
using gpuagents_editing::Revision;

/** @brief Deterministic scrub request for editor preview. */
struct EVENGINE_API_EDITORS GpuAgentsPreviewRequest {
    int           agentCount   = 32;
    double        seconds      = 1.0;
    double        fixedStep    = 1.0 / 60.0;
    double        spawnSpread  = 2.0;
    double        centerX      = 0.0;
    double        centerY      = 1.0;
    double        centerZ      = 0.0;
    std::uint32_t maximumSteps = 100000;
};

/** @brief One agent sample in a preview snapshot. */
struct EVENGINE_API_EDITORS GpuAgentsPreviewAgent {
    float x = 0.f, y = 0.f, z = 0.f;
    float vx = 0.f, vy = 0.f, vz = 0.f;
    float age     = 0.f;
    float custom0 = 0.f;
};

/** @brief Revision-bound preview snapshot with optional life-field energy. */
struct EVENGINE_API_EDITORS GpuAgentsPreviewSnapshot {
    EditorStatus                       status           = EditorStatus::Failed;
    Revision                           documentRevision = 0;
    double                             simulatedSeconds = 0.0;
    std::vector<GpuAgentsPreviewAgent> agents;
    float                              lifeTrailEnergy = 0.f;
    std::vector<EditorDiagnostic>      diagnostics;
};

/**
 * @brief Rebuilds an isolated GPU Agents simulation for deterministic scrub preview.
 * @ownership Service is value-owned; does not retain document/world pointers across calls.
 */
class EVENGINE_API_EDITORS GpuAgentsPreviewService {
public:
    /** @brief Validate, spawn, step and capture a renderer-neutral preview frame. */
    [[nodiscard]] GpuAgentsPreviewSnapshot build(const GpuAgentsDocumentTarget& target,
                                                 const GpuAgentsPreviewRequest& request) const;
};

}  // namespace eve::gpuagents_editor
