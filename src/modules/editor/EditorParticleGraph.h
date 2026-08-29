#pragma once

#include "editor/EditorGraph.h"

#include <functional>
#include <string>
#include <vector>

namespace eve::editor {

/** @brief Validated particle configuration compiled from a module graph. */
struct ParticleGraphCompileResult {
    EditorStatus status = EditorStatus::Failed;
    Revision documentRevision = 0;
    EditorValue configuration;
    std::vector<EditorDiagnostic> diagnostics;
};

/** @brief Deterministic, revision-tagged particle preview estimate. */
struct ParticleGraphPreviewResult {
    EditorStatus status = EditorStatus::Failed;
    Revision documentRevision = 0;
    int spawnedParticles = 0;
    int peakLiveParticles = 0;
    int droppedParticles = 0;
    double simulatedSeconds = 0.0;
    std::vector<EditorDiagnostic> diagnostics;
};

/** @brief `particles.emitter` graph domain reflecting core emitter modules. */
class ParticleGraphDomain final : public IGraphDomainProvider {
public:
    std::string domain() const override { return "particles.emitter"; }
    GraphConnectionDecision canConnect(const GraphPinRecord& from,
                                       const GraphPinRecord& to) const override;
    /** @brief Construct a node of type emission, motion, collision, renderer, or output. */
    EditorResult<GraphNodeRecord> makeNode(const GraphNodeId& id, const std::string& type) const;
    /** @brief Compile one acyclic module chain into a runtime-neutral configuration object. */
    ParticleGraphCompileResult compile(const GraphDocumentData& graph) const;
    /** @brief Estimate a deterministic preview and enforce particle/frame budgets. */
    ParticleGraphPreviewResult preview(const GraphDocumentData& graph, double seconds,
                                       double fixedStep = 1.0 / 60.0,
                                       int particleBudget = 100000,
                                       int spawnBudgetPerFrame = 0) const;
};

}  // namespace eve::editor

namespace eve::particles {
class ParticleEmitter;
}
namespace eve::graphics {
class Texture;
}

namespace eve::editor {

/** @brief Optional bridge applying a compiled graph to a real ParticleEmitter. */
class ParticleGraphRuntimeBuilder {
public:
    using TextureResolver = std::function<graphics::Texture*(const std::string& asset)>;
    EditorResult<void> apply(const GraphDocumentData& graph,
                             particles::ParticleEmitter* emitter,
                             const TextureResolver& textures = {}) const;
};

}  // namespace eve::editor
