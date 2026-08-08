#pragma once

#include "procgen/Params.h"
#include "procgen/texture/ColorRamp.h"

#include "image/ImageData.h"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::procgen {

/** Recipe returns a new RGBA8 ImageData (caller owns), or nullptr on failure. */
using TextureRecipeFn =
    std::function<image::ImageData *(const Params &params, std::string &error)>;

class TextureRecipeRegistry {
public:
    static TextureRecipeRegistry &instance();

    void registerRecipe(const std::string &id, TextureRecipeFn fn);
    bool has(const std::string &id) const;
    image::ImageData *generate(const std::string &id, const Params &params, std::string &error) const;
    std::vector<std::string> list() const;

    void registerBuiltins();

private:
    TextureRecipeRegistry() = default;
    std::unordered_map<std::string, TextureRecipeFn> recipes_;
    bool builtinsRegistered_ = false;
};

struct TextureGenContext {
    int      width     = 64;
    int      height    = 64;
    uint32_t seed      = 1;
    float    scale     = 4.f;
    int      octaves   = 4;
    int      colors    = 6;
    int      pixelSize = 1;
    bool     seamless  = true;
    float    warpAmp   = 1.f;

    static TextureGenContext fromParams(const Params &params);
};

void paintHeightToImage(image::ImageData &img, const std::vector<float> &height, int w, int h,
                        const ColorRamp &ramp, int bands, int pixelSize);

image::ImageData *heightToNormalImage(const std::vector<float> &height, int w, int h,
                                      float strength, bool seamless);

}  // namespace eve::procgen
