#pragma once

/** @file HexNoise.h @brief Deterministic asset-free noise for hex perturbation and generation. */

#include "hexmap/HexMetrics.h"

#include <cstdint>

namespace eve::hexmap {

/**
 * @brief Deterministic four-channel value noise over the XZ plane.
 *
 * The reference hex-map project samples a four-channel `Noise.png` texture.
 * This implementation reproduces the same sampling contract (four independent
 * channels, smooth bilinear interpolation, repeatable for a given seed) without
 * shipping a texture asset, so generated maps stay reproducible from a seed
 * alone.
 *
 * @note Immutable after construction and safe to share across the map and its
 *       mesh builders; it holds no process-global state.
 */
class HexNoise {
public:
    /** @brief Scale of the noise relative to one hex cell (world units per noise unit). */
    static constexpr float kCellScale = 43.3f;

    /**
     * @brief Creates a noise field for `seed`.
     * @param seed Deterministic seed; the same seed always yields the same field.
     */
    explicit HexNoise(std::uint32_t seed = 0u) noexcept : seed_(seed) {}

    /** @brief Reseeds the field. */
    void reset(std::uint32_t seed) noexcept { seed_ = seed; }
    /** @brief The seed this field was created with. */
    [[nodiscard]] std::uint32_t seed() const noexcept { return seed_; }

    /**
     * @brief Samples the four noise channels at a world XZ position.
     * @param worldX World X coordinate.
     * @param worldZ World Z coordinate.
     * @return Channel values in `[0, 1]`; `.x`/`.z` drive XZ perturbation and
     *         `.y` drives vertical elevation perturbation.
     */
    [[nodiscard]] HexVec4 sample(float worldX, float worldZ) const noexcept;

    /**
     * @brief Perturbs a position in XZ by `±kCellPerturbStrength`.
     * @param position Input position; Y is preserved.
     * @return Perturbed position.
     */
    [[nodiscard]] HexVec3 perturb(HexVec3 position) const noexcept;

    /**
     * @brief Vertical perturbation applied to a cell's elevation, `±kElevationPerturbStrength`.
     * @param worldX World X coordinate.
     * @param worldZ World Z coordinate.
     */
    [[nodiscard]] float elevationPerturb(float worldX, float worldZ) const noexcept;

private:
    [[nodiscard]] float lattice(int ix, int iz, int channel) const noexcept;

    std::uint32_t seed_ = 0u;
};

}  // namespace eve::hexmap
