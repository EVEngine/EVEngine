#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "audio/Audio.h"
#include "audio/Source.h"
#include "sound/Sound.h"
#include "data/ByteData.h"
#include "common/Exception.h"

#include <chrono>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

static eve::audio::Audio *tryCreateAudio() {
    try {
        return eve::audio::Audio::create();
    } catch (const eve::Exception &) {
        std::cerr << "N/A: OpenAL device not available\n";
        return nullptr;
    }
}

static std::vector<char> makeSilentWav(int samples = 441) {
    const int dataBytes = samples * 2;
    std::vector<char> buf(44 + dataBytes, 0);
    auto w32 = [&](int off, uint32_t v) { std::memcpy(buf.data() + off, &v, 4); };
    auto w16 = [&](int off, uint16_t v) { std::memcpy(buf.data() + off, &v, 2); };
    std::memcpy(buf.data(), "RIFF", 4);
    w32(4, 36 + dataBytes);
    std::memcpy(buf.data() + 8, "WAVEfmt ", 8);
    w32(16, 16);
    w16(20, 1);
    w16(22, 1);
    w32(24, 44100);
    w32(28, 44100 * 2);
    w16(32, 2);
    w16(34, 16);
    std::memcpy(buf.data() + 36, "data", 4);
    w32(40, dataBytes);
    return buf;
}

TEST_CASE("audio.device") {
    auto *audio = eve::audio::Audio::create();
    REQUIRE(audio != nullptr);
    audio->setVolume(0.5f);
    CHECK(audio->getVolume() == 0.5f);
    audio->setPosition(1.f, 2.f, 3.f);
    audio->pump();
}

TEST_CASE("audio.staticSource.playStop") {
    auto *sound = eve::sound::Sound::create();
    auto *audio = eve::audio::Audio::create();
    auto wav = makeSilentWav(4410);
    eve::data::ByteData data(wav.data(), wav.size());
    auto *sd = sound->newSoundData(&data);
    auto *src = audio->newSource(sd);
    REQUIRE(src != nullptr);
    src->setVolume(0.2f);
    src->setPitch(1.0f);
    src->setLooping(false);
    src->play();
    CHECK(src->isPlaying());
    src->stop();
    CHECK(!src->isPlaying());
    delete src;
    delete sd;
}

TEST_CASE("audio.streamSource.pump") {
    auto *sound = eve::sound::Sound::create();
    auto *audio = eve::audio::Audio::create();
    auto wav = makeSilentWav(44100);
    eve::data::ByteData data(wav.data(), wav.size());
    auto *src = audio->newSourceFromData(&data, "stream");
    REQUIRE(src != nullptr);
    src->setLooping(true);
    src->play();
    for (int i = 0; i < 30; ++i) {
        audio->pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(src->isPlaying());
    src->stop();
    audio->pump();
    delete src;
}

TEST_CASE("audio.spatial.api") {
    auto *audio = eve::audio::Audio::create();
    auto *sound = eve::sound::Sound::create();
    auto wav = makeSilentWav();
    eve::data::ByteData data(wav.data(), wav.size());
    auto *sd = sound->newSoundData(&data);
    auto *src = audio->newSource(sd);
    audio->setPosition(0, 0, 0);
    src->setRelative(false);
    src->setPosition(5.f, 0.f, 0.f);
    src->setAttenuationDistances(1.f, 50.f);
    src->play();
    audio->pump();
    src->stop();
    delete src;
    delete sd;
}

TEST_CASE("audio.stopAll") {
    auto *audio = tryCreateAudio();
    if (!audio)
        return;

    auto *sound = eve::sound::Sound::create();
    auto wav = makeSilentWav(44100);
    eve::data::ByteData data(wav.data(), wav.size());
    auto *sd = sound->newSoundData(&data);
    auto *src1 = audio->newSource(sd);
    auto *src2 = audio->newSource(sd);
    src1->play();
    src2->play();
    audio->pump();
    CHECK(src1->isPlaying());
    CHECK(src2->isPlaying());
    audio->stopAll();
    audio->pump();
    CHECK(!src1->isPlaying());
    CHECK(!src2->isPlaying());
    delete src1;
    delete src2;
    delete sd;
}

TEST_CASE("audio.pause") {
    auto *audio = tryCreateAudio();
    if (!audio)
        return;

    auto *sound = eve::sound::Sound::create();
    auto wav = makeSilentWav(44100);
    eve::data::ByteData data(wav.data(), wav.size());
    auto *sd = sound->newSoundData(&data);
    auto *src = audio->newSource(sd);
    src->play();
    audio->pump();
    CHECK(src->isPlaying());
    audio->pause(src);
    CHECK(!src->isPlaying());
    delete src;
    delete sd;
}

TEST_CASE("audio.source.seekTell") {
    auto *audio = tryCreateAudio();
    if (!audio)
        return;

    auto *sound = eve::sound::Sound::create();
    auto wav = makeSilentWav(44100);
    eve::data::ByteData data(wav.data(), wav.size());
    auto *sd = sound->newSoundData(&data);
    auto *src = audio->newSource(sd);

    CHECK(src->getDuration() > 0.9);
    src->play();
    audio->pump();
    CHECK(src->seek(0.5));
    audio->pump();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    double pos = src->tell();
    // OpenAL tell() can drift after pump/sleep; keep a loose window around seek(0.5).
    CHECK(pos >= 0.40);
    CHECK(pos <= 0.65);
    src->stop();

    delete src;
    delete sd;
}

TEST_CASE("audio.source.pitch") {
    auto *audio = tryCreateAudio();
    if (!audio)
        return;

    auto *sound = eve::sound::Sound::create();
    auto wav = makeSilentWav();
    eve::data::ByteData data(wav.data(), wav.size());
    auto *sd = sound->newSoundData(&data);
    auto *src = audio->newSource(sd);

    src->setPitch(1.5f);
    CHECK(src->getPitch() == 1.5f);
    src->setPitch(0.5f);
    CHECK(src->getPitch() == 0.5f);

    delete src;
    delete sd;
}

TEST_CASE("audio.volumeRoundTrip") {
    auto *audio = tryCreateAudio();
    if (!audio)
        return;

    audio->setVolume(0.75f);
    CHECK(audio->getVolume() == 0.75f);

    auto *sound = eve::sound::Sound::create();
    auto wav = makeSilentWav();
    eve::data::ByteData data(wav.data(), wav.size());
    auto *sd = sound->newSoundData(&data);
    auto *src = audio->newSource(sd);

    src->setVolume(0.3f);
    CHECK(src->getVolume() == 0.3f);

    delete src;
    delete sd;
}
