#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Exception.h"
#include "data/ByteData.h"
#include "font/Font.h"
#include "font/FontData.h"
#include "image/ImageData.h"

#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace {

// FontAwesome private-use icon with a non-empty bitmap in the fixture.
constexpr int kIconCodepoint = 0xF000;

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

bool expectException(const std::function<void()> &fn) {
    try {
        fn();
    } catch (const eve::Exception &) {
        return true;
    }
    return false;
}

eve::font::Font *fontModule() { return eve::font::Font::create(); }

std::unique_ptr<eve::font::FontData> loadFixture(int size = 24) {
    auto raw = readBinaryFile(pathBesideThisSource("fonts/FontAwesome.ttf"));
    if (raw.empty())
        return nullptr;
    eve::data::ByteData data(raw.data(), raw.size());
    return std::unique_ptr<eve::font::FontData>(fontModule()->newFontData(&data, size));
}

}  // namespace

TEST_CASE("font.create") {
    auto *module = fontModule();
    REQUIRE(module != nullptr);
    CHECK_EQ(module->getName(), std::string("Font"));
}

TEST_CASE("font.newFontData.empty") {
    auto *module = fontModule();
    CHECK(expectException([&] { module->newFontData(nullptr); }));
    CHECK(expectException([&] { module->newFontDataFromFile(""); }));
}

TEST_CASE("font.newFontData.garbage") {
    auto *module = fontModule();
    const char garbage[] = "this is not a font";
    eve::data::ByteData bytes(garbage, sizeof(garbage) - 1);
    CHECK(expectException([&] { module->newFontData(&bytes, 16); }));
}

TEST_CASE("font.newFontData.ttf") {
    auto fd = loadFixture(24);
    REQUIRE(fd.get() != nullptr);
    CHECK_EQ(fd->getSize(), 24);
    CHECK(fd->getAscent() > 0.f);
    CHECK(fd->getLineHeight() > 0.f);
    CHECK(fd->getGlyphCount() > 0);
    CHECK(!fd->getFamilyName().empty());
    CHECK(fd->hasGlyph(kIconCodepoint));
    CHECK(!fd->hasGlyph(static_cast<int>(0x4E00)));  // CJK not in fixture
    CHECK(fd->getWidth("\xEF\x80\x80") > 0.f);       // UTF-8 for U+F000
    CHECK(fd->getGlyphAdvance(kIconCodepoint) > 0);

    std::unique_ptr<eve::image::ImageData> glyph(fd->newGlyphImageData(kIconCodepoint));
    REQUIRE(glyph.get() != nullptr);
    CHECK(glyph->getWidth() > 0);
    CHECK(glyph->getHeight() > 0);
    CHECK_EQ(glyph->getFormat(), std::string("RGBA8"));
}

TEST_CASE("font.newFontData.invalidSize") {
    auto raw = readBinaryFile(pathBesideThisSource("fonts/FontAwesome.ttf"));
    REQUIRE(!raw.empty());
    eve::data::ByteData data(raw.data(), raw.size());
    auto *module = fontModule();
    CHECK(expectException([&] { module->newFontData(&data, 0); }));
    CHECK(expectException([&] { module->newFontData(&data, -8); }));
}
