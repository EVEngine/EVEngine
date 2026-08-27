#pragma once

#include "procgen/Params.h"
#include "procgen/texture/TextureRecipe.h"

#include <cstdint>
#include <functional>
#include <memory>
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
    /** @brief Releases all generated maps when the owning set is destroyed. */
    ~PbrTextureSet();
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

/** @brief Recipe returns a newly owned full PBR set, or null on failure. */
using PbrRecipeFn =
    std::function<std::unique_ptr<PbrTextureSet>(const Params &params, std::string &error)>;

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
    /**
     * @brief Generates a PBR map set and transfers its unique ownership to the caller.
     * @param id Stable recipe id.
     * @param params Validated recipe parameters.
     * @param error Receives a human-readable failure description when generation fails.
     * @return The newly owned set, or null when the recipe is unknown or generation fails.
     * @ownership The returned set and all maps it owns belong to the caller.
     */
    [[nodiscard]] std::unique_ptr<PbrTextureSet> generate(
        const std::string &id, const Params &params, std::string &error) const;
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

/** @brief Builds a full PBR set from a texture definition and returns unique ownership. */
[[nodiscard]] std::unique_ptr<PbrTextureSet> generatePbrSet(
    const TextureRecipeDef &def, const Params &params, std::string &error);

/** @brief Builds a grayscale RGBA8 image and returns unique ownership to the caller. */
[[nodiscard]] std::unique_ptr<image::ImageData> grayscaleImage(
    const std::vector<float> &values, int w, int h);

}  // namespace eve::procgen
