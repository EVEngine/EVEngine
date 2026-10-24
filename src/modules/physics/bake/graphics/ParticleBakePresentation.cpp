#include "physics/bake/graphics/ParticleBakePresentation.h"
#include <algorithm>
#include <new>
#include <utility>
#include "graphics/DrawItem2D.h"
#include "graphics/Graphics.h"
#include "graphics/RenderSystem.h"
#include "physics/bake/BakedParticlePlayback.h"
namespace eve::physics {
struct ParticleBakePresentation::Impl {
    BakedParticlePlayback             player;
    std::vector<BakedParticleSample>  samples;
    std::vector<graphics::DrawItem2D> items;
    explicit Impl(BakedParticlePlayback value) : player(std::move(value)), samples(player.track().capacity()) {
        items.reserve(samples.size());
    }
};
ParticleBakePresentation::ParticleBakePresentation(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
ParticleBakePresentation::~ParticleBakePresentation() = default;
Result<std::unique_ptr<ParticleBakePresentation>> ParticleBakePresentation::prepare(ParticleTrack track) {
    auto player = BakedParticlePlayback::prepare(std::move(track));
    if (!player) return Result<std::unique_ptr<ParticleBakePresentation>>::failure(player.status());
    try {
        return Result<std::unique_ptr<ParticleBakePresentation>>::success(std::unique_ptr<ParticleBakePresentation>(
            new ParticleBakePresentation(std::make_unique<Impl>(std::move(player.value())))));
    } catch (const std::bad_alloc&) {
        return Result<std::unique_ptr<ParticleBakePresentation>>::failure(Diagnostic::error(
            DiagnosticCode::Failed, "particle presentation allocation failed", "particleBakePresentation"));
    }
}
BakedParticlePlayback& ParticleBakePresentation::player() noexcept { return impl_->player; }
Result<std::size_t> ParticleBakePresentation::draw(graphics::Graphics& graphics, const graphics::DrawItem2D& material) {
    return drawImpl(graphics, &material, {});
}
Result<std::size_t> ParticleBakePresentation::draw(graphics::Graphics&                   graphics,
                                                   std::span<const ParticleBakeMaterial> materials) {
    if (materials.size() > 65536)
        return Result<std::size_t>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "particle material budget exceeded", "particleBakePresentation"));
    for (std::size_t i = 0; i < materials.size(); ++i)
        if (materials[i].emitterSession.isNil() ||
            (i && !(materials[i - 1].emitterSession < materials[i].emitterSession)))
            return Result<std::size_t>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "particle materials must have sorted unique session identities",
                "particleBakePresentation"));
    return drawImpl(graphics, nullptr, materials);
}
Result<std::size_t> ParticleBakePresentation::drawImpl(graphics::Graphics&                   graphics,
                                                       const graphics::DrawItem2D*           uniformMaterial,
                                                       std::span<const ParticleBakeMaterial> materials) {
    auto sampled = impl_->player.sample(impl_->samples);
    if (!sampled) return Result<std::size_t>::failure(sampled.status());
    const auto count = sampled.value();
    // Clear borrowed resource references even if the renderer throws; no material survives this call.
    struct Clear {
        std::vector<graphics::DrawItem2D>& items;
        ~Clear() { items.clear(); }
    } clear{impl_->items};
    bool usedCanvas = false;
    for (std::size_t i = 0; i < count; ++i) {
        const auto&                 pose     = impl_->samples[i].pose;
        const graphics::DrawItem2D* selected = uniformMaterial;
        if (!selected) {
            const auto id    = impl_->samples[i].emitter;
            auto       found = std::lower_bound(
                materials.begin(), materials.end(), id,
                [](const ParticleBakeMaterial& binding, PersistentId value) { return binding.emitterSession < value; });
            if (found == materials.end() || found->emitterSession != id)
                return Result<std::size_t>::failure(
                    Diagnostic::error(DiagnosticCode::NotFound, "visible particle emitter has no material binding",
                                      "particleBakePresentation"));
            selected = &found->material;
        }
        auto item      = *selected;
        usedCanvas     = usedCanvas || item.canvas != nullptr;
        item.x         = pose.x;
        item.y         = pose.y;
        item.w         = pose.w;
        item.h         = pose.h;
        item.rotation  = pose.rotation;
        item.color     = {pose.r, pose.g, pose.b, pose.a};
        item.u0        = pose.u0;
        item.v0        = pose.v0;
        item.u1        = pose.u1;
        item.v1        = pose.v1;
        item.hasUV     = true;
        item.rotatedUV = false;
        item.anchorX = item.anchorY = 0.5f;
        item.flipX = item.flipY = false;
        item.order              = static_cast<int>(i);
        item.hasOrder           = true;
        item.quad               = nullptr;
        impl_->items.push_back(item);
    }
    if (!impl_->items.empty()) graphics::RenderSystem::drawItems(graphics, impl_->items, false);
    if (usedCanvas) graphics.setCanvas();
    return Result<std::size_t>::success(count);
}
}  // namespace eve::physics
