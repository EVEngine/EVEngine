#include "common/Capability.h"
#include "common/Module.h"
#include "particles/ParticleEffect.h"
#include "particles/ParticleEmitter.h"
#include "particles/Particles.h"
#include "particles/ParticlesCapabilities.h"
#include "stylize/AttackVfxLayerExecutor.h"

#include <ECS.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

namespace eve::particles {
namespace {

float layerParam(const eve::stylize::AttackVfxLayerStartRequest& request, const char* key,
                 float fallback) {
    if (request.layer) {
        const auto found = request.layer->floatParams.find(key);
        if (found != request.layer->floatParams.end()) return found->second;
    }
    return fallback;
}

bool effectHasLiveParticles(const ParticleEffect& effect) {
    const int count = effect.getEmitterCount();
    for (int i = 0; i < count; ++i) {
        auto* emitter = effect.getEmitter(i);
        if (!emitter) continue;
        if (emitter->getCount() > 0) return true;
        if (!emitter->isStopped()) return true;
    }
    return false;
}

class ParticlesAttackVfxExecutor final : public eve::stylize::IAttackVfxLayerExecutor {
public:
    /**
     * @brief Executor is a function-local static constructed during capability
     * register — historically before any ParticleEmitter touched
     * ecs::engine_default_table(). On abrupt exit() (e.g. X11 fatal IO), atexit
     * can destroy the Table first; destroying live ParticleEffects would then
     * DestroyEntity into freed ComponentManagers (SIGSEGV). Abandon pointers
     * instead when tearing down via static destruction; process is exiting.
     * registerParticlesAttackVfxExecutor() now touches the Table first so the
     * common atexit order is executor-then-Table; abandon remains a guard.
     */
    ~ParticlesAttackVfxExecutor() { abandonEffects(); }

    /** @brief Orderly shutdown while ECS is still alive (~Particles). */
    void clearEffects() { effects_.clear(); }

    /** @brief Drop ParticleEffect ownership without DestroyEntity (ECS may be gone). */
    void abandonEffects() noexcept {
        for (auto& entry : effects_) {
            if (entry.second.effect) (void)entry.second.effect.release();
        }
        effects_.clear();
    }

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

        // Placement uses authored floatParams; entity ids are not world coordinates.
        effect->setPosition(layerParam(request, "x", 0.f), layerParam(request, "y", 0.f));
        if (const auto intensity = request.layer->floatParams.find("intensity");
            intensity != request.layer->floatParams.end()) {
            effect->setFloatParameter("intensity", intensity->second);
        }
        effect->start();

        Live owned;
        owned.effect = std::move(effect);
        const auto id = ++nextId_;
        effects_.emplace(id, std::move(owned));
        return eve::Result<eve::stylize::AttackVfxLayerHandle>::success(
            eve::stylize::AttackVfxLayerHandle{id});
    }

    eve::Result<void> update(eve::stylize::AttackVfxLayerHandle handle, double,
                             const eve::stylize::AttackVfxLayerStartRequest&) override {
        const auto found = effects_.find(handle.id);
        if (found == effects_.end())
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::StaleHandle, "particle AttackVfx layer handle is stale", "handle"));
        if (found->second.draining) {
            if (!found->second.effect || !effectHasLiveParticles(*found->second.effect)) {
                effects_.erase(found);
                return eve::Result<void>::success(eve::Status::success(eve::StatusCode::NoOp));
            }
        }
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    eve::Result<void> stop(eve::stylize::AttackVfxLayerHandle handle,
                           eve::stylize::AttackVfxStopBehavior behavior) override {
        const auto found = effects_.find(handle.id);
        if (found == effects_.end())
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::StaleHandle, "particle AttackVfx layer handle is stale", "handle"));
        if (found->second.effect) found->second.effect->stop();
        if (behavior == eve::stylize::AttackVfxStopBehavior::ClearImmediately) {
            if (found->second.effect) found->second.effect->reset();
            effects_.erase(found);
            return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
        }
        // StopEmitting keeps the effect so residual particles can age out.
        found->second.draining = true;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

private:
    struct Live {
        std::unique_ptr<ParticleEffect> effect;
        bool                            draining = false;
    };

    std::uint64_t nextId_ = 1;
    std::unordered_map<std::uint64_t, Live> effects_;
};

ParticlesAttackVfxExecutor& executor() {
    static ParticlesAttackVfxExecutor instance;
    return instance;
}

bool gRegistered = false;

}  // namespace

void registerParticlesAttackVfxExecutor() {
    if (gRegistered) return;
    // Construct the process ECS Table before this function-local static so
    // atexit destroys the executor (and any leftover effects) while managers
    // are still alive. register() runs before any emitter is created.
    (void)ecs::default_table();
    eve::cap::addListener<eve::stylize::IAttackVfxLayerExecutor>(&executor());
    gRegistered = true;
}

void unregisterParticlesAttackVfxExecutor() {
    if (!gRegistered) return;
    eve::cap::removeListener<eve::stylize::IAttackVfxLayerExecutor>(&executor());
    // Destroy live ParticleEffects while ECS ComponentManagers still exist.
    executor().clearEffects();
    gRegistered = false;
}

}  // namespace eve::particles
