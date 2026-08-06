#pragma once

#include "common/Resource.h"

#include <cstdint>
#include <string>
#include <vector>

struct FT_FaceRec_;
typedef struct FT_FaceRec_ *FT_Face;

namespace eve {
namespace image {
class ImageData;
}

namespace font {

/**
 * CPU-side decoded font face (FreeType FT_Face + owned font bytes).
 * Does not upload to GPU — rasterize glyphs to ImageData for graphics::Texture.
 */
class FontData : public Resource {
public:
    FontData(std::vector<uint8_t> bytes, int pixelSize, std::string uri = "");
    ~FontData() override;

    int getSize() const;
    float getAscent() const;
    float getDescent() const;
    float getLineHeight() const;
    float getBaseline() const;

    std::string getFamilyName() const;
    std::string getStyleName() const;
    int getGlyphCount() const;

    bool hasGlyph(int codepoint) const;
    bool hasGlyphs(std::string text) const;
    float getWidth(std::string text) const;
    float getKerning(int leftCodepoint, int rightCodepoint) const;

    int getGlyphWidth(int codepoint) const;
    int getGlyphHeight(int codepoint) const;
    int getGlyphBearingX(int codepoint) const;
    int getGlyphBearingY(int codepoint) const;
    int getGlyphAdvance(int codepoint) const;

    /**
     * Rasterize one glyph to RGBA8 ImageData (RGB white, A from FreeType gray).
     * Returns a 0x0 ImageData if the codepoint is missing / empty.
     */
    image::ImageData *newGlyphImageData(int codepoint);

private:
    struct GlyphCache {
        int width = 0;
        int height = 0;
        int bearingX = 0;
        int bearingY = 0;
        int advance = 0;
        bool loaded = false;
        bool empty = true;
    };

    const GlyphCache &ensureGlyph(int codepoint) const;
    unsigned int glyphIndex(int codepoint) const;

    std::vector<uint8_t> bytes;
    FT_Face face = nullptr;
    int pixelSize = 16;
    mutable std::vector<GlyphCache> glyphCache;  // indexed by glyph index
};

}  // namespace font
}  // namespace eve
