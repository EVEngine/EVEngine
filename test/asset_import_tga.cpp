#include "asset/CanonicalImageCook.h"
#include "asset/SourceTga.h"
#include "asset/import/AssetImporter.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve;
namespace {
std::vector<std::uint8_t> fixture(bool rle, unsigned channels, unsigned orientation) {
    std::vector<std::uint8_t> bytes(18, 0);
    bytes[2]  = rle ? 10 : 2;
    bytes[12] = 2;
    bytes[14] = 2;
    bytes[16] = std::uint8_t(channels * 8);
    bytes[17] = std::uint8_t(orientation | (channels == 4 ? 8 : 0));
    if (rle) bytes.push_back(0x81);
    const unsigned copies = rle ? 1 : 2;
    for (unsigned i = 0; i < copies; ++i) {
        bytes.insert(bytes.end(), {30, 20, 10});
        if (channels == 4) bytes.push_back(40);
    }
    if (rle) bytes.push_back(1);
    bytes.insert(bytes.end(), {70, 60, 50});
    if (channels == 4) bytes.push_back(80);
    bytes.insert(bytes.end(), {110, 100, 90});
    if (channels == 4) bytes.push_back(120);
    return bytes;
}
}  // namespace
TEST_CASE("asset.import.tgaRawAndRlePreserveOrientationAndAlpha") {
    for (bool rle : {false, true})
        for (unsigned channels : {3u, 4u})
            for (unsigned orientation : {0u, 16u, 32u, 48u}) {
                auto decoded = asset::detail::decodeTgaRgba8(fixture(rle, channels, orientation), 1024);
                REQUIRE(decoded.ok());
                const std::vector<std::uint8_t> source{10, 20, 30, 40, 10, 20,  30,  40,
                                                       50, 60, 70, 80, 90, 100, 110, 120};
                for (unsigned i = 0; i < 4; ++i) {
                    const auto x = (orientation & 16) ? 1 - i % 2 : i % 2, y = (orientation & 32) ? i / 2 : 1 - i / 2;
                    for (unsigned c = 0; c < 4; ++c)
                        REQUIRE_EQ(decoded.value().rgba[(y * 2 + x) * 4 + c],
                                   channels == 3 && c == 3 ? std::uint8_t(255) : source[i * 4 + c]);
                }
            }
}
TEST_CASE("asset.import.tgaRejectsBadPacketsAndCooksLinearPixels") {
    auto source = fixture(true, 4, 32);
    for (std::size_t size = 0; size < source.size(); ++size)
        REQUIRE(!asset::detail::decodeTgaRgba8(std::span(source).first(size), 1024).ok());
    REQUIRE(!asset::detail::decodeTgaRgba8(source, 39).ok());
    auto overflow = source;
    overflow[18]  = 255;
    REQUIRE(!asset::detail::decodeTgaRgba8(overflow, 1024).ok());
    auto interleaved = source;
    interleaved[17] |= 64;
    REQUIRE(!asset::detail::decodeTgaRgba8(interleaved, 1024).ok());
    auto footer = source;
    footer.insert(footer.end(), {255, 255, 255, 255, 0, 0, 0, 0});
    const std::string signature("TRUEVISION-XFILE.\0", 18);
    footer.insert(footer.end(), signature.begin(), signature.end());
    REQUIRE(!asset::detail::decodeTgaRgba8(footer, 1024).ok());
    asset_import::ImageImportRequest request;
    request.package      = {*PersistentId::parse("11111111-2222-4333-8444-555555555555"), "tga.test", "1.0.0", {}};
    request.sourceName   = "mask.tga";
    request.encodedBytes = source;
    request.colorSpace   = asset_import::ImageColorSpace::Linear;
    request.usage        = "mask";
    auto imported        = asset_import::prepareImageImport(request);
    REQUIRE(imported.ok());
    bool checked = false;
    for (const auto& entry : imported.value().entries)
        if (entry.path == imported.value().manifest.assets.front().definition) {
            auto cooked = asset::cookCanonicalImageRgba8(entry.bytes, source, 1024);
            REQUIRE(cooked.ok());
            REQUIRE_EQ(cooked.value().bulk[28], std::uint8_t(10));
            REQUIRE_EQ(cooked.value().bulk[31], std::uint8_t(40));
            REQUIRE_EQ(cooked.value().bulk[43], std::uint8_t(120));
            checked = true;
        }
    REQUIRE(checked);
}
