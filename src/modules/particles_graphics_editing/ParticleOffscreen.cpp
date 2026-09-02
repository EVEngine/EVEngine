#include "particles_graphics_editing/ParticleOffscreen.h"

namespace eve::particles_graphics_editing {

EditorResult<OffscreenPreviewArtifact> ParticleOffscreenPreviewService::render(
    const ParticleOffscreenPreviewRequest& request) const {
    if (!previews_ || (!draw_ && !presenter_) || request.previewId.empty())
        return eve::editing::failed<OffscreenPreviewArtifact>(EditorStatus::Rejected,
            RuleId("editor.particles.offscreen-renderer"), "Particle preview requires identity, service and draw callback");
    ParticleGraphDomain domain;
    ParticleGraphCompileResult compiled = domain.compile(request.graph);
    if (compiled.status != EditorStatus::Applied) {
        return EditorResult<OffscreenPreviewArtifact>::failure(
            eve::Status(compiled.status, compiled.diagnostics));
    }
    ParticleGraphPreviewResult estimate = domain.preview(request.graph, request.seconds,
        request.fixedStep, request.particleBudget);
    if (estimate.status != EditorStatus::Applied) {
        return EditorResult<OffscreenPreviewArtifact>::failure(
            eve::Status(estimate.status, estimate.diagnostics));
    }
    OffscreenPreviewRequest offscreen;
    offscreen.previewId = request.previewId; offscreen.sourceRevision = request.graph.revision;
    offscreen.width = request.width; offscreen.height = request.height;
    return previews_->render(offscreen, [&](graphics::Graphics* graphics, graphics::Canvas* canvas) {
        if (presenter_) return presenter_->draw(request, compiled, estimate, graphics, canvas);
        return draw_(compiled, estimate, graphics, canvas);
    });
}

}  // namespace eve::particles_graphics_editing
