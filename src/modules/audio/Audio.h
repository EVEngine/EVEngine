#pragma once

#include "common/Module.h"
#include "common/Data.h"

#include "sound/SoundData.h"
#include "sound/Decoder.h"

#include <AL/alc.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace eve {
namespace audio {

class Source;

class Audio : public Module {
public:
    Module_REG(Audio);

    Audio();
    ~Audio() override;

    Source *newSource(sound::SoundData *data);
    Source *newSourceFromDecoder(sound::Decoder *decoder, std::string type);
    Source *newSourceFromData(Data *data, std::string type);

    void play(Source *s);
    void stop(Source *s);
    void stopAll();
    void pause(Source *s);

    void setVolume(float v);
    float getVolume() const;

    void setPosition(float x, float y, float z);
    void setVelocity(float x, float y, float z);
    void setOrientation(float fx, float fy, float fz, float ux, float uy, float uz);

    void pump();

    // Internal for Source/worker
    void registerStream(Source *s);
    void unregisterStream(Source *s);
    void unregisterSource(Source *s);
    void notifyWorker();

private:
    void workerMain();

    ALCdevice *device = nullptr;
    ALCcontext *context = nullptr;
    float masterVolume = 1.f;

    std::mutex mutex;
    std::condition_variable cv;
    std::atomic<bool> running{true};
    std::thread worker;
    std::vector<Source *> streamSources;
    std::vector<Source *> allSources;
};

}  // namespace audio
}  // namespace eve
