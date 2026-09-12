#include "action/ActionNotifyRegistry.h"
#include "action/ActionSpatialBlock.h"
#include "common/Capability.h"
#include "common/ParticlesQuery.h"
#include "particles/ParticleEmitter.h"
#include "particles/ParticleEffect.h"
#include "particles/Particles.h"

#include <map>
#include <memory>
#include <string>
#include <utility>

namespace eve::particles {
namespace {

class ParticlesQueryImpl final : public eve::IParticlesQuery {
public:
    int emitterCount() override {
        auto *p = eve::ModuleManager::getInstance<Particles>("Particles");
        return p ? p->getEmitterCount() : 0;
    }

    bool createEmitter(int bufferSize, float x, float y, const std::string &preset, int count,
                       float *outX, float *outY, int *outCount) override {
        auto *p = eve::ModuleManager::getInstance<Particles>("Particles");
        if (!p) return false;
        auto *em = p->newEmitter(bufferSize > 0 ? bufferSize : 1000);
        if (!em) return false;
        em->setPosition(x, y);
        if (!preset.empty()) em->applyPreset(preset);
        em->start();
        em->emit(count);
        if (outX) *outX = em->getX();
        if (outY) *outY = em->getY();
        if (outCount) *outCount = em->getCount();
        return true;
    }
};

using ActiveKey = std::pair<eve::action::ActionExecutionId, std::string>;

template <typename T>
eve::Result<T> actionFailure(eve::DiagnosticCode code, std::string message, std::string path) {
    return eve::Result<T>::failure(eve::Diagnostic::error(code, std::move(message), std::move(path)));
}

class ParticleActionHandler final : public eve::action::IActionNotifyHandler {
public:
    eve::Result<void> handle(const eve::action::ActionTimelineEvent& event,
                             const eve::action::ActionNotifyContext& context) override {
        const ActiveKey key{context.executionId, event.itemId.format()};
        if (event.kind == eve::action::ActionTimelineEventKind::StateExit) {
            const auto found = active_.find(key);
            if (found == active_.end())
                return eve::Result<void>::success(eve::Status::success(eve::StatusCode::NoOp));
            found->second.effect->stop();
            active_.erase(found);
            return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
        }
        if (event.kind != eve::action::ActionTimelineEventKind::StateEnter)
            return actionFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                       "VFX state handler requires enter or exit boundary", "event.kind");
        if (active_.contains(key))
            return actionFailure<void>(eve::DiagnosticCode::Conflict, "VFX state is already active", "itemId");
        auto spatial = eve::action::ActionSpatialBinding::fromPayload(event.payload);
        if (!spatial) return eve::Result<void>::failure(spatial.status());
        auto validated = validateResolvable(spatial.value(), context);
        if (!validated) return validated;
        const auto uri = event.payload.find("uri");
        if (uri == event.payload.end() || !uri->second.getIf<std::string>())
            return actionFailure<void>(eve::DiagnosticCode::InvalidArgument, "VFX URI must be text", "uri");
        auto* particles = eve::ModuleManager::getInstance<Particles>("Particles");
        if (!particles)
            return actionFailure<void>(eve::DiagnosticCode::NotFound, "Particles module is unavailable", "particles");
        std::unique_ptr<ParticleEffect> effect(particles->newEffectFromFile(*uri->second.getIf<std::string>()));
        if (!effect)
            return actionFailure<void>(eve::DiagnosticCode::Failed,
                                       particles->getLastEffectError().empty() ? "VFX asset could not be loaded"
                                                                              : particles->getLastEffectError(),
                                       "uri");
        applyWorldOffsets(*effect, spatial.value());
        effect->start();
        active_.emplace(key, ActiveEffect{std::move(effect), std::move(spatial).takeValue()});
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    eve::Result<void> update(const eve::action::ActionActiveBlock& block,
                             const eve::action::ActionNotifyContext& context) override {
        const auto found = active_.find({context.executionId, block.itemId.format()});
        if (found == active_.end())
            return actionFailure<void>(eve::DiagnosticCode::NotFound, "Active VFX state has no instance", "itemId");
        applyWorldOffsets(*found->second.effect, found->second.spatial);
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    eve::Result<void> sample(const eve::action::ActionActiveBlock& block,
                             const eve::action::ActionNotifyContext&) const override {
        auto spatial = eve::action::ActionSpatialBinding::fromPayload(block.payload);
        if (!spatial) return eve::Result<void>::failure(spatial.status());
        const auto uri = block.payload.find("uri");
        if (uri == block.payload.end() || !uri->second.getIf<std::string>())
            return actionFailure<void>(eve::DiagnosticCode::InvalidArgument, "VFX URI must be text", "uri");
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::NoOp));
    }

private:
    struct ActiveEffect {
        std::unique_ptr<ParticleEffect>       effect;
        eve::action::ActionSpatialBinding spatial;
    };

    static eve::Result<void> validateResolvable(const eve::action::ActionSpatialBinding& spatial,
                                                const eve::action::ActionNotifyContext& context) {
        const bool needsSource = spatial.target == eve::action::ActionSpatialTarget::Source && context.source.has_value();
        const bool needsTarget = spatial.target == eve::action::ActionSpatialTarget::Target && !context.targets.empty();
        if (needsSource || needsTarget)
            return actionFailure<void>(eve::DiagnosticCode::Unsupported,
                                       "Entity or bone anchored VFX requires a spatial resolver", "spatialTarget");
        if (spatial.target == eve::action::ActionSpatialTarget::Target &&
            spatial.targetIndex >= context.targets.size() && !context.targets.empty())
            return actionFailure<void>(eve::DiagnosticCode::NotFound, "VFX target index is unavailable", "targetIndex");
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    static void applyWorldOffsets(ParticleEffect& effect, const eve::action::ActionSpatialBinding& spatial) {
        effect.setPosition(static_cast<float>(spatial.positionOffset.x), static_cast<float>(spatial.positionOffset.y));
        effect.setRotation(static_cast<float>(spatial.rotationOffsetDegrees.z * 0.017453292519943295));
        effect.setScale(static_cast<float>(spatial.scale.x));
    }

    std::map<ActiveKey, ActiveEffect> active_;
};

class ParticleActionProvider final : public eve::action::IActionNotifyProvider {
public:
    eve::Result<void> install(eve::action::ActionNotifyRegistry& registry) override {
        return registry.registerHandler("presentation:vfx-state", std::make_shared<ParticleActionHandler>());
    }
};

}  // namespace

void registerParticlesCapabilities() {
    static ParticlesQueryImpl impl;
    static ParticleActionProvider actionProvider;
    eve::cap::provide<eve::IParticlesQuery>(&impl);
    eve::cap::addListener<eve::action::IActionNotifyProvider>(&actionProvider);
}

}  // namespace eve::particles
