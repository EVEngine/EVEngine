#include "common/Capability.h"
#include "stylize/AttackVfxLayerExecutor.h"

#if defined(EVE_STYLIZE_ACTION_AUDIO)
#include "audio/Audio.h"
#include "audio/Source.h"
#include "common/Module.h"
#include "common/Resource.h"
#include "sound/Sound.h"
#include "sound/SoundData.h"

#include <cstdint>
#include <exception>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#endif

namespace eve::stylize_action {

#if defined(EVE_STYLIZE_ACTION_AUDIO)
namespace {

float layerParam(const eve::stylize::AttackVfxLayerStartRequest& request, const char* key,
                 float fallback) {
    if (request.layer) {
        const auto found = request.layer->floatParams.find(key);
        if (found != request.layer->floatParams.end()) return found->second;
    }
    return fallback;
}

class AudioAttackVfxExecutor final : public eve::stylize::IAttackVfxLayerExecutor {
public:
    eve::stylize::AttackVfxLayerRole role() const noexcept override {
        return eve::stylize::AttackVfxLayerRole::Audio;
    }

    eve::Result<eve::stylize::AttackVfxLayerHandle> start(
        const eve::stylize::AttackVfxLayerStartRequest& request) override {
        if (!request.layer)
            return eve::Result<eve::stylize::AttackVfxLayerHandle>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "AttackVfx audio layer is null", "layer"));
        if (request.layer->uri.empty())
            return eve::Result<eve::stylize::AttackVfxLayerHandle>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "AttackVfx audio layer uri is empty", "uri"));

        auto* audio = eve::ModuleManager::getInstance<eve::audio::Audio>("Audio");
        auto* sound = eve::ModuleManager::getInstance<eve::sound::Sound>("Sound");
        if (!audio || !sound)
            return eve::Result<eve::stylize::AttackVfxLayerHandle>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::NotFound, "Audio/Sound modules are unavailable", "audio"));

        try {
            auto* data = sound->newSoundDataFromFile(request.layer->uri);
            auto  pin  = eve::ResourceManager::getInstance().pin(data);
            if (!pin.ok())
                return eve::Result<eve::stylize::AttackVfxLayerHandle>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::NotFound, "audio resource is no longer cached", "uri"));
            eve::ResourcePin keepAlive = std::move(pin).takeValue();
            auto*           live      = static_cast<eve::sound::SoundData*>(keepAlive.get());
            std::unique_ptr<eve::audio::Source> source(audio->newSource(live));
            if (!source)
                return eve::Result<eve::stylize::AttackVfxLayerHandle>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::Failed, "audio Source could not be created", "uri"));

            const float volume = layerParam(request, "volume", 1.f);
            const float pitch  = layerParam(request, "pitch", 1.f);
            source->setVolume(volume);
            source->setPitch(pitch);
            source->setLooping(layerParam(request, "looping", 0.f) > 0.5f);
            source->setRelative(true);
            if (request.playRequest) {
                source->setPosition(static_cast<float>(request.playRequest->sourceId), 0.f,
                                    static_cast<float>(request.playRequest->targetId));
            }
            source->play();

            Live owned;
            owned.pin    = std::move(keepAlive);
            owned.source = std::move(source);
            const auto id = ++nextId_;
            live_.emplace(id, std::move(owned));
            return eve::Result<eve::stylize::AttackVfxLayerHandle>::success(
                eve::stylize::AttackVfxLayerHandle{id});
        } catch (const std::exception& error) {
            return eve::Result<eve::stylize::AttackVfxLayerHandle>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::NotFound, error.what(), "uri"));
        }
    }

    eve::Result<void> update(eve::stylize::AttackVfxLayerHandle handle, double,
                             const eve::stylize::AttackVfxLayerStartRequest&) override {
        if (!live_.contains(handle.id))
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::StaleHandle, "audio AttackVfx layer handle is stale", "handle"));
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::NoOp));
    }

    eve::Result<void> stop(eve::stylize::AttackVfxLayerHandle handle,
                           eve::stylize::AttackVfxStopBehavior behavior) override {
        const auto found = live_.find(handle.id);
        if (found == live_.end())
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::StaleHandle, "audio AttackVfx layer handle is stale", "handle"));
        if (found->second.source) {
            found->second.source->stop();
            if (behavior == eve::stylize::AttackVfxStopBehavior::ClearImmediately)
                found->second.source->seek(0.0);
        }
        live_.erase(found);
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

private:
    struct Live {
        eve::ResourcePin                    pin;
        std::unique_ptr<eve::audio::Source> source;
    };

    std::uint64_t nextId_ = 1;
    std::unordered_map<std::uint64_t, Live> live_;
};

AudioAttackVfxExecutor& audioExecutor() {
    static AudioAttackVfxExecutor instance;
    return instance;
}

bool gAudioRegistered = false;

}  // namespace

void registerAudioAttackVfxExecutor() {
    if (gAudioRegistered) return;
    eve::cap::addListener<eve::stylize::IAttackVfxLayerExecutor>(&audioExecutor());
    gAudioRegistered = true;
}

void unregisterAudioAttackVfxExecutor() {
    if (!gAudioRegistered) return;
    eve::cap::removeListener<eve::stylize::IAttackVfxLayerExecutor>(&audioExecutor());
    gAudioRegistered = false;
}

#else

void registerAudioAttackVfxExecutor() {}
void unregisterAudioAttackVfxExecutor() {}

#endif

}  // namespace eve::stylize_action
