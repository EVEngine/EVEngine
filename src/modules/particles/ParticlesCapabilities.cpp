#include "action/ActionNotifyRegistry.h"
#include "action/ActionPreview.h"
#include "action/ActionSpatialBlock.h"
#include "action/ActionVfxBlock.h"
#include "common/Capability.h"
#include "common/EntitySpatialResolver.h"
#include "common/ParticlesQuery.h"
#include "particles/ParticleEmitter.h"
#include "particles/ParticleEffect.h"
#include "particles/Particles.h"

#include <algorithm>
#include <cmath>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

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
            finish(found->second);
            active_.erase(found);
            return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
        }
        const bool instant = event.kind == eve::action::ActionTimelineEventKind::Notify;
        if (!instant && event.kind != eve::action::ActionTimelineEventKind::StateEnter)
            return actionFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                       "VFX state handler requires enter or exit boundary", "event.kind");
        if (!instant && active_.contains(key))
            return actionFailure<void>(eve::DiagnosticCode::Conflict, "VFX state is already active", "itemId");
        const auto shape = instant ? eve::action::ActionVfxShape::Instant : eve::action::ActionVfxShape::State;
        auto binding = eve::action::ActionVfxBinding::fromPayload(event.payload, shape);
        if (!binding) return eve::Result<void>::failure(binding.status());
        auto pose = resolvePose(binding.value().spatial, context);
        if (!pose) return eve::Result<void>::failure(pose.status());
        auto* particles = eve::ModuleManager::getInstance<Particles>("Particles");
        if (!particles)
            return actionFailure<void>(eve::DiagnosticCode::NotFound, "Particles module is unavailable", "particles");
        std::unique_ptr<ParticleEffect> effect(particles->newEffectFromFile(binding.value().uri));
        if (!effect)
            return actionFailure<void>(eve::DiagnosticCode::Failed,
                                       particles->getLastEffectError().empty() ? "VFX asset could not be loaded"
                                                                              : particles->getLastEffectError(),
                                       "uri");
        applyWorldTransform(*effect, binding.value().spatial, pose.value());
        effect->start();
        ActiveEffect owned{std::move(effect), binding.value(), std::move(pose).takeValue()};
        if (instant) {
            auto duration = eve::Duration::fromSeconds(binding.value().lifetimeSeconds);
            if (!duration) return eve::Result<void>::failure(duration.status());
            auto end = context.time.tryAdd(duration.value());
            if (!end) return eve::Result<void>::failure(end.status());
            transients_.push_back({context.executionId, std::move(end).takeValue(), std::move(owned)});
        } else {
            active_.emplace(key, std::move(owned));
        }
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    eve::Result<void> update(const eve::action::ActionActiveBlock& block,
                             const eve::action::ActionNotifyContext& context) override {
        const auto found = active_.find({context.executionId, block.itemId.format()});
        if (found == active_.end())
            return actionFailure<void>(eve::DiagnosticCode::NotFound, "Active VFX state has no instance", "itemId");
        if (found->second.binding.spatial.mode != eve::action::ActionSpatialAttachmentMode::WorldTransformAtStart) {
            auto pose = resolvePose(found->second.binding.spatial, context);
            if (!pose) return eve::Result<void>::failure(pose.status());
            if (found->second.binding.spatial.mode == eve::action::ActionSpatialAttachmentMode::FollowPositionOnly) {
                found->second.pose.positionX = pose.value().positionX;
                found->second.pose.positionY = pose.value().positionY;
                found->second.pose.positionZ = pose.value().positionZ;
            } else {
                found->second.pose = std::move(pose).takeValue();
            }
        }
        applyWorldTransform(*found->second.effect, found->second.binding.spatial, found->second.pose);
        const double duration = block.duration.seconds();
        if (duration > 0.0) {
            if (!std::isfinite(context.playbackRate) || context.playbackRate <= 0.0)
                return actionFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                           "VFX playback rate must be positive and finite", "playbackRate");
            const double externalRate = found->second.binding.playbackRateSynced ? context.playbackRate : 1.0;
            const float  stretch      = static_cast<float>(
                (found->second.binding.clipEndTime - found->second.binding.clipStartTime) /
                duration * externalRate);
            for (int i = 0; i < found->second.effect->getEmitterCount(); ++i)
                if (auto* emitter = found->second.effect->getEmitter(i)) emitter->setPlaybackSpeed(stretch);
        }
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    eve::Result<void> sample(const eve::action::ActionActiveBlock& block,
                             const eve::action::ActionNotifyContext&) const override {
        auto binding = eve::action::ActionVfxBinding::fromPayload(
            block.payload, eve::action::ActionVfxShape::State);
        if (!binding) return eve::Result<void>::failure(binding.status());
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::NoOp));
    }

    eve::Result<void> advance(const eve::action::ActionNotifyContext& context) override {
        bool changed = false;
        for (auto it = transients_.begin(); it != transients_.end();) {
            if (it->executionId != context.executionId || context.time < it->endTime) {
                ++it;
                continue;
            }
            finish(it->owned);
            it = transients_.erase(it);
            changed = true;
        }
        const auto reaped = std::erase_if(residuals_, [](const std::unique_ptr<ParticleEffect>& effect) {
            for (int i = 0; i < effect->getEmitterCount(); ++i)
                if (auto* emitter = effect->getEmitter(i); emitter && emitter->getCount() > 0) return false;
            return true;
        });
        changed = changed || reaped > 0;
        return eve::Result<void>::success(eve::Status::success(
            changed ? eve::StatusCode::Applied : eve::StatusCode::NoOp));
    }

private:
    struct ActiveEffect {
        std::unique_ptr<ParticleEffect>  effect;
        eve::action::ActionVfxBinding    binding;
        eve::EntitySpatialPose           pose;
    };
    struct TransientEffect {
        eve::action::ActionExecutionId executionId;
        eve::Duration endTime;
        ActiveEffect owned;
    };

    static eve::Result<eve::EntitySpatialPose> resolvePose(
        const eve::action::ActionSpatialBinding& spatial, const eve::action::ActionNotifyContext& context) {
        std::optional<ecs::EntityHandle> handle;
        eve::OptionalRef<const eve::IAttachmentPointSource> attachments;
        if (spatial.target == eve::action::ActionSpatialTarget::Source) {
            handle      = context.source;
            attachments = context.sourceAttachment;
        } else {
            if (spatial.targetIndex >= context.targets.size())
                return actionFailure<eve::EntitySpatialPose>(eve::DiagnosticCode::NotFound,
                                                             "VFX target index is unavailable", "targetIndex");
            handle = context.targets[spatial.targetIndex];
            if (spatial.targetIndex < context.targetAttachments.size())
                attachments = context.targetAttachments[spatial.targetIndex];
        }
        eve::EntitySpatialPose pose;
        if (handle) {
            auto root = eve::resolveEntitySpatialPose(*handle);
            if (!root) return root;
            pose = std::move(root).takeValue();
        }
        if (spatial.bone.empty()) return eve::Result<eve::EntitySpatialPose>::success(std::move(pose));
        if (!attachments)
            return actionFailure<eve::EntitySpatialPose>(eve::DiagnosticCode::Unsupported,
                                                         "VFX bone requires an attachment source", "bone");
        auto point = attachments->get().sampleAttachmentPoint(
            spatial.bone, {static_cast<float>(spatial.positionOffset.x),
                           static_cast<float>(spatial.positionOffset.y),
                           static_cast<float>(spatial.positionOffset.z)});
        if (!point) return eve::Result<eve::EntitySpatialPose>::failure(point.status());
        pose.positionX = point.value().x;
        pose.positionY = point.value().y;
        pose.positionZ = point.value().z;
        return eve::Result<eve::EntitySpatialPose>::success(std::move(pose));
    }

    static void applyWorldTransform(ParticleEffect& effect, const eve::action::ActionSpatialBinding& spatial,
                                    const eve::EntitySpatialPose& pose) {
        const double offsetX = spatial.bone.empty() ? spatial.positionOffset.x : 0.0;
        const double offsetY = spatial.bone.empty() ? spatial.positionOffset.y : 0.0;
        effect.setPosition(static_cast<float>(pose.positionX + offsetX),
                           static_cast<float>(pose.positionY + offsetY));
        effect.setRotation(static_cast<float>((pose.rotationZDegrees + spatial.rotationOffsetDegrees.z) *
                                              0.017453292519943295));
        effect.setScale(static_cast<float>(pose.scaleX * spatial.scale.x));
    }

    void finish(ActiveEffect& active) {
        active.effect->stop();
        if (active.binding.stopBehavior == eve::action::ActionVfxStopBehavior::ClearImmediately) {
            active.effect->reset();
            return;
        }
        bool hasParticles = false;
        for (int i = 0; i < active.effect->getEmitterCount(); ++i)
            if (auto* emitter = active.effect->getEmitter(i); emitter && emitter->getCount() > 0) {
                hasParticles = true;
                break;
            }
        if (hasParticles) residuals_.push_back(std::move(active.effect));
    }

    std::map<ActiveKey, ActiveEffect> active_;
    std::vector<TransientEffect> transients_;
    std::vector<std::unique_ptr<ParticleEffect>> residuals_;
};

class ParticleActionPreviewSink final : public eve::action::IActionPreviewSink {
public:
    eve::Result<void> prepare(const eve::action::ActionPreviewFrame& frame) override {
        discardPrepared();
        preparedReason_ = frame.reason;
        for (const auto& block : frame.activeBlocks) {
            if (block.type.format() != "presentation:vfx-state") continue;
            const std::string key = block.itemId.format();
            auto binding = eve::action::ActionVfxBinding::fromPayload(
                block.payload, eve::action::ActionVfxShape::State);
            if (!binding) return failPrepared(binding.status());
            const double target = mediaTime(binding.value(), block.localTime, block.duration);
            const auto   found  = current_.find(key);
            if (frame.reason == eve::action::ActionPreviewReason::Advance && found != current_.end() &&
                found->second.payload == block.payload && found->second.duration == block.duration &&
                target + kTimeEpsilon >= found->second.mediaTime) {
                preparedRetained_.emplace(key, target);
                continue;
            }
            auto effect = makeEffect(binding.value(), target);
            if (!effect) return failPrepared(effect.status());
            preparedStates_.emplace(
                key, PreviewEffect{block.payload, block.duration, std::move(effect).takeValue(), target});
        }

        for (const auto& cue : frame.cues) {
            if (cue.kind != eve::action::ActionPreviewCueKind::Vfx || cue.type.format() != "presentation:vfx")
                continue;
            auto binding = eve::action::ActionVfxBinding::fromPayload(
                cue.payload, eve::action::ActionVfxShape::Instant);
            if (!binding) return failPrepared(binding.status());
            auto lifetime = eve::Duration::fromSeconds(binding.value().lifetimeSeconds);
            if (!lifetime) return failPrepared(lifetime.status());
            auto end = cue.time.tryAdd(lifetime.value());
            if (!end) return failPrepared(end.status());
            if (frame.current >= end.value()) continue;
            const auto elapsedNs = std::max<std::int64_t>(0, frame.current.nanoseconds() - cue.time.nanoseconds());
            const auto elapsed   = eve::Duration::fromNanoseconds(elapsedNs);
            const double target  = mediaTime(binding.value(), elapsed, lifetime.value());
            auto effect = makeEffect(binding.value(), target);
            if (!effect) return failPrepared(effect.status());
            preparedInstants_.push_back(
                TransientEffect{std::move(binding).takeValue(), cue.time, end.value(),
                                std::move(effect).takeValue(), target});
        }
        preparedCurrent_ = frame.current;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    void present(const eve::action::ActionPreviewFrame&) noexcept override {
        for (auto it = current_.begin(); it != current_.end();) {
            if (preparedRetained_.contains(it->first)) {
                ++it;
                continue;
            }
            it = current_.erase(it);
        }
        for (const auto& [key, target] : preparedRetained_) {
            auto& current = current_.at(key);
            simulateRange(*current.effect, current.mediaTime, target);
            current.mediaTime = target;
        }
        current_.merge(preparedStates_);
        for (auto& [key, value] : current_) {
            (void)key;
            value.effect->setVisible(true);
        }

        if (preparedReason_ != eve::action::ActionPreviewReason::Advance) transients_.clear();
        for (auto it = transients_.begin(); it != transients_.end();) {
            if (preparedCurrent_ >= it->endTime) {
                it = transients_.erase(it);
                continue;
            }
            const auto elapsedNs = std::max<std::int64_t>(
                0, preparedCurrent_.nanoseconds() - it->startTime.nanoseconds());
            const auto duration = eve::Duration::fromNanoseconds(
                it->endTime.nanoseconds() - it->startTime.nanoseconds());
            const double target = mediaTime(
                it->binding, eve::Duration::fromNanoseconds(elapsedNs), duration);
            simulateRange(*it->effect, it->mediaTime, target);
            it->mediaTime = target;
            ++it;
        }
        for (auto& value : preparedInstants_) value.effect->setVisible(true);
        transients_.splice(transients_.end(), preparedInstants_);
        preparedRetained_.clear();
    }

    void discardPrepared() noexcept override {
        preparedStates_.clear();
        preparedInstants_.clear();
        preparedRetained_.clear();
    }

private:
    static constexpr double kTimeEpsilon = 1e-9;

    struct PreviewEffect {
        eve::Value::Object             payload;
        eve::Duration                  duration;
        std::unique_ptr<ParticleEffect> effect;
        double                         mediaTime = 0.0;
    };

    struct TransientEffect {
        eve::action::ActionVfxBinding   binding;
        eve::Duration                   startTime;
        eve::Duration                   endTime;
        std::unique_ptr<ParticleEffect> effect;
        double                          mediaTime = 0.0;
    };

    eve::Result<void> failPrepared(const eve::Status& status) {
        discardPrepared();
        return eve::Result<void>::failure(status);
    }

    static double mediaTime(const eve::action::ActionVfxBinding& binding,
                            eve::Duration localTime, eve::Duration blockDuration) noexcept {
        const double duration = blockDuration.seconds();
        const double progress = duration > 0.0 ? std::clamp(localTime.seconds() / duration, 0.0, 1.0) : 0.0;
        return binding.clipStartTime + progress * (binding.clipEndTime - binding.clipStartTime);
    }

    static void simulateRange(ParticleEffect& effect, double from, double to) noexcept {
        double remaining = std::max(0.0, to - from);
        constexpr float fixedStep = 1.0f / 60.0f;
        while (remaining > kTimeEpsilon) {
            const float step = static_cast<float>(std::min<double>(remaining, fixedStep));
            for (int i = 0; i < effect.getEmitterCount(); ++i) {
                auto* emitter = effect.getEmitter(i);
                if (!emitter) continue;
                auto sim          = emitter->sim();
                const bool paused = sim->paused;
                sim->paused       = false;
                stepEmitterSim(*emitter->config(), *sim, step);
                sim->paused = paused;
            }
            remaining -= step;
        }
    }

    static eve::Result<std::unique_ptr<ParticleEffect>> makeEffect(
        const eve::action::ActionVfxBinding& binding, double targetTime) {
        auto* particles = eve::ModuleManager::getInstance<Particles>("Particles");
        if (!particles)
            return actionFailure<std::unique_ptr<ParticleEffect>>(
                eve::DiagnosticCode::NotFound, "VFX preview requires the Particles module", "particles");
        std::unique_ptr<ParticleEffect> effect(particles->newEffectFromFile(binding.uri));
        if (!effect)
            return actionFailure<std::unique_ptr<ParticleEffect>>(
                eve::DiagnosticCode::Failed,
                particles->getLastEffectError().empty() ? "VFX preview asset could not be loaded"
                                                        : particles->getLastEffectError(),
                "uri");
        effect->setVisible(false);
        effect->setPosition(static_cast<float>(binding.spatial.positionOffset.x),
                            static_cast<float>(binding.spatial.positionOffset.y));
        effect->setRotation(static_cast<float>(binding.spatial.rotationOffsetDegrees.z * 0.017453292519943295));
        effect->setScale(static_cast<float>(binding.spatial.scale.x));
        for (int i = 0; i < effect->getEmitterCount(); ++i) {
            auto* emitter = effect->getEmitter(i);
            if (!emitter) continue;
            emitter->setRandomSeed(emitter->getRandomSeed());
            emitter->setPlaybackSpeed(1.0f);
        }
        effect->start();
        simulateRange(*effect, 0.0, targetTime);
        effect->pause();
        return eve::Result<std::unique_ptr<ParticleEffect>>::success(std::move(effect));
    }

    std::map<std::string, PreviewEffect> current_;
    std::list<TransientEffect>           transients_;
    std::map<std::string, PreviewEffect> preparedStates_;
    std::list<TransientEffect>           preparedInstants_;
    std::map<std::string, double>        preparedRetained_;
    eve::action::ActionPreviewReason     preparedReason_ = eve::action::ActionPreviewReason::Refresh;
    eve::Duration                        preparedCurrent_ = eve::Duration::zero();
};

class ParticleActionProvider final : public eve::action::IActionNotifyProvider,
                                     public eve::action::IActionPreviewSinkProvider {
public:
    eve::Result<void> install(eve::action::ActionNotifyRegistry& registry) override {
        auto handler = std::make_shared<ParticleActionHandler>();
        auto instant = registry.registerHandler("presentation:vfx", handler);
        if (!instant) return instant;
        return registry.registerHandler("presentation:vfx-state", std::move(handler));
    }

    eve::Result<std::unique_ptr<eve::action::IActionPreviewSink>> createActionPreviewSink() override {
        return eve::Result<std::unique_ptr<eve::action::IActionPreviewSink>>::success(
            std::make_unique<ParticleActionPreviewSink>());
    }
};

}  // namespace

void registerParticlesCapabilities() {
    static ParticlesQueryImpl impl;
    static ParticleActionProvider actionProvider;
    eve::cap::provide<eve::IParticlesQuery>(&impl);
    eve::cap::addListener<eve::action::IActionNotifyProvider>(&actionProvider);
    eve::cap::addListener<eve::action::IActionPreviewSinkProvider>(&actionProvider);
}

}  // namespace eve::particles
