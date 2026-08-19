#include "Audio.h"
#include "Source.h"

#include "common/Exception.h"
#include "common/StartupTiming.h"
#include "sound/Sound.h"

#include <AL/al.h>
#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <chrono>

namespace eve {
namespace audio {

Module_IMPL(Audio, new Audio());

Audio::Audio() {
    StartupStage stage("audio: OpenAL device/context + worker thread");
    device = alcOpenDevice(nullptr);
    if (!device)
        throw eve::Exception("Could not open OpenAL device");
    context = alcCreateContext(device, nullptr);
    if (!context || !alcMakeContextCurrent(context))
        throw eve::Exception("Could not create OpenAL context");
    alListenerf(AL_GAIN, masterVolume);
    worker = std::thread([this] { workerMain(); });
}

Audio::~Audio() {
    running = false;
    cv.notify_all();
    if (worker.joinable())
        worker.join();

    {
        std::lock_guard<std::mutex> lock(mutex);
        // Sources are owned by callers; just clear tracking.
        streamSources.clear();
        allSources.clear();
    }

    alcMakeContextCurrent(nullptr);
    if (context) {
        alcDestroyContext(context);
        context = nullptr;
    }
    if (device) {
        alcCloseDevice(device);
        device = nullptr;
    }
}

void Audio::workerMain() {
    while (running.load()) {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait_for(lock, std::chrono::milliseconds(10),
                    [this] { return !running.load() || !streamSources.empty(); });
        if (!running.load())
            break;
        // Keep Audio::mutex held while filling. Source::~Source() removes the
        // source from streamSources under the same mutex, so a source cannot be
        // destroyed (and its decoderMutex/pendingMutex freed) while the worker
        // is mid-fill. A snapshot-then-release pattern would let the destructor
        // finish between the snapshot and the fill; the worker would then lock
        // mutexes that no longer exist (std::system_error: mutex lock failed).
        for (Source *s : streamSources) {
            if (s)
                s->fillPendingFromDecoder();
        }
    }
}

void Audio::registerStream(Source *s) {
    std::lock_guard<std::mutex> lock(mutex);
    streamSources.push_back(s);
}

void Audio::unregisterSource(Source *s) {
    std::lock_guard<std::mutex> lock(mutex);
    streamSources.erase(std::remove(streamSources.begin(), streamSources.end(), s), streamSources.end());
    allSources.erase(std::remove(allSources.begin(), allSources.end(), s), allSources.end());
}

void Audio::unregisterStream(Source *s) {
    unregisterSource(s);
}

void Audio::notifyWorker() { cv.notify_all(); }

Source *Audio::newSource(sound::SoundData *data) {
    auto *s = new Source(this, data);
    std::lock_guard<std::mutex> lock(mutex);
    allSources.push_back(s);
    return s;
}

Source *Audio::newSourceFromDecoder(sound::Decoder *decoder, std::string type) {
    if (type != "static" && type != "stream")
        throw eve::Exception("Invalid source type '%s'", type.c_str());
    if (type == "static") {
        auto *sound = sound::Sound::create();
        auto *sd = sound->newSoundDataFromDecoder(decoder);
        auto *s = newSource(sd);
        // SoundData owned by caller eventually; keep sd alive via Source holding pointer only —
        // Source does not own SoundData. Caller must keep SoundData alive.
        // For static-from-decoder convenience, leak prevention: Source should own or we document.
        // Spec: squirrel manages SoundData. Here C++ path: attach as dependency by not deleting.
        // Store sd on Source via staticData without ownership — caller must delete sd after Source.
        // Better: Source holds SoundData* without delete; document. For this factory, transfer:
        // We'll let Source keep staticData pointer; user deletes Source then SoundData.
        return s;
    }
    auto *s = new Source(this, decoder, true);
    std::lock_guard<std::mutex> lock(mutex);
    allSources.push_back(s);
    return s;
}

Source *Audio::newSourceFromData(Data *data, std::string type) {
    if (type != "static" && type != "stream")
        throw eve::Exception("Invalid source type '%s'", type.c_str());
    auto *sound = sound::Sound::create();
    if (type == "static") {
        auto *sd = sound->newSoundData(data);
        return newSource(sd);
    }
    auto *dec = sound->newDecoder(data);
    auto *s = new Source(this, dec, true, true);
    std::lock_guard<std::mutex> lock(mutex);
    allSources.push_back(s);
    return s;
}

void Audio::play(Source *s) {
    if (s)
        s->play();
}
void Audio::stop(Source *s) {
    if (s)
        s->stop();
}
void Audio::pause(Source *s) {
    if (s)
        s->pause();
}
void Audio::stopAll() {
    // Hold the mutex while stopping: Source::~Source() → unregisterSource()
    // blocks on the same mutex, so a concurrently destroyed source cannot be
    // touched here after it has been removed from allSources.
    std::lock_guard<std::mutex> lock(mutex);
    for (Source *s : allSources) {
        if (s)
            s->stop();
    }
}

void Audio::setVolume(float v) {
    masterVolume = std::max(0.f, v);
    alListenerf(AL_GAIN, masterVolume);
}
float Audio::getVolume() const { return masterVolume; }

void Audio::setPosition(float x, float y, float z) { alListener3f(AL_POSITION, x, y, z); }
void Audio::setVelocity(float x, float y, float z) { alListener3f(AL_VELOCITY, x, y, z); }
void Audio::setOrientation(float fx, float fy, float fz, float ux, float uy, float uz) {
    float ori[6] = {fx, fy, fz, ux, uy, uz};
    alListenerfv(AL_ORIENTATION, ori);
}

void Audio::pump() {
    // Same guarantee as workerMain: destruction removes the source under this
    // mutex, so the pointer stays valid for the whole iteration.
    std::lock_guard<std::mutex> lock(mutex);
    for (Source *s : streamSources) {
        if (s)
            s->pump();
    }
}

void Audio::expose(ssq::Table &table) {
    auto cls = table.addClass(name, Audio::create, false);
    expose(cls);

    auto src = table.addClass<Source>(
        "Source", std::function<Source *()>([]() -> Source * { return nullptr; }), true);
    src.addFunc("play", &Source::play);
    src.addFunc("pause", &Source::pause);
    src.addFunc("stop", &Source::stop);
    src.addFunc("isPlaying", &Source::isPlaying);
    src.addFunc("setVolume", &Source::setVolume);
    src.addFunc("getVolume", &Source::getVolume);
    src.addFunc("setPitch", &Source::setPitch);
    src.addFunc("getPitch", &Source::getPitch);
    src.addFunc("setLooping", &Source::setLooping);
    src.addFunc("isLooping", &Source::isLooping);
    src.addFunc("seek", &Source::seek);
    src.addFunc("tell", &Source::tell);
    src.addFunc("getDuration", &Source::getDuration);
    src.addFunc("setPosition", &Source::setPosition);
    src.addFunc("setVelocity", &Source::setVelocity);
    src.addFunc("setDirection", &Source::setDirection);
    src.addFunc("setRelative", &Source::setRelative);
    src.addFunc("setAttenuationDistances", &Source::setAttenuationDistances);
}

void Audio::expose(ssq::Class &cls) {
    cls.addFunc("getName", &Audio::getName);
    cls.addFunc("newSource", &Audio::newSource);
    cls.addFunc("newSourceFromDecoder", &Audio::newSourceFromDecoder);
    cls.addFunc("newSourceFromData", &Audio::newSourceFromData);
    cls.addFunc("play", &Audio::play);
    cls.addFunc("stop", &Audio::stop);
    cls.addFunc("stopAll", &Audio::stopAll);
    cls.addFunc("pause", &Audio::pause);
    cls.addFunc("setVolume", &Audio::setVolume);
    cls.addFunc("getVolume", &Audio::getVolume);
    cls.addFunc("setPosition", &Audio::setPosition);
    cls.addFunc("setVelocity", &Audio::setVelocity);
    cls.addFunc("setOrientation", &Audio::setOrientation);
}

}  // namespace audio
}  // namespace eve
