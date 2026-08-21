#pragma once

#include "procgen/Params.h"
#include "procgen/texture/TextureRecipe.h"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::image {
class ImageData;
}

namespace eve::procgen {

/**
 * @brief Full metallic-roughness PBR map set. All maps are caller-owned; call destroy()
 * (or delete each pointer) when done. Maps are derived from one shared
 * displacement field so albedo, normal, height, roughness, metallic and AO
 * line up pixel-for-pixel.
 */
struct PbrTextureSet {
    image::ImageData *albedo    = nullptr;  // RGBA8
    image::ImageData *normal    = nullptr;  // RGBA8 tangent-space normal
    image::ImageData *roughness = nullptr;  // RGBA8, grayscale 0=glossy 1=rough
    image::ImageData *metallic  = nullptr;  // RGBA8, grayscale 0=dielectric 1=metal
    image::ImageData *height    = nullptr;  // RGBA8, grayscale displacement
    image::ImageData *ao        = nullptr;  // RGBA8, grayscale ambient occlusion

    void destroy() {
        delete albedo;
        delete normal;
        delete roughness;
        delete metallic;
        delete height;
        delete ao;
        albedo = normal = roughness = metallic = height = ao = nullptr;
    }
};

/** @brief Recipe returns a full PBR set (caller owns), or nullptr on failure. */
using PbrRecipeFn = std::function<PbrTextureSet *(const Params &params, std::string &error)>;

class PbrRecipeRegistry {
public:
    static PbrRecipeRegistry &instance();

    void registerPbrRecipe(const std::string &id, PbrRecipeFn fn);
    bool has(const std::string &id) const;
    PbrTextureSet *generate(const std::string &id, const Params &params, std::string &error) const;
    std::vector<std::string> list() const;

    void registerPbrBuiltins();

private:
    PbrRecipeRegistry() = default;
    std::unordered_map<std::string, PbrRecipeFn> recipes_;
    bool builtinsRegistered_ = false;
};

/** @brief Build a full PBR set from a texture definition. Caller owns the result. */
PbrTextureSet *generatePbrSet(const TextureRecipeDef &def, const Params &params,
                              std::string &error);

/** @brief Build a grayscale RGBA8 image from per-pixel values in [0,1]. Caller owns. */
image::ImageData *grayscaleImage(const std::vector<float> &values, int w, int h);

}  // namespace eve::procgen
