#include "action/ActionNotifyRegistry.h"
#include "action/ActionSpatialBlock.h"
#include "common/Capability.h"
#include "common/EntitySpatialResolver.h"
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
        auto pose = resolvePose(spatial.value(), context);
        if (!pose) return eve::Result<void>::failure(pose.status());
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
        applyWorldTransform(*effect, spatial.value(), pose.value());
        effect->start();
        active_.emplace(key, ActiveEffect{std::move(effect), std::move(spatial).takeValue(),
                                          std::move(pose).takeValue()});
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    eve::Result<void> update(const eve::action::ActionActiveBlock& block,
                             const eve::action::ActionNotifyContext& context) override {
        const auto found = active_.find({context.executionId, block.itemId.format()});
        if (found == active_.end())
            return actionFailure<void>(eve::DiagnosticCode::NotFound, "Active VFX state has no instance", "itemId");
        if (found->second.spatial.mode != eve::action::ActionSpatialAttachmentMode::WorldTransformAtStart) {
            auto pose = resolvePose(found->second.spatial, context);
            if (!pose) return eve::Result<void>::failure(pose.status());
            if (found->second.spatial.mode == eve::action::ActionSpatialAttachmentMode::FollowPositionOnly) {
                found->second.pose.positionX = pose.value().positionX;
                found->second.pose.positionY = pose.value().positionY;
                found->second.pose.positionZ = pose.value().positionZ;
            } else {
                found->second.pose = std::move(pose).takeValue();
            }
        }
        applyWorldTransform(*found->second.effect, found->second.spatial, found->second.pose);
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
        eve::EntitySpatialPose             pose;
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
