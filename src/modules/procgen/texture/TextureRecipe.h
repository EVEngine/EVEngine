#pragma once

#include "procgen/Params.h"
#include "procgen/texture/ColorRamp.h"
#include "procgen/texture/NoiseField.h"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::image {
class ImageData;
}

namespace eve::procgen {

/** @brief Recipe returns a new RGBA8 ImageData (caller owns), or nullptr on failure. */
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

/** @brief Sample a height function over the full [w×h] grid into `height` (clamped to [0,1]). */
void fillHeightField(const TextureGenContext &ctx,
                     const std::function<float(float, float, const NoiseField &)> &fn,
                     std::vector<float> &height);

image::ImageData *heightToNormalImage(const std::vector<float> &height, int w, int h,
                                      float strength, bool seamless);

/**
 * @brief PBR surface defaults for a generated material. Every map in a PBR set is
 * derived from the same displacement field (see TextureRecipeDef).
 */
struct PbrParams {
    float roughnessLow      = 0.45f;  // roughness at the low end of displacement
    float roughnessHigh     = 0.85f;  // roughness at the high end of displacement
    float metallic          = 0.f;    // uniform metalness (0..1)
    float normalStrength    = 4.f;    // normal-map height strength
    float aoStrength        = 1.f;    // cavity/ambient-occlusion strength
    float heightStrength    = 1.f;    // height-map displacement scale

    static PbrParams fromParams(const Params &params);
};

/**
 * @brief A procedural texture is fully described by an albedo ramp + a displacement
 * field + PBR knobs. From one definition both an albedo recipe (ImageData) and
 * a full PBR set (albedo/normal/roughness/metallic/height/ao) are produced.
 */
struct TextureRecipeDef {
    std::string id;  // albedo recipe id, e.g. "tex.marble"
    ColorRamp   albedo;
    std::function<float(float, float, const NoiseField &)> height;  // clamped to [0,1]
    PbrParams   pbr;
};

/** @brief All built-in texture definitions (albedo + PBR). Populated lazily. */
const std::vector<TextureRecipeDef> &builtinTextureDefs();

}  // namespace eve::procgen
