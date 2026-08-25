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

    /** @brief Return the generated albedo image. @return Borrowed image. */
    image::ImageData *getAlbedo() const { return albedo; }
    /** @brief Return the generated normal image. @return Borrowed image. */
    image::ImageData *getNormal() const { return normal; }
    /** @brief Return the generated roughness image. @return Borrowed image. */
    image::ImageData *getRoughness() const { return roughness; }
    /** @brief Return the generated metallic image. @return Borrowed image. */
    image::ImageData *getMetallic() const { return metallic; }
    /** @brief Return the generated height image. @return Borrowed image. */
    image::ImageData *getHeight() const { return height; }
    /** @brief Return the generated ambient-occlusion image. @return Borrowed image. */
    image::ImageData *getAo() const { return ao; }
    /** @brief Deletes all maps and nulls the pointers (defined in PbrMaterial.cpp). */
    void destroy();
};

/** @brief Recipe returns a full PBR set (caller owns), or nullptr on failure. */
using PbrRecipeFn = std::function<PbrTextureSet *(const Params &params, std::string &error)>;

class PbrRecipeRegistry {
public:
    /** @brief Access the process-wide PBR registry. @return Registry instance. */
    static PbrRecipeRegistry &instance();

    /** @brief Register a recipe without metadata. @param id Recipe id. @param fn Generator callback. */
    void registerPbrRecipe(const std::string &id, PbrRecipeFn fn);
    /** @brief Register a recipe with metadata. @param descriptor Recipe schema. @param fn Generator callback. */
    void registerPbrRecipe(RecipeDescriptor descriptor, PbrRecipeFn fn);
    /** @brief Test whether a recipe exists. @param id Recipe id. @return True when registered. */
    bool has(const std::string &id) const;
    /** @brief Generate a PBR set. @param id Recipe id. @param params Values. @param error Failure text. @return Caller-owned set or nullptr. */
    PbrTextureSet *generate(const std::string &id, const Params &params, std::string &error) const;
    /** @brief List recipe ids. @return Sorted ids. */
    std::vector<std::string> list() const;
    /** @brief Look up recipe metadata. @param id Recipe id. @return Registry-owned schema or nullptr. */
    const RecipeDescriptor *descriptor(const std::string &id) const;
    /** @brief Fill missing values from metadata. @param id Recipe id. @param params Values to update. @return False for an unknown recipe. */
    bool applyDefaults(const std::string &id, Params &params) const;

    /** @brief Register engine-provided PBR recipes once. */
    void registerPbrBuiltins();

private:
    struct Entry {
        PbrRecipeFn fn;
        RecipeDescriptor descriptor;
    };
    PbrRecipeRegistry() = default;
    std::unordered_map<std::string, Entry> recipes_;
    bool builtinsRegistered_ = false;
};

/** @brief Build a full PBR set from a texture definition. Caller owns the result. */
PbrTextureSet *generatePbrSet(const TextureRecipeDef &def, const Params &params,
                              std::string &error);

/** @brief Build a grayscale RGBA8 image from per-pixel values in [0,1]. Caller owns. */
image::ImageData *grayscaleImage(const std::vector<float> &values, int w, int h);

}  // namespace eve::procgen
