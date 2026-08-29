#include "editor/EditorParticleOffscreen.h"

namespace eve::editor {

EditorResult<OffscreenPreviewArtifact> ParticleOffscreenPreviewService::render(
    const ParticleOffscreenPreviewRequest& request) const {
    if (!previews_ || (!draw_ && !presenter_) || request.previewId.empty())
        return EditorResult<OffscreenPreviewArtifact>::error(EditorStatus::Rejected,
            RuleId("editor.particles.offscreen-renderer"), "Particle preview requires identity, service and draw callback");
    ParticleGraphDomain domain;
    ParticleGraphCompileResult compiled = domain.compile(request.graph);
    if (compiled.status != EditorStatus::Applied) {
        EditorResult<OffscreenPreviewArtifact> failed; failed.status = compiled.status;
        failed.diagnostics = compiled.diagnostics; return failed;
    }
    ParticleGraphPreviewResult estimate = domain.preview(request.graph, request.seconds,
        request.fixedStep, request.particleBudget);
    if (estimate.status != EditorStatus::Applied) {
        EditorResult<OffscreenPreviewArtifact> failed; failed.status = estimate.status;
        failed.diagnostics = estimate.diagnostics; return failed;
    }
    OffscreenPreviewRequest offscreen;
    offscreen.previewId = request.previewId; offscreen.sourceRevision = request.graph.revision;
    offscreen.width = request.width; offscreen.height = request.height;
    return previews_->render(offscreen, [&](graphics::Graphics* graphics, graphics::Canvas* canvas) {
        if (presenter_) return presenter_->draw(request, compiled, estimate, graphics, canvas);
        return draw_(compiled, estimate, graphics, canvas);
    });
}

}  // namespace eve::editor
