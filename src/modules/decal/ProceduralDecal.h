#pragma once

#include "common/Result.h"

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace eve::decal {

/** @brief Built-in procedural masks used by a procedural decal layer. */
enum class DecalPattern : std::uint8_t {
    Spots,
    Cracks,
    Streaks,
    Puddle,
    Grunge,
};

/**
 * @brief One independently art-directed layer of a procedural decal recipe.
 *
 * Layers are pure authoring values. They own no renderer resources and are safe
 * to serialize, copy, and evaluate on a worker thread.
 */
struct ProceduralDecalLayer {
    bool                 enabled  = true;
    DecalPattern         pattern  = DecalPattern::Grunge;
    std::uint32_t        seed     = 1;
    int                  sourcePatternId = -1;
    bool                 invertMask = false;
    float                amount   = 0.65f;
    float                scale    = 5.f;
    float                rotation = 0.f;
    float                blur     = 0.08f;
    float                contrast = 1.4f;
    std::array<float, 3> color{0.35f, 0.12f, 0.06f};
    float                colorVariation     = 0.15f;
    float                hueVariation       = 0.f;
    float                roughness          = 0.7f;
    float                roughnessVariation = 0.1f;
    float                metallic           = 0.f;
    float                emissive           = 0.f;
    float                normalStrength     = 2.f;
    float                normalSoftness     = 0.08f;
    float                normalTrim         = 0.f;
    float                normalThickness    = 1.f;
    int                  normalStyle        = 0;
    float                height             = 0.5f;
};

/**
 * @brief Versioned two-layer authoring recipe analogous to Procedural Decal's
 * Standard graph and material-instance presets.
 */
struct ProceduralDecalRecipe {
    static constexpr std::uint32_t kSchemaVersion = 2;
    std::uint32_t                  schemaVersion  = kSchemaVersion;
    int                            width          = 256;
    int                            height         = 256;
    std::uint32_t                  seed           = 1;
    ProceduralDecalLayer             layerA{};
    ProceduralDecalLayer             layerB{};
    float                          layerHeightBlend = 0.5f;
    float                          layerBalance     = 0.5f;
    int                            layerBlendMode   = 0;
    float                          opacity          = 1.f;
};

/**
 * @brief Owning, renderer-neutral output of one decal bake.
 *
 * All arrays are tightly packed RGBA8. `params` stores roughness, metallic,
 * emissive, and height. Its RGB layout is accepted directly
 * by the existing decal pass; alpha remains available to height-aware tools.
 * @ownership The value owns all output arrays; no renderer resource is retained.
 */
struct ProceduralDecalBake {
    int                       width  = 0;
    int                       height = 0;
    std::vector<std::uint8_t> albedo;
    std::vector<std::uint8_t> normal;
    std::vector<std::uint8_t> params;
};

/**
 * @brief Build a deterministic preset recipe.
 * @param name Stable preset family: blood-wet, blood-dried, damage, dirt,
 * rust, puddle, paint, moss, mold, or lichen.
 * @param seed Named recipe seed; equal inputs are byte-identical.
 * @return Recipe or InvalidArgument for an unknown preset.
 * @thread Thread-safe; no global mutable state is accessed.
 * @reentrancy Does not invoke callbacks.
 */
[[nodiscard]] Result<ProceduralDecalRecipe> proceduralDecalPreset(std::string_view name, std::uint32_t seed = 1);

/**
 * @brief Import one Substance `.sbsprs` preset into the native version-two recipe.
 * @param xml UTF-8 `sbspresets` document; the importer consumes known Procedural Decal inputs and ignores
 * unknown forward-compatible inputs.
 * @return A fully owned recipe, or a structured parse/validation failure without publishing state.
 * @cost Linear in the preset text size; intended for asset admission, never per frame.
 * @thread Thread-safe; no filesystem, renderer, or global state is accessed.
 * @reentrancy Does not invoke callbacks.
 */
[[nodiscard]] Result<ProceduralDecalRecipe> importProceduralDecalSbsprs(std::string_view xml);

/**
 * @brief Bake a two-layer decal recipe into renderer-ready texture bytes.
 * @param recipe Fully specified version-two recipe.
 * @return Three owning RGBA8 maps, or a structured validation failure.
 * @cost Linear in pixel count and blur radius; amortize at authoring or load time.
 * @thread Thread-safe; no renderer calls or shared caches.
 * @reentrancy Does not invoke callbacks.
 */
[[nodiscard]] Result<ProceduralDecalBake> bakeProceduralDecal(const ProceduralDecalRecipe& recipe);

}  // namespace eve::decal
