#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "sound/Sound.h"
#include "sound/Decoder.h"
#include "sound/SoundData.h"
#include "data/ByteData.h"

#include <cstring>
#include <cstdint>
#include <vector>

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

TEST_CASE("sound.newDecoder.wav") {
    auto *sound = eve::sound::Sound::create();
    auto wav = makeSilentWav();
    eve::data::ByteData data(wav.data(), wav.size());
    auto *dec = sound->newDecoder(&data);
    REQUIRE(dec != nullptr);
    CHECK(dec->getSampleRate() == 44100);
    CHECK(dec->getChannelCount() == 1);
    CHECK(dec->getBitDepth() == 16);
    delete dec;
}

TEST_CASE("sound.newSoundData.wav") {
    auto *sound = eve::sound::Sound::create();
    auto wav = makeSilentWav(4410);
    eve::data::ByteData data(wav.data(), wav.size());
    auto *sd = sound->newSoundData(&data);
    REQUIRE(sd != nullptr);
    CHECK(sd->getSampleRate() == 44100);
    CHECK(sd->getChannelCount() == 1);
    CHECK(sd->getBitDepth() == 16);
    CHECK(sd->getSampleCount() == 4410);
    CHECK(sd->getDuration() > 0.09);
    CHECK(sd->getSize() == 4410 * 2);
    delete sd;
}

TEST_CASE("sound.newSoundDataEmpty") {
    auto *sound = eve::sound::Sound::create();
    auto *sd = sound->newSoundDataEmpty(100, 22050, 16, 2);
    REQUIRE(sd != nullptr);
    CHECK(sd->getSampleCount() == 100);
    CHECK(sd->getChannelCount() == 2);
    CHECK(sd->getSize() == 100 * 2 * 2);
    delete sd;
}
