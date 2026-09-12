#include "action/ActionNotifyRegistry.h"
#include "action/ActionSpatialBlock.h"
#include "audio/Audio.h"
#include "audio/Source.h"
#include "common/AudioQuery.h"
#include "common/Capability.h"
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
        if ((spatial.value().target == eve::action::ActionSpatialTarget::Source && context.source.has_value()) ||
            (spatial.value().target == eve::action::ActionSpatialTarget::Target && !context.targets.empty()))
            return fail(eve::DiagnosticCode::Unsupported,
                        "entity or bone anchored audio requires a spatial resolver", "spatialTarget");
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
            source->setPosition(static_cast<float>(spatial.value().positionOffset.x),
                                static_cast<float>(spatial.value().positionOffset.y),
                                static_cast<float>(spatial.value().positionOffset.z));
            source->play();
            active_.emplace(key, ActiveSource{std::move(data), std::move(source), std::move(spatial).takeValue()});
        } catch (const std::exception& error) {
            return fail(eve::DiagnosticCode::Failed, error.what(), "uri");
        }
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    eve::Result<void> update(const eve::action::ActionActiveBlock& block,
                             const eve::action::ActionNotifyContext& context) override {
        const auto found = active_.find({context.executionId, block.itemId.format()});
        if (found == active_.end()) return fail(eve::DiagnosticCode::NotFound, "active audio has no source", "itemId");
        found->second.source->setPosition(static_cast<float>(found->second.spatial.positionOffset.x),
                                          static_cast<float>(found->second.spatial.positionOffset.y),
                                          static_cast<float>(found->second.spatial.positionOffset.z));
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
    };

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
