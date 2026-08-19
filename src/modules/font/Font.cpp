#include "font/Font.h"

#include "common/Data.h"
#include "common/Exception.h"
#include "common/Resource.h"
#include "filesystem/FileData.h"
#include "filesystem/Filesystem.h"
#include "image/ImageData.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace eve {
namespace font {

Module_IMPL(Font, new Font());

Font::Font() = default;
Font::~Font() = default;

FontData *Font::newFontData(Data *data, int size) {
    if (data == nullptr || data->getData() == nullptr || data->getSize() == 0)
        throw eve::Exception("Cannot decode empty font data");

    const auto *ptr = static_cast<const uint8_t *>(data->getData());
    std::vector<uint8_t> bytes(ptr, ptr + data->getSize());

    std::string uri;
    if (auto *fd = dynamic_cast<filesystem::FileData *>(data))
        uri = "file://" + fd->getFilename();

    return new FontData(std::move(bytes), size, std::move(uri));
}

FontData *Font::newFontDataFromFile(std::string path, int size) {
    if (path.empty())
        throw eve::Exception("Font::newFontDataFromFile: empty path");

    // Route through the unified resource cache: one decoded face per
    // (path, size) pair, refreshed in place on file change.
    const std::string key = eve::ResourceManager::makeKey(path, "size=" + std::to_string(size));
    eve::Resource *resource = eve::ResourceManager::getInstance().get(key);
    if (!resource)
        throw eve::Exception("Could not load font file: %s", path.c_str());
    return static_cast<FontData *>(resource);
}

void Font::expose(ssq::Table &table) {
    auto cls = table.addClass(name, Font::create, false);
    expose(cls);

    auto fd = table.addClass<FontData>(
        "FontData", std::function<FontData *()>([]() -> FontData * { return nullptr; }), true);
    fd.addFunc("getSize", &FontData::getSize);
    fd.addFunc("getAscent", &FontData::getAscent);
    fd.addFunc("getDescent", &FontData::getDescent);
    fd.addFunc("getLineHeight", &FontData::getLineHeight);
    fd.addFunc("getBaseline", &FontData::getBaseline);
    fd.addFunc("getFamilyName", &FontData::getFamilyName);
    fd.addFunc("getStyleName", &FontData::getStyleName);
    fd.addFunc("getGlyphCount", &FontData::getGlyphCount);
    fd.addFunc("hasGlyph", &FontData::hasGlyph);
    fd.addFunc("hasGlyphs", &FontData::hasGlyphs);
    fd.addFunc("getWidth", &FontData::getWidth);
    fd.addFunc("getKerning", &FontData::getKerning);
    fd.addFunc("getGlyphWidth", &FontData::getGlyphWidth);
    fd.addFunc("getGlyphHeight", &FontData::getGlyphHeight);
    fd.addFunc("getGlyphBearingX", &FontData::getGlyphBearingX);
    fd.addFunc("getGlyphBearingY", &FontData::getGlyphBearingY);
    fd.addFunc("getGlyphAdvance", &FontData::getGlyphAdvance);
    fd.addFunc("newGlyphImageData", &FontData::newGlyphImageData);

    // Minimal ImageData surface so newGlyphImageData is usable from scripts.
    // Full image decode APIs remain on eve.Image.
    auto img = table.addClass<image::ImageData>(
        "ImageData", std::function<image::ImageData *()>([]() -> image::ImageData * { return nullptr; }),
        true);
    img.addFunc("getWidth", &image::ImageData::getWidth);
    img.addFunc("getHeight", &image::ImageData::getHeight);
    img.addFunc("getFormat", &image::ImageData::getFormat);
    img.addFunc("getSize", &image::ImageData::getSize);
    img.addFunc("rotate", &image::ImageData::rotate);
}

void Font::expose(ssq::Class &cls) {
    cls.addFunc("getName", &Font::getName);
    cls.addFunc("newFontData", &Font::newFontData);
    cls.addFunc("newFontDataFromFile", &Font::newFontDataFromFile);
}

}  // namespace font
}  // namespace eve
