#pragma once

#include "editor/EditorOffscreenPreview.h"
#include "editor/EditorParticleGraph.h"

#include <functional>

namespace eve::editor {

struct ParticleOffscreenPreviewRequest;

/** @brief Presentation boundary for an isolated, compiled particle preview. */
class IParticleOffscreenPresenter {
public:
    virtual ~IParticleOffscreenPresenter() = default;
    /** @brief Simulate and draw only the requested graph into the active Canvas. */
    virtual EditorResult<void> draw(const ParticleOffscreenPreviewRequest& request,
                                    const ParticleGraphCompileResult& compiled,
                                    const ParticleGraphPreviewResult& estimate,
                                    graphics::Graphics* graphics,
                                    graphics::Canvas* canvas) = 0;
};

/** @brief Revision-bound particle scrub preview request. */
struct ParticleOffscreenPreviewRequest {
    StableId previewId;
    GraphDocumentData graph;
    int width = 512;
    int height = 512;
    double seconds = 1.0;
    double fixedStep = 1.0 / 60.0;
    int particleBudget = 100000;
};

/** @brief Compiles, budget-checks and rasterizes particle graph previews offscreen. */
class ParticleOffscreenPreviewService {
public:
    using DrawCallback = std::function<EditorResult<void>(const ParticleGraphCompileResult&,
        const ParticleGraphPreviewResult&, graphics::Graphics*, graphics::Canvas*)>;
    ParticleOffscreenPreviewService(GraphicsOffscreenPreviewService* previews, DrawCallback draw)
        : previews_(previews), draw_(std::move(draw)) {}
    ParticleOffscreenPreviewService(GraphicsOffscreenPreviewService* previews,
                                    IParticleOffscreenPresenter* presenter)
        : previews_(previews), presenter_(presenter) {}
    EditorResult<OffscreenPreviewArtifact> render(const ParticleOffscreenPreviewRequest& request) const;
private:
    GraphicsOffscreenPreviewService* previews_ = nullptr;
    DrawCallback draw_;
    IParticleOffscreenPresenter* presenter_ = nullptr;
};

/** @brief Real ParticleEmitter presenter with deterministic stepping and isolated drawing. */
class ParticleEmitterOffscreenPresenter final : public IParticleOffscreenPresenter {
public:
    using TextureResolver = ParticleGraphRuntimeBuilder::TextureResolver;
    explicit ParticleEmitterOffscreenPresenter(TextureResolver textures = {})
        : textures_(std::move(textures)) {}
    EditorResult<void> draw(const ParticleOffscreenPreviewRequest& request,
                            const ParticleGraphCompileResult& compiled,
                            const ParticleGraphPreviewResult& estimate,
                            graphics::Graphics* graphics,
                            graphics::Canvas* canvas) override;
private:
    TextureResolver textures_;
};

}  // namespace eve::editor
