#include "common/AudioQuery.h"
#include "common/Capability.h"
#include "audio/Audio.h"

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

}  // namespace

void registerAudioCapabilities() {
    static AudioQueryImpl impl;
    eve::cap::provide<eve::IAudioQuery>(&impl);
}

}  // namespace eve::audio
