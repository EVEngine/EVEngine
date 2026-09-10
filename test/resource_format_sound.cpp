#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "data/ByteData.h"
#include "sound/Decoder.h"
#include "sound/Sound.h"
#include "sound/SoundData.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {
#include "PathBesideSource.h"
EVE_DEFINE_PATH_BESIDE_SOURCE()

std::vector<char> tone(zeroerr::TestContext *_ZEROERR_TEST_CONTEXT, const std::string &extension) {
    std::ifstream input(pathBesideThisSource(("fixtures/resource_formats/tone." + extension).c_str()),
                        std::ios::binary);
    REQUIRE(input.good());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void decodeTone(zeroerr::TestContext *_ZEROERR_TEST_CONTEXT, const std::string &extension, bool lossless) {
    const auto                             bytes = tone(_ZEROERR_TEST_CONTEXT, extension);
    eve::data::ByteData                    source(bytes.data(), bytes.size());
    auto                                  *module = eve::sound::Sound::create();
    std::unique_ptr<eve::sound::SoundData> decoded(module->newSoundData(&source));
    REQUIRE(decoded != nullptr);
    REQUIRE_EQ(decoded->getSampleRate(), 44100);
    REQUIRE_EQ(decoded->getChannelCount(), 2);
    REQUIRE_EQ(decoded->getBitDepth(), 16);
    REQUIRE(decoded->getSampleCount() >= 11025);
    CHECK(decoded->getDuration() < 0.32);
    const auto *samples = static_cast<const int16_t *>(decoded->getData());
    // Distinct frequencies/amplitudes catch channel swaps and silent output.
    double error = 0;
    for (int n = 0; n < 11025; ++n) {
        const auto left  = std::round(12000 * std::sin(2 * 3.14159265358979323846 * 440 * n / 44100));
        const auto right = std::round(6000 * std::sin(2 * 3.14159265358979323846 * 880 * n / 44100));
        error += std::fabs(samples[n * 2] - left) + std::fabs(samples[n * 2 + 1] - right);
    }
    CHECK(error / 22050 < (lossless ? 0.01 : 800.0));
    std::unique_ptr<eve::sound::Decoder> stream(module->newDecoder(&source, 1024));
    std::vector<char>                    streaming;
    for (int count = 0; count < 1000 && !stream->isFinished(); ++count) {
        const int size = stream->decode();
        REQUIRE(size >= 0);
        const auto *buffer = static_cast<const char *>(stream->getBuffer());
        streaming.insert(streaming.end(), buffer, buffer + size);
        if (size == 0) break;
    }
    REQUIRE_EQ(streaming.size(), decoded->getSize());
    CHECK_EQ(std::memcmp(streaming.data(), decoded->getData(), streaming.size()), 0);
    REQUIRE(stream->rewind());
    const int firstChunk = stream->decode();
    REQUIRE(firstChunk > 0);
    CHECK_EQ(std::memcmp(stream->getBuffer(), streaming.data(), static_cast<size_t>(firstChunk)), 0);
}
}  // namespace

TEST_CASE("resourceFormats.sound.wav.independentPcmAndStreaming") { decodeTone(_ZEROERR_TEST_CONTEXT, "wav", true); }
TEST_CASE("resourceFormats.sound.flac.independentPcmAndStreaming") { decodeTone(_ZEROERR_TEST_CONTEXT, "flac", true); }
TEST_CASE("resourceFormats.sound.ogg.independentPcmAndStreaming") { decodeTone(_ZEROERR_TEST_CONTEXT, "ogg", false); }
TEST_CASE("resourceFormats.sound.mp3.independentPcmAndStreaming") { decodeTone(_ZEROERR_TEST_CONTEXT, "mp3", false); }
