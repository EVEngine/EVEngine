#include "common/Capability.h"
#include "common/ModuleManager.h"
#include "particles/ParticleEffect.h"
#include "particles/Particles.h"
#include "particles/ParticlesCapabilities.h"
#include "stylize/AttackVfxLayerExecutor.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

namespace eve::particles {
namespace {

class ParticlesAttackVfxExecutor final : public eve::stylize::IAttackVfxLayerExecutor {
public:
    eve::stylize::AttackVfxLayerRole role() const noexcept override {
        return eve::stylize::AttackVfxLayerRole::Particles;
    }

    eve::Result<eve::stylize::AttackVfxLayerHandle> start(
        const eve::stylize::AttackVfxLayerStartRequest& request) override {
        if (!request.layer)
            return eve::Result<eve::stylize::AttackVfxLayerHandle>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "AttackVfx particles layer is null", "layer"));
        auto* particles = eve::ModuleManager::getInstance<Particles>("Particles");
        if (!particles)
            return eve::Result<eve::stylize::AttackVfxLayerHandle>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::NotFound, "Particles module is unavailable", "particles"));

        std::unique_ptr<ParticleEffect> effect;
        const std::string& uri = request.layer->uri;
        if (uri.rfind("json:", 0) == 0) {
            effect.reset(particles->newEffectFromText(uri.substr(5)));
        } else {
            effect.reset(particles->newEffectFromFile(uri));
        }
        if (!effect) {
            const auto error = particles->getLastEffectError();
            return eve::Result<eve::stylize::AttackVfxLayerHandle>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Failed, error.empty() ? "particle effect could not be loaded" : error, "uri"));
        }

        if (request.playRequest) {
            // Phase 2: 2D particle transform uses source id as a stable x offset marker when present.
            effect->setPosition(static_cast<float>(request.playRequest->sourceId),
                                static_cast<float>(request.playRequest->targetId));
        }
        if (const auto intensity = request.layer->floatParams.find("intensity");
            intensity != request.layer->floatParams.end()) {
            effect->setFloatParameter("intensity", intensity->second);
        }
        effect->start();

        const auto id = ++nextId_;
        effects_.emplace(id, std::move(effect));
        return eve::Result<eve::stylize::AttackVfxLayerHandle>::success(
            eve::stylize::AttackVfxLayerHandle{id});
    }

    eve::Result<void> update(eve::stylize::AttackVfxLayerHandle handle, double,
                             const eve::stylize::AttackVfxLayerStartRequest&) override {
        if (!effects_.contains(handle.id))
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::StaleHandle, "particle AttackVfx layer handle is stale", "handle"));
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::NoOp));
    }

    eve::Result<void> stop(eve::stylize::AttackVfxLayerHandle handle,
                           eve::stylize::AttackVfxStopBehavior behavior) override {
        const auto found = effects_.find(handle.id);
        if (found == effects_.end())
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::StaleHandle, "particle AttackVfx layer handle is stale", "handle"));
        found->second->stop();
        if (behavior == eve::stylize::AttackVfxStopBehavior::ClearImmediately) found->second->reset();
        effects_.erase(found);
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

private:
    std::uint64_t nextId_ = 1;
    std::unordered_map<std::uint64_t, std::unique_ptr<ParticleEffect>> effects_;
};

ParticlesAttackVfxExecutor& executor() {
    static ParticlesAttackVfxExecutor instance;
    return instance;
}

bool gRegistered = false;

}  // namespace

void registerParticlesAttackVfxExecutor() {
    if (gRegistered) return;
    eve::cap::addListener<eve::stylize::IAttackVfxLayerExecutor>(&executor());
    gRegistered = true;
}

void unregisterParticlesAttackVfxExecutor() {
    if (!gRegistered) return;
    eve::cap::removeListener<eve::stylize::IAttackVfxLayerExecutor>(&executor());
    gRegistered = false;
}

}  // namespace eve::particles
