#include "action/ActionAudioBlock.h"
#include "action/ActionNotifyRegistry.h"
#include "action/ActionPreview.h"
#include "action/ActionSpatialBlock.h"
#include "audio/Audio.h"
#include "audio/Source.h"
#include "common/AudioQuery.h"
#include "common/Capability.h"
#include "common/EntitySpatialResolver.h"
#include "sound/Sound.h"
#include "sound/SoundData.h"

#include <algorithm>
#include <cmath>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

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
        const bool instant = event.kind == eve::action::ActionTimelineEventKind::Notify;
        if (!instant && event.kind != eve::action::ActionTimelineEventKind::StateEnter)
            return fail(eve::DiagnosticCode::InvalidArgument, "audio state requires enter or exit", "event.kind");
        if (!instant && active_.contains(key))
            return fail(eve::DiagnosticCode::Conflict, "audio state is already active", "itemId");
        const auto shape = instant ? eve::action::ActionAudioShape::Instant : eve::action::ActionAudioShape::State;
        auto binding = eve::action::ActionAudioBinding::fromPayload(event.payload, shape);
        if (!binding) return eve::Result<void>::failure(binding.status());
        auto pose = resolvePose(binding.value().spatial, context);
        if (!pose) return eve::Result<void>::failure(pose.status());
        auto* audio = eve::ModuleManager::getInstance<Audio>("Audio");
        if (!audio) return fail(eve::DiagnosticCode::NotFound, "Audio module is unavailable", "audio");
        try {
            auto* data = eve::sound::Sound::create()->newSoundDataFromFile(binding.value().uri);
            std::unique_ptr<Source> source(audio->newSource(data));
            source->setVolume(static_cast<float>(binding.value().volume));
            source->setPitch(static_cast<float>(binding.value().pitch));
            source->setLooping(binding.value().looping);
            applyPosition(*source, binding.value().spatial, pose.value());
            source->play();
            ActiveSource owned{data, std::move(source),
                               binding.value().spatial, std::move(pose).takeValue()};
            if (instant) {
                auto duration = eve::Duration::fromSeconds(owned.source->getDuration() / owned.source->getPitch());
                if (!duration) return eve::Result<void>::failure(duration.status());
                auto end = context.time.tryAdd(duration.value());
                if (!end) return eve::Result<void>::failure(end.status());
                transients_.push_back({context.executionId, std::move(end).takeValue(), std::move(owned)});
            } else {
                active_.emplace(key, std::move(owned));
            }
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
        auto binding = eve::action::ActionAudioBinding::fromPayload(
            block.payload, eve::action::ActionAudioShape::State);
        if (!binding) return eve::Result<void>::failure(binding.status());
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::NoOp));
    }

    eve::Result<void> advance(const eve::action::ActionNotifyContext& context) override {
        const auto before = transients_.size();
        std::erase_if(transients_, [&](TransientSource& transient) {
            if (transient.executionId != context.executionId || context.time < transient.endTime) return false;
            transient.owned.source->stop();
            return true;
        });
        return eve::Result<void>::success(eve::Status::success(
            before == transients_.size() ? eve::StatusCode::NoOp : eve::StatusCode::Applied));
    }

private:
    struct ActiveSource {
        // Source borrows this data; the ref keeps it alive across cache unload.
        eve::ref<eve::sound::SoundData> data;
        std::unique_ptr<Source> source;
        eve::action::ActionSpatialBinding spatial;
        eve::EntitySpatialPose pose;
    };
    struct TransientSource {
        eve::action::ActionExecutionId executionId;
        eve::Duration endTime;
        ActiveSource owned;
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
                return eve::Result<eve::EntitySpatialPose>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::NotFound,
                                           "audio target index is unavailable", "targetIndex"));
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
            return eve::Result<eve::EntitySpatialPose>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Unsupported,
                                       "audio bone requires an attachment source", "bone"));
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

    static void applyPosition(Source& source, const eve::action::ActionSpatialBinding& spatial,
                              const eve::EntitySpatialPose& pose) {
        const double offsetX = spatial.bone.empty() ? spatial.positionOffset.x : 0.0;
        const double offsetY = spatial.bone.empty() ? spatial.positionOffset.y : 0.0;
        const double offsetZ = spatial.bone.empty() ? spatial.positionOffset.z : 0.0;
        source.setPosition(static_cast<float>(pose.positionX + offsetX),
                           static_cast<float>(pose.positionY + offsetY),
                           static_cast<float>(pose.positionZ + offsetZ));
    }

    static eve::Result<void> fail(eve::DiagnosticCode code, std::string message, std::string path) {
        return eve::Result<void>::failure(eve::Diagnostic::error(code, std::move(message), std::move(path)));
    }

    std::map<ActiveKey, ActiveSource> active_;
    std::vector<TransientSource> transients_;
};

template <typename T>
eve::Result<T> previewFailure(eve::DiagnosticCode code, std::string message, std::string path) {
    return eve::Result<T>::failure(eve::Diagnostic::error(code, std::move(message), std::move(path)));
}

class AudioActionPreviewSink final : public eve::action::IActionPreviewSink {
public:
    ~AudioActionPreviewSink() override {
        discardPrepared();
        stopAll(current_);
        stopAll(transients_);
    }

    eve::Result<void> prepare(const eve::action::ActionPreviewFrame& frame) override {
        discardPrepared();
        const bool retainContinuous = frame.reason == eve::action::ActionPreviewReason::Advance;
        for (const auto& block : frame.activeBlocks) {
            if (block.type.format() != "presentation:audio-state") continue;
            const std::string key = block.itemId.format();
            auto binding = eve::action::ActionAudioBinding::fromPayload(
                block.payload, eve::action::ActionAudioShape::State);
            if (!binding) return failPrepared(binding.status());
            const auto current = current_.find(key);
            if (retainContinuous && current != current_.end() && current->second.payload == block.payload) {
                preparedRetained_.insert(key);
                continue;
            }
            auto audio = makeAudio(binding.value(), block.localTime);
            if (!audio) return failPrepared(audio.status());
            if (audio.value())
                preparedStates_.emplace(key, PreviewAudio{block.payload, std::move(*audio.value())});
        }
        for (const auto& cue : frame.cues) {
            if (cue.kind != eve::action::ActionPreviewCueKind::Audio ||
                cue.type.format() != "presentation:audio")
                continue;
            auto binding = eve::action::ActionAudioBinding::fromPayload(
                cue.payload, eve::action::ActionAudioShape::Instant);
            if (!binding) return failPrepared(binding.status());
            auto audio = makeAudio(binding.value(), eve::Duration::zero());
            if (!audio) return failPrepared(audio.status());
            if (audio.value()) preparedInstants_.push_back(std::move(*audio.value()));
        }
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    void present(const eve::action::ActionPreviewFrame&) noexcept override {
        for (auto it = current_.begin(); it != current_.end();) {
            if (preparedRetained_.contains(it->first)) {
                ++it;
                continue;
            }
            it->second.audio.source->stop();
            it = current_.erase(it);
        }
        current_.merge(preparedStates_);
        for (auto& [key, state] : current_) {
            if (preparedRetained_.contains(key)) continue;
            state.audio.source->play();
        }
        preparedRetained_.clear();

        std::erase_if(transients_, [](const OwnedAudio& audio) { return !audio.source->isPlaying(); });
        for (auto& audio : preparedInstants_) audio.source->play();
        transients_.splice(transients_.end(), preparedInstants_);
    }

    void discardPrepared() noexcept override {
        preparedStates_.clear();
        preparedInstants_.clear();
        preparedRetained_.clear();
    }

private:
    struct OwnedAudio {
        OwnedAudio(eve::sound::SoundData* data, std::unique_ptr<Source> source)
            : data(data), source(std::move(source)) {}

        // Source borrows this data; the ref keeps it alive across cache unload.
        eve::ref<eve::sound::SoundData> data;
        std::unique_ptr<Source>         source;
    };
    struct PreviewAudio {
        eve::Value::Object payload;
        OwnedAudio         audio;
    };

    eve::Result<void> failPrepared(const eve::Status& status) {
        discardPrepared();
        return eve::Result<void>::failure(status);
    }

    static eve::Result<std::optional<OwnedAudio>> makeAudio(
        const eve::action::ActionAudioBinding& binding, eve::Duration localTime) {
        auto* audio = eve::ModuleManager::getInstance<Audio>("Audio");
        if (!audio)
            return previewFailure<std::optional<OwnedAudio>>(
                eve::DiagnosticCode::NotFound, "Audio preview requires the Audio module", "audio");
        try {
            auto* data = eve::sound::Sound::create()->newSoundDataFromFile(binding.uri);
            std::unique_ptr<Source> source(audio->newSource(data));
            OwnedAudio owned(data, std::move(source));
            owned.source->setVolume(static_cast<float>(binding.volume));
            owned.source->setPitch(static_cast<float>(binding.pitch));
            owned.source->setLooping(binding.looping);
            owned.source->setPosition(static_cast<float>(binding.spatial.positionOffset.x),
                                      static_cast<float>(binding.spatial.positionOffset.y),
                                      static_cast<float>(binding.spatial.positionOffset.z));
            const double duration = owned.source->getDuration();
            double       mediaTime = localTime.seconds() * binding.pitch;
            if (duration > 0.0) {
                if (binding.looping)
                    mediaTime = std::fmod(mediaTime, duration);
                else if (mediaTime >= duration)
                    return eve::Result<std::optional<OwnedAudio>>::success(std::nullopt);
            }
            if (mediaTime > 0.0 && !owned.source->seek(mediaTime))
                return previewFailure<std::optional<OwnedAudio>>(
                    eve::DiagnosticCode::Unsupported, "Audio preview source does not support seeking", "uri");
            return eve::Result<std::optional<OwnedAudio>>::success(
                std::optional<OwnedAudio>(std::move(owned)));
        } catch (const std::exception& error) {
            return previewFailure<std::optional<OwnedAudio>>(
                eve::DiagnosticCode::Failed, error.what(), "uri");
        }
    }

    static void stopAll(std::map<std::string, PreviewAudio>& values) noexcept {
        for (auto& [key, value] : values) {
            (void)key;
            value.audio.source->stop();
        }
        values.clear();
    }

    static void stopAll(std::list<OwnedAudio>& values) noexcept {
        for (auto& value : values) value.source->stop();
        values.clear();
    }

    std::map<std::string, PreviewAudio> current_;
    std::list<OwnedAudio>               transients_;
    std::map<std::string, PreviewAudio> preparedStates_;
    std::list<OwnedAudio>               preparedInstants_;
    std::set<std::string>               preparedRetained_;
};

class AudioActionProvider final : public eve::action::IActionNotifyProvider,
                                  public eve::action::IActionPreviewSinkProvider {
public:
    eve::Result<void> install(eve::action::ActionNotifyRegistry& registry) override {
        auto handler = std::make_shared<AudioActionHandler>();
        auto instant = registry.registerHandler("presentation:audio", handler);
        if (!instant) return instant;
        return registry.registerHandler("presentation:audio-state", std::move(handler));
    }

    eve::Result<std::unique_ptr<eve::action::IActionPreviewSink>> createActionPreviewSink() override {
        if (!eve::ModuleManager::getInstance<Audio>("Audio"))
            return previewFailure<std::unique_ptr<eve::action::IActionPreviewSink>>(
                eve::DiagnosticCode::NotFound, "Audio preview requires the Audio module", "audio");
        return eve::Result<std::unique_ptr<eve::action::IActionPreviewSink>>::success(
            std::make_unique<AudioActionPreviewSink>());
    }
};

}  // namespace

void registerAudioCapabilities() {
    static AudioQueryImpl impl;
    static AudioActionProvider actionProvider;
    eve::cap::provide<eve::IAudioQuery>(&impl);
    eve::cap::addListener<eve::action::IActionNotifyProvider>(&actionProvider);
    eve::cap::addListener<eve::action::IActionPreviewSinkProvider>(&actionProvider);
}

}  // namespace eve::audio
