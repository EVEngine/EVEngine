#include <zlib.h>
#include <array>
#include "asset/CanonicalImageCook.h"
#include "asset/SourceTiff.h"
#include "asset/import/AssetImporter.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve;
namespace {
std::vector<std::uint8_t> fixture(unsigned compression, unsigned bits, bool little, bool reverse = false) {
    std::vector<std::uint8_t> raw;
    const auto                put = [&](std::vector<std::uint8_t>& v, unsigned n, unsigned bytes) {
        for (unsigned i = 0; i < bytes; ++i) v.push_back(std::uint8_t(n >> ((little ? i : bytes - 1 - i) * 8)));
    };
    for (auto value : {10u, 20u, 30u, 40u, 90u, 90u, 90u, 90u}) put(raw, bits == 8 ? value : value * 257, bits / 8);
    std::vector<std::uint8_t> encoded;
    if (compression == 1)
        encoded = raw;
    else if (compression == 8) {
        uLongf length = compressBound(uLong(raw.size()));
        encoded.resize(length);
        REQUIRE_EQ(compress2(encoded.data(), &length, raw.data(), uLong(raw.size()), 6), Z_OK);
        encoded.resize(length);
    } else {
        std::vector<unsigned> codes{256};
        for (auto v : raw) codes.push_back(v);
        codes.push_back(257);
        unsigned acc = 0, count = 0;
        for (auto code : codes) {
            acc = (acc << 9) | code;
            count += 9;
            while (count >= 8) {
                count -= 8;
                encoded.push_back(std::uint8_t(acc >> count));
            }
        }
        if (count) encoded.push_back(std::uint8_t(acc << (8 - count)));
    }
    constexpr unsigned        tags = 12, bitsOffset = 8 + 2 + tags * 12 + 4, stripOffset = bitsOffset + 8;
    std::vector<std::uint8_t> out{std::uint8_t(little ? 'I' : 'M'), std::uint8_t(little ? 'I' : 'M')};
    put(out, 42, 2);
    put(out, 8, 4);
    put(out, tags, 2);
    auto tag = [&](unsigned id, unsigned type, unsigned n, unsigned value) {
        put(out, id, 2);
        put(out, type, 2);
        put(out, n, 4);
        if (type == 3 && n == 1) {
            put(out, value, 2);
            put(out, 0, 2);
        } else
            put(out, value, 4);
    };
    tag(256, 4, 1, 2);
    tag(257, 4, 1, 1);
    tag(258, 3, 4, bitsOffset);
    tag(259, 3, 1, compression);
    tag(262, 3, 1, 2);
    tag(273, 4, 1, stripOffset);
    tag(274, 3, 1, reverse ? 2 : 1);
    tag(277, 3, 1, 4);
    tag(278, 4, 1, 1);
    tag(279, 4, 1, unsigned(encoded.size()));
    tag(317, 3, 1, 2);
    tag(338, 3, 1, 0);
    put(out, 0, 4);
    for (int i = 0; i < 4; ++i) put(out, bits, 2);
    out.insert(out.end(), encoded.begin(), encoded.end());
    return out;
}
}  // namespace
TEST_CASE("asset.import.tiffPreservesFourthChannelPredictorAndEndian") {
    for (auto compression : {1u, 5u, 8u})
        for (auto bits : {8u, 16u})
            for (bool little : {false, true}) {
                auto source = fixture(compression, bits, little);
                auto result = asset::detail::decodeTiffRgba8(source, 1024);
                REQUIRE(result.ok());
                REQUIRE_EQ(result.value().rgba, (std::vector<std::uint8_t>{10, 20, 30, 40, 100, 110, 120, 130}));
                auto reversed = asset::detail::decodeTiffRgba8(fixture(compression, bits, little, true), 1024);
                REQUIRE(reversed.ok());
                REQUIRE_EQ(reversed.value().rgba, (std::vector<std::uint8_t>{100, 110, 120, 130, 10, 20, 30, 40}));
            }
}
TEST_CASE("asset.import.tiffRejectsTruncationAndBudgetAndCooksPixels") {
    const auto source = fixture(5, 8, true);
    for (std::size_t n = 0; n < source.size(); ++n) {
        auto truncated = asset::detail::decodeTiffRgba8(std::span(source).first(n), 1024);
        REQUIRE(!truncated.ok());
    }
    REQUIRE(!asset::detail::decodeTiffRgba8(source, 31).ok());
    auto malformed = source;
    malformed[4]   = 255;
    REQUIRE(!asset::detail::decodeTiffRgba8(malformed, 1024).ok());
    asset_import::ImageImportRequest request;
    request.package      = {*PersistentId::parse("11111111-2222-4333-8444-555555555555"), "tiff.test", "1.0.0", {}};
    request.sourceName   = "mask.tif";
    request.encodedBytes = source;
    request.colorSpace   = asset_import::ImageColorSpace::Linear;
    request.usage        = "mask";
    auto imported        = asset_import::prepareImageImport(request);
    REQUIRE(imported.ok());
    const auto path = imported.value().manifest.assets.front().definition;
    for (const auto& entry : imported.value().entries)
        if (entry.path == path) {
            auto cooked = asset::cookCanonicalImageRgba8(entry.bytes, source, 1024);
            REQUIRE(cooked.ok());
            REQUIRE_EQ(cooked.value().bulk.size(), std::size_t(36));
            REQUIRE_EQ(cooked.value().bulk[31], std::uint8_t(40));
            REQUIRE_EQ(cooked.value().bulk[35], std::uint8_t(130));
            const auto json =
                Value::fromJson(std::string(cooked.value().definition.begin(), cooked.value().definition.end()));
            REQUIRE(json.ok());
            REQUIRE_EQ(json.value().getIf<Value::Object>()->at("sourceEncoding").asString(), std::string("tiff"));
        }
}
