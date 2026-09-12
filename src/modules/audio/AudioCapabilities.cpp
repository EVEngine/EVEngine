#include "action/ActionNotifyRegistry.h"
#include "action/ActionSpatialBlock.h"
#include "audio/Audio.h"
#include "audio/Source.h"
#include "common/AudioQuery.h"
#include "common/Capability.h"
#include "common/EntitySpatialResolver.h"
#include "sound/Sound.h"
#include "sound/SoundData.h"

#include <map>
#include <memory>

namespace eve::audio {
namespace {

class AudioQueryImpl final : public eve::IAudioQuery {
public:
    float volume() const override {
        auto *a = eve::ModuleManager::getInstance<Audio>("Audio");
        return a ? a->getVolume() : 1.f;
    }

    void setVolume(float v) override {
        if (auto *a = eve::ModuleManager::getInstance<Audio>("Audio")) a->setVolume(v);
    }

    void stopAll() override {
        if (auto *a = eve::ModuleManager::getInstance<Audio>("Audio")) a->stopAll();
    }
};

using ActiveKey = std::pair<eve::action::ActionExecutionId, std::string>;

class AudioActionHandler final : public eve::action::IActionNotifyHandler {
public:
    eve::Result<void> handle(const eve::action::ActionTimelineEvent& event,
                             const eve::action::ActionNotifyContext& context) override {
        const ActiveKey key{context.executionId, event.itemId.format()};
        if (event.kind == eve::action::ActionTimelineEventKind::StateExit) {
            const auto found = active_.find(key);
            if (found == active_.end())
                return eve::Result<void>::success(eve::Status::success(eve::StatusCode::NoOp));
            found->second.source->stop();
            active_.erase(found);
            return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
        }
        if (event.kind != eve::action::ActionTimelineEventKind::StateEnter)
            return fail(eve::DiagnosticCode::InvalidArgument, "audio state requires enter or exit", "event.kind");
        auto spatial = eve::action::ActionSpatialBinding::fromPayload(event.payload);
        if (!spatial) return eve::Result<void>::failure(spatial.status());
        auto pose = resolvePose(spatial.value(), context);
        if (!pose) return eve::Result<void>::failure(pose.status());
        const auto uri = event.payload.find("uri");
        if (uri == event.payload.end() || !uri->second.getIf<std::string>())
            return fail(eve::DiagnosticCode::InvalidArgument, "audio URI must be text", "uri");
        auto* audio = eve::ModuleManager::getInstance<Audio>("Audio");
        if (!audio) return fail(eve::DiagnosticCode::NotFound, "Audio module is unavailable", "audio");
        try {
            std::unique_ptr<eve::sound::SoundData> data(
                eve::sound::Sound::create()->newSoundDataFromFile(*uri->second.getIf<std::string>()));
            std::unique_ptr<Source> source(audio->newSource(data.get()));
            if (const auto value = event.payload.find("volume"); value != event.payload.end())
                if (const auto* number = value->second.getIf<double>()) source->setVolume(static_cast<float>(*number));
            if (const auto value = event.payload.find("pitch"); value != event.payload.end())
                if (const auto* number = value->second.getIf<double>()) source->setPitch(static_cast<float>(*number));
            if (const auto value = event.payload.find("looping"); value != event.payload.end())
                if (const auto* enabled = value->second.getIf<bool>()) source->setLooping(*enabled);
            applyPosition(*source, spatial.value(), pose.value());
            source->play();
            active_.emplace(key, ActiveSource{std::move(data), std::move(source),
                                              std::move(spatial).takeValue(), std::move(pose).takeValue()});
        } catch (const std::exception& error) {
            return fail(eve::DiagnosticCode::Failed, error.what(), "uri");
        }
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    eve::Result<void> update(const eve::action::ActionActiveBlock& block,
                             const eve::action::ActionNotifyContext& context) override {
        const auto found = active_.find({context.executionId, block.itemId.format()});
        if (found == active_.end()) return fail(eve::DiagnosticCode::NotFound, "active audio has no source", "itemId");
        if (found->second.spatial.mode != eve::action::ActionSpatialAttachmentMode::WorldTransformAtStart) {
            auto pose = resolvePose(found->second.spatial, context);
            if (!pose) return eve::Result<void>::failure(pose.status());
            found->second.pose.positionX = pose.value().positionX;
            found->second.pose.positionY = pose.value().positionY;
            found->second.pose.positionZ = pose.value().positionZ;
            if (found->second.spatial.mode == eve::action::ActionSpatialAttachmentMode::FollowTarget)
                found->second.pose = std::move(pose).takeValue();
        }
        applyPosition(*found->second.source, found->second.spatial, found->second.pose);
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    eve::Result<void> sample(const eve::action::ActionActiveBlock& block,
                             const eve::action::ActionNotifyContext&) const override {
        auto spatial = eve::action::ActionSpatialBinding::fromPayload(block.payload);
        if (!spatial) return eve::Result<void>::failure(spatial.status());
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::NoOp));
    }

private:
    struct ActiveSource {
        std::unique_ptr<eve::sound::SoundData> data;
        std::unique_ptr<Source> source;
        eve::action::ActionSpatialBinding spatial;
        eve::EntitySpatialPose pose;
    };

    static eve::Result<eve::EntitySpatialPose> resolvePose(
        const eve::action::ActionSpatialBinding& spatial, const eve::action::ActionNotifyContext& context) {
        std::optional<ecs::EntityHandle> handle;
        if (spatial.target == eve::action::ActionSpatialTarget::Source) {
            handle = context.source;
        } else {
            if (spatial.targetIndex >= context.targets.size())
                return eve::Result<eve::EntitySpatialPose>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::NotFound,
                                           "audio target index is unavailable", "targetIndex"));
            handle = context.targets[spatial.targetIndex];
        }
        if (!handle) return eve::Result<eve::EntitySpatialPose>::success({});
        return eve::resolveEntitySpatialPose(*handle, spatial.bone);
    }

    static void applyPosition(Source& source, const eve::action::ActionSpatialBinding& spatial,
                              const eve::EntitySpatialPose& pose) {
        source.setPosition(static_cast<float>(pose.positionX + spatial.positionOffset.x),
                           static_cast<float>(pose.positionY + spatial.positionOffset.y),
                           static_cast<float>(pose.positionZ + spatial.positionOffset.z));
    }

    static eve::Result<void> fail(eve::DiagnosticCode code, std::string message, std::string path) {
        return eve::Result<void>::failure(eve::Diagnostic::error(code, std::move(message), std::move(path)));
    }

    std::map<ActiveKey, ActiveSource> active_;
};

class AudioActionProvider final : public eve::action::IActionNotifyProvider {
public:
    eve::Result<void> install(eve::action::ActionNotifyRegistry& registry) override {
        return registry.registerHandler("presentation:audio-state", std::make_shared<AudioActionHandler>());
    }
};

}  // namespace

void registerAudioCapabilities() {
    static AudioQueryImpl impl;
    static AudioActionProvider actionProvider;
    eve::cap::provide<eve::IAudioQuery>(&impl);
    eve::cap::addListener<eve::action::IActionNotifyProvider>(&actionProvider);
}

}  // namespace eve::audio
