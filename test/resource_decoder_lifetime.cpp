#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "medialoader/sound/WaveDecoder.h"
#include "sound/Decoder.h"

#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {
#include "PathBesideSource.h"
EVE_DEFINE_PATH_BESIDE_SOURCE()

std::unique_ptr<eve::sound::Decoder> decoder(zeroerr::TestContext *_ZEROERR_TEST_CONTEXT) {
    std::ifstream input(pathBesideThisSource("fixtures/resource_formats/tone.wav"), std::ios::binary);
    REQUIRE(input.good());
    std::vector<char> bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    auto              provider = std::make_unique<medialoader::WaveDecoder>(bytes.data(), bytes.size(), 1024);
    return std::make_unique<eve::sound::Decoder>(std::move(provider), std::move(bytes));
}

std::vector<char> drain(zeroerr::TestContext *_ZEROERR_TEST_CONTEXT, eve::sound::Decoder &stream) {
    std::vector<char> result;
    for (int count = 0; count < 100 && !stream.isFinished(); ++count) {
        const int size = stream.decode();
        REQUIRE(size >= 0);
        const auto *buffer = static_cast<const char *>(stream.getBuffer());
        result.insert(result.end(), buffer, buffer + size);
        if (size == 0) break;
    }
    REQUIRE_EQ(result.size(), 44100u);
    return result;
}
}  // namespace

TEST_CASE("resourceFormats.sound.clone.originalDestroyedFirst") {
    const auto                           expected = drain(_ZEROERR_TEST_CONTEXT, *decoder(_ZEROERR_TEST_CONTEXT));
    auto                                 original = decoder(_ZEROERR_TEST_CONTEXT);
    std::unique_ptr<eve::sound::Decoder> clone(original->clone());
    original.reset();
    CHECK(drain(_ZEROERR_TEST_CONTEXT, *clone) == expected);
}

TEST_CASE("resourceFormats.sound.clone.cloneDestroyedFirst") {
    const auto expected = drain(_ZEROERR_TEST_CONTEXT, *decoder(_ZEROERR_TEST_CONTEXT));
    auto       original = decoder(_ZEROERR_TEST_CONTEXT);
    {
        std::unique_ptr<eve::sound::Decoder> clone(original->clone());
        CHECK(drain(_ZEROERR_TEST_CONTEXT, *clone) == expected);
    }
    CHECK(drain(_ZEROERR_TEST_CONTEXT, *original) == expected);
}

TEST_CASE("resourceFormats.sound.clone.nestedClonesRetainStorageAndIndependentPosition") {
    const auto expected = drain(_ZEROERR_TEST_CONTEXT, *decoder(_ZEROERR_TEST_CONTEXT));
    auto       original = decoder(_ZEROERR_TEST_CONTEXT);
    REQUIRE(original->decode() > 0);
    std::unique_ptr<eve::sound::Decoder> clone(original->clone());
    std::unique_ptr<eve::sound::Decoder> nested(clone->clone());
    original.reset();
    CHECK(drain(_ZEROERR_TEST_CONTEXT, *clone) == expected);
    clone.reset();
    CHECK(drain(_ZEROERR_TEST_CONTEXT, *nested) == expected);
}
