#include "action/ActionAudioBlock.h"
#include "action/ActionAudioWaveform.h"
#include "action/ActionParameterCurve.h"
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
#include <cstdint>
#include <cmath>
#include <cstring>
#include <list>
#include <limits>
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

class ActionAudioWaveformProvider final : public eve::action::IActionAudioWaveformProvider {
public:
    eve::Result<eve::action::ActionAudioWaveform> waveform(
        const eve::action::ActionAudioWaveformRequest& request) override {
        if (request.bucketCount == 0 || request.bucketCount > kMaximumBuckets)
            return failure(eve::DiagnosticCode::InvalidArgument,
                           "Audio waveform bucket count is outside the bounded range", "bucketCount");
        if (request.blockDuration <= eve::Duration::zero())
            return failure(eve::DiagnosticCode::InvalidArgument,
                           "Audio waveform block duration must be positive", "blockDuration");
        if (request.binding.uri.empty())
            return failure(eve::DiagnosticCode::InvalidArgument,
                           "Audio waveform URI must be non-empty", "uri");
        if (!std::isfinite(request.binding.pitch) || request.binding.pitch <= 0.0)
            return failure(eve::DiagnosticCode::InvalidArgument,
                           "Audio waveform pitch must be positive and finite", "pitch");
        try {
            auto* data = eve::sound::Sound::create()->newSoundDataFromFile(request.binding.uri);
            if (!data || data->getSampleRate() <= 0 || data->getSampleCount() <= 0 ||
                (data->getBitDepth() != 8 && data->getBitDepth() != 16) ||
                (data->getChannelCount() != 1 && data->getChannelCount() != 2))
                return failure(eve::DiagnosticCode::Unsupported,
                               "Audio waveform requires bounded PCM8 or PCM16 data", "uri");
            eve::ref<eve::sound::SoundData> retained(data);
            (void)retained;
            eve::action::ActionAudioWaveform result;
            result.clipDurationSeconds = data->getDuration();
            result.buckets.resize(request.bucketCount);
            const double blockSeconds = request.blockDuration.seconds();
            const auto   frameCount   = static_cast<std::size_t>(data->getSampleCount());
            for (std::size_t bucket = 0; bucket < request.bucketCount; ++bucket) {
                const double localStart = blockSeconds * static_cast<double>(bucket) /
                                          static_cast<double>(request.bucketCount);
                const double localEnd = blockSeconds * static_cast<double>(bucket + 1) /
                                        static_cast<double>(request.bucketCount);
                result.buckets[bucket] = sampleBucket(*data, request.binding, localStart, localEnd, frameCount);
            }
            return eve::Result<eve::action::ActionAudioWaveform>::success(std::move(result));
        } catch (const std::exception& error) {
            return failure(eve::DiagnosticCode::Failed, error.what(), "uri");
        }
    }

private:
    static constexpr std::size_t kMaximumBuckets = 4096;
    static constexpr std::size_t kMaximumSamplesPerBucket = 8192;

    static float sampleAt(const eve::sound::SoundData& data, std::size_t frame, int channel) noexcept {
        const auto channels = static_cast<std::size_t>(data.getChannelCount());
        const auto index    = frame * channels + static_cast<std::size_t>(channel);
        const auto* bytes   = static_cast<const std::uint8_t*>(data.getData());
        if (data.getBitDepth() == 8)
            return (static_cast<float>(bytes[index]) - 128.0f) / 128.0f;
        std::int16_t sample = 0;
        std::memcpy(&sample, bytes + index * sizeof(sample), sizeof(sample));
        return static_cast<float>(sample) / 32768.0f;
    }

    static eve::action::ActionAudioWaveformBucket sampleBucket(
        const eve::sound::SoundData& data, const eve::action::ActionAudioBinding& binding,
        double localStart, double localEnd, std::size_t frameCount) noexcept {
        eve::action::ActionAudioWaveformBucket result;
        result.minimum = 1.0f;
        result.maximum = -1.0f;
        const double duration = data.getDuration();
        if (duration <= 0.0) return {};
        double mediaStart = localStart * binding.pitch;
        double mediaEnd   = localEnd * binding.pitch;
        if (!binding.looping && mediaStart >= duration) return {};
        if (binding.looping) {
            const double span = std::max(0.0, mediaEnd - mediaStart);
            mediaStart = span >= duration ? 0.0 : std::fmod(mediaStart, duration);
            mediaEnd   = mediaStart + std::min(span, duration);
        } else {
            mediaStart = std::min(mediaStart, duration);
            mediaEnd   = std::min(mediaEnd, duration);
        }
        const double mediaSpan = std::max(0.0, mediaEnd - mediaStart);
        const auto estimatedFrames = static_cast<std::size_t>(
            std::ceil(mediaSpan * static_cast<double>(data.getSampleRate())));
        const std::size_t stride = std::max<std::size_t>(
            1, (estimatedFrames + kMaximumSamplesPerBucket - 1) / kMaximumSamplesPerBucket);
        const std::size_t samples = std::max<std::size_t>(
            1, std::min(estimatedFrames + 1, kMaximumSamplesPerBucket));
        for (std::size_t offset = 0; offset < samples; ++offset) {
            double media = mediaStart + static_cast<double>(offset * stride) /
                                            static_cast<double>(data.getSampleRate());
            if (binding.looping)
                media = std::fmod(media, duration);
            else
                media = std::min(media, duration);
            const auto frame = std::min(frameCount - 1, static_cast<std::size_t>(media * data.getSampleRate()));
            for (int channel = 0; channel < data.getChannelCount(); ++channel) {
                const float value = sampleAt(data, frame, channel);
                result.minimum = std::min(result.minimum, value);
                result.maximum = std::max(result.maximum, value);
            }
        }
        if (result.minimum > result.maximum) return {};
        return result;
    }

    static eve::Result<eve::action::ActionAudioWaveform> failure(
        eve::DiagnosticCode code, std::string message, std::string path) {
        return eve::Result<eve::action::ActionAudioWaveform>::failure(
            eve::Diagnostic::error(code, std::move(message), std::move(path)));
    }
};

class AudioActionParameterSink final : public eve::action::IActionParameterSink {
public:
    bool supports(const eve::LogicalId& target) const noexcept override {
        return target.format() == "audio:master-volume";
    }

    eve::Result<void> apply(const eve::action::ActionParameterSample& sample) override {
        auto* audio = eve::ModuleManager::getInstance<Audio>("Audio");
        if (!audio)
            return parameterFailure(eve::DiagnosticCode::NotFound, "Audio parameter target is unavailable", "audio");
        const ActiveKey key{sample.executionId, sample.itemId.format()};
        auto candidate = active_;
        if (sample.phase == eve::action::ActionParameterPhase::Begin) {
            if (candidate.contains(key))
                return parameterFailure(eve::DiagnosticCode::Conflict,
                                        "Audio parameter curve is already active", "itemId");
            if (candidate.empty()) baseline_ = audio->getVolume();
            candidate.emplace(key, ActiveValue{sample.operation, sample.value});
        } else if (sample.phase == eve::action::ActionParameterPhase::Update) {
            const auto found = candidate.find(key);
            if (found == candidate.end())
                return parameterFailure(eve::DiagnosticCode::NotFound,
                                        "Audio parameter curve has no active state", "itemId");
            found->second = ActiveValue{sample.operation, sample.value};
        } else {
            if (!candidate.erase(key))
                return eve::Result<void>::success(eve::Status::success(eve::StatusCode::NoOp));
        }
        const double value = compose(candidate);
        if (!std::isfinite(value) || value < 0.0 || value > static_cast<double>(std::numeric_limits<float>::max()))
            return parameterFailure(eve::DiagnosticCode::InvalidArgument,
                                    "Audio parameter curves produced an invalid master volume", "value");
        active_ = std::move(candidate);
        audio->setVolume(static_cast<float>(value));
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

private:
    using ActiveKey = std::pair<eve::action::ActionExecutionId, std::string>;
    struct ActiveValue {
        eve::action::ActionParameterOperation operation = eve::action::ActionParameterOperation::Replace;
        double                                value = 0.0;
    };

    double compose(const std::map<ActiveKey, ActiveValue>& values) const noexcept {
        double result = baseline_;
        for (const auto& [key, active] : values) {
            (void)key;
            switch (active.operation) {
                case eve::action::ActionParameterOperation::Replace: result = active.value; break;
                case eve::action::ActionParameterOperation::Add: result += active.value; break;
                case eve::action::ActionParameterOperation::Multiply: result *= active.value; break;
            }
        }
        return result;
    }

    static eve::Result<void> parameterFailure(eve::DiagnosticCode code, std::string message,
                                               std::string path) {
        return eve::Result<void>::failure(eve::Diagnostic::error(code, std::move(message), std::move(path)));
    }

    std::map<ActiveKey, ActiveValue> active_;
    double                           baseline_ = 1.0;
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
    static ActionAudioWaveformProvider waveformProvider;
    static AudioActionParameterSink parameterSink;
    eve::cap::provide<eve::IAudioQuery>(&impl);
    eve::cap::provide<eve::action::IActionAudioWaveformProvider>(&waveformProvider);
    eve::cap::addListener<eve::action::IActionParameterSink>(&parameterSink);
    eve::cap::addListener<eve::action::IActionNotifyProvider>(&actionProvider);
    eve::cap::addListener<eve::action::IActionPreviewSinkProvider>(&actionProvider);
}

}  // namespace eve::audio
