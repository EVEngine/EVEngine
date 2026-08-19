#pragma once

#include "common/Module.h"
#include "font/FontData.h"

#include <string>

namespace eve {
class Data;

namespace font {

/**
 * @brief Resource module for decoding TrueType / OpenType fonts via FreeType.
 * Produces FontData (CPU metrics + glyph raster); GPU upload/draw is graphics'.
 */
class Font : public Module {
public:
    Module_REG(Font);

    Font();
    ~Font() override;

    /**
     * @brief Decode a font from in-memory bytes at the given pixel size.
     * @param data Encoded font bytes (e.g. FileData / ByteData).
     * @param size Pixel height (default 16).
     */
    FontData *newFontData(Data *data, int size = 16);

    /**
     * @brief Decode a font from a VFS path (physfs) at the given pixel size.
     */
    FontData *newFontDataFromFile(std::string path, int size = 16);
};

}  // namespace font
}  // namespace eve
