#pragma once

#include "common/Module.h"
#include "common/Data.h"

#include <AL/alc.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace eve {
namespace sound {
class SoundData;
class Decoder;
}  // namespace sound
namespace audio {

class Source;

/**
 * @brief OpenAL audio module: device management, master listener state, and Source factory.
 * Script: `audio <- eve.Audio();`
 */
class Audio : public Module {
public:
    Module_REG(Audio);

    Audio();
    ~Audio() override;

    /**
     * @brief Creates a static (non-streaming) Source from decoded sound data.
     * @param data Decoded audio (must be non-null; the caller keeps ownership).
     */
    Source *newSource(sound::SoundData *data);
    /** @brief Creates a Source from a decoder; type must be "static" or "stream". */
    Source *newSourceFromDecoder(sound::Decoder *decoder, std::string type);
    /** @brief Creates a Source from raw encoded data; type must be "static" or "stream". */
    Source *newSourceFromData(Data *data, std::string type);

    /** @brief Starts playback of a source (no-op when s is null). */
    void play(Source *s);
    /** @brief Stops playback of a source (no-op when s is null). */
    void stop(Source *s);
    /** @brief Stops every live source registered with this module. */
    void stopAll();
    /** @brief Pauses a source (no-op when s is null). */
    void pause(Source *s);

    /** @brief Sets master volume (clamped to >= 0) applied to the OpenAL listener. */
    void setVolume(float v);
    float getVolume() const;

    /** @brief Sets the listener position in world units. */
    void setPosition(float x, float y, float z);
    /** @brief Sets the listener velocity (used by OpenAL doppler). */
    void setVelocity(float x, float y, float z);
    /** @brief Sets the listener forward and up orientation vectors. */
    void setOrientation(float fx, float fy, float fz, float ux, float uy, float uz);

    /** @brief Advances streaming sources; call once per frame from the main thread. */
    void pump();

    /** @brief Internal: registers a streaming source with the decode worker. */
    void registerStream(Source *s);
    /** @brief Internal: removes a source from worker tracking. */
    void unregisterStream(Source *s);
    /** @brief Internal: removes a source from all module tracking. */
    void unregisterSource(Source *s);
    /** @brief Internal: wakes the decode worker thread. */
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
