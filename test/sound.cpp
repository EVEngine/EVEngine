#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "sound/Sound.h"
#include "sound/Decoder.h"
#include "sound/SoundData.h"
#include "data/ByteData.h"
#include "filesystem/FileData.h"
#include "common/Exception.h"

#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::string pathBesideThisSource(const char *filename) {
    std::string here = __FILE__;
    auto slash = here.find_last_of("/\\");
    std::string dir = (slash == std::string::npos) ? std::string(".") : here.substr(0, slash);
    return dir + "/" + filename;
}

std::vector<char> readBinaryFile(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return {};
    return std::vector<char>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

}  // namespace

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

TEST_CASE("sound.decoder.seekRewindIsFinished") {
    auto *sound = eve::sound::Sound::create();
    auto wav = makeSilentWav(44100);
    eve::data::ByteData data(wav.data(), wav.size());
    auto *dec = sound->newDecoder(&data);
    REQUIRE(dec != nullptr);
    CHECK(dec->isSeekable());
    CHECK(!dec->isFinished());
    CHECK(dec->getDuration() > 0.9);

    CHECK(dec->decode() > 0);
    CHECK(dec->seek(0.5));
    CHECK(!dec->isFinished());

    while (!dec->isFinished()) {
        if (dec->decode() <= 0)
            break;
    }
    CHECK(dec->isFinished());

    CHECK(dec->rewind());
    CHECK(!dec->isFinished());
    CHECK(dec->decode() > 0);
    delete dec;
}

TEST_CASE("sound.decoder.clone") {
    auto *sound = eve::sound::Sound::create();
    auto wav = makeSilentWav(4410);
    eve::data::ByteData data(wav.data(), wav.size());
    auto *dec = sound->newDecoder(&data);
    REQUIRE(dec != nullptr);

    auto *cloned = dec->clone();
    REQUIRE(cloned != nullptr);
    CHECK(cloned->getSampleRate() == dec->getSampleRate());
    CHECK(cloned->getChannelCount() == dec->getChannelCount());
    CHECK(cloned->getBitDepth() == dec->getBitDepth());

    dec->decode();
    CHECK(cloned->decode() > 0);
    delete cloned;
    delete dec;
}

TEST_CASE("sound.newSoundDataFromDecoder") {
    auto *sound = eve::sound::Sound::create();
    auto wav = makeSilentWav(4410);
    eve::data::ByteData data(wav.data(), wav.size());
    auto *dec = sound->newDecoder(&data);
    REQUIRE(dec != nullptr);

    auto *sd = sound->newSoundDataFromDecoder(dec);
    REQUIRE(sd != nullptr);
    CHECK(sd->getSampleCount() == 4410);
    CHECK(sd->getSampleRate() == 44100);
    CHECK(sd->getChannelCount() == 1);
    CHECK(sd->getBitDepth() == 16);
    CHECK(sd->getSize() == 4410 * 2);
    delete sd;
    delete dec;
}

TEST_CASE("sound.invalidData") {
    auto *sound = eve::sound::Sound::create();

    try {
        sound->newDecoder(nullptr);
        CHECK(false);
    } catch (const eve::Exception &) {
        CHECK(true);
    }

    try {
        const char garbage[] = "not a sound file";
        eve::data::ByteData bad(garbage, sizeof(garbage) - 1);
        sound->newDecoder(&bad);
        CHECK(false);
    } catch (const eve::Exception &) {
        CHECK(true);
    }

    try {
        sound->newSoundDataFromDecoder(nullptr);
        CHECK(false);
    } catch (const eve::Exception &) {
        CHECK(true);
    }
}

TEST_CASE("sound.modplug.midi.decode") {
    const std::string path = pathBesideThisSource("angel.mid");
    auto bytes = readBinaryFile(path);
    REQUIRE(!bytes.empty());
    REQUIRE(bytes.size() >= 4);
    CHECK(std::memcmp(bytes.data(), "MThd", 4) == 0);

    eve::filesystem::FileData fd("angel.mid", bytes.size());
    std::memcpy(fd.getData(), bytes.data(), bytes.size());

    auto *sound = eve::sound::Sound::create();
    eve::sound::Decoder *dec = nullptr;
    try {
        dec = sound->newDecoder(&fd);
    } catch (const eve::Exception &e) {
        std::cerr << "MIDI decode failed: " << e.what() << "\n";
        CHECK(false);
        return;
    }
    REQUIRE(dec != nullptr);
    CHECK(dec->getSampleRate() > 0);
    CHECK(dec->getChannelCount() == 2);
    CHECK(dec->getBitDepth() == 16);
    CHECK(dec->getDuration() > 0.0);

    int decoded = 0;
    int64_t absSum = 0;
    int samplesChecked = 0;
    while (!dec->isFinished()) {
        int n = dec->decode();
        if (n <= 0)
            break;
        decoded += n;
        auto *buf = static_cast<const int16_t *>(dec->getBuffer());
        int samples = n / 2;
        for (int i = 0; i < samples && samplesChecked < 44100 * 4; ++i) {
            absSum += buf[i] < 0 ? -buf[i] : buf[i];
            ++samplesChecked;
        }
        if (samplesChecked >= 44100 * 4)
            break;
    }

    std::cerr << "MIDI decode: duration=" << dec->getDuration()
              << "s bytes=" << decoded << " absSum=" << absSum << "\n";
    CHECK(decoded > 0);
    // ModPlug may load MIDI but output silence without instrument patches.
    if (absSum == 0)
        std::cerr << "WARNING: MIDI decoded to silence (likely no instruments)\n";

    // Optional: EVENGINE_DUMP_MIDI=1 writes ~3s WAV for manual afplay.
    if (const char *dump = std::getenv("EVENGINE_DUMP_MIDI"); dump && dump[0] == '1') {
        eve::filesystem::FileData fd2("angel.mid", bytes.size());
        std::memcpy(fd2.getData(), bytes.data(), bytes.size());
        auto *dec2 = sound->newDecoder(&fd2);
        const int rate = dec2->getSampleRate();
        const int ch = dec2->getChannelCount();
        const int wantBytes = rate * ch * 2 * 3;
        std::vector<uint8_t> pcm;
        pcm.reserve(static_cast<size_t>(wantBytes));
        while ((int)pcm.size() < wantBytes && !dec2->isFinished()) {
            int n = dec2->decode();
            if (n <= 0)
                break;
            auto *buf = static_cast<const uint8_t *>(dec2->getBuffer());
            pcm.insert(pcm.end(), buf, buf + n);
        }
        delete dec2;
        if (pcm.size() > static_cast<size_t>(wantBytes))
            pcm.resize(static_cast<size_t>(wantBytes));
        const uint32_t dataBytes = static_cast<uint32_t>(pcm.size());
        std::vector<char> wav(44 + pcm.size());
        auto w32 = [&](int off, uint32_t v) { std::memcpy(wav.data() + off, &v, 4); };
        auto w16 = [&](int off, uint16_t v) { std::memcpy(wav.data() + off, &v, 2); };
        std::memcpy(wav.data(), "RIFF", 4);
        w32(4, 36 + dataBytes);
        std::memcpy(wav.data() + 8, "WAVEfmt ", 8);
        w32(16, 16);
        w16(20, 1);
        w16(22, static_cast<uint16_t>(ch));
        w32(24, static_cast<uint32_t>(rate));
        w32(28, static_cast<uint32_t>(rate * ch * 2));
        w16(32, static_cast<uint16_t>(ch * 2));
        w16(34, 16);
        std::memcpy(wav.data() + 36, "data", 4);
        w32(40, dataBytes);
        std::memcpy(wav.data() + 44, pcm.data(), pcm.size());
        const std::string out = std::string(EVENGINE_TEST_BINARY_DIR) + "/angel_modplug.wav";
        std::ofstream ofs(out, std::ios::binary);
        ofs.write(wav.data(), static_cast<std::streamsize>(wav.size()));
        std::cerr << "Wrote " << out << " (" << pcm.size() << " pcm bytes)\n";
    }

    delete dec;
}
