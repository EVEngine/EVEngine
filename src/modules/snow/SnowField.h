#pragma once

#include <cstdint>
#include <vector>

namespace eve::snow {

/**
 * @brief Interactive snow depth field: a dense [0,1] float grid.
 *
 * 1.0 = full snow cover, 0.0 = bare ground. The grid drives both the
 * terrain-surface displacement (snow thickness) and the POM height map
 * (R channel, white = raised), so footprints and impact craters deform the
 * rendered snow surface instead of being a flat decal.
 *
 * Pure CPU data (no graphics / procgen includes): render and heightmap
 * bridging live in the Snow module (Snow.h).
 */
class SnowField {
public:
    SnowField() = default;
    SnowField(int width, int height);

    void resize(int width, int height);
    int  getWidth() const { return width_; }
    int  getHeight() const { return height_; }
    bool inBounds(int x, int y) const;

    /** @brief Set every cell to v (clamped to [0,1]). */
    void fill(float v);
    /** @brief Set one cell (clamped to [0,1]); out-of-bounds is a no-op. */
    void setHeight(int x, int y, float h);
    /** @brief Cell value in [0,1]; out-of-bounds reads 0. */
    float height(int x, int y) const;

    /** @brief True when any cell changed since the last clearDirty(). */
    bool isDirty() const { return dirty_; }
    void clearDirty() { dirty_ = false; }

    /**
     * @brief Press a footprint into the snow: an ellipse along (dirX, dirZ)
     * with a soft bowl depression and a raised rim at the edges.
     * @param cx / cz  center in grid cell coordinates (may be fractional)
     * @param dirX / dirZ  travel direction (need not be normalized)
     * @param radius  half length along the travel direction, in cells
     * @param depth   depression strength [0,1]
     */
    void stampFootprint(float cx, float cz, float dirX, float dirZ, float radius, float depth);

    /**
     * @brief Carve an impact crater: a parabolic bowl (deep center, shallow
     * edge) plus a raised snow rim just outside the bowl.
     * @param cx / cz  center in grid cell coordinates
     * @param radius  crater radius in cells
     * @param depth   crater depth [0,1]
     */
    void stampImpact(float cx, float cz, float radius, float depth);

    /**
     * @brief Snowfall recovery: raise every cell toward 1 by `amount`.
     * Call with dt * snowfallRate each frame; clamped per cell.
     */
    void addSnowfall(float amount);

    /**
     * @brief POM height map as RGBA8 (width*height*4): R = snow depth, G = B = 0,
     * A = 255 (white = raised toward the viewer, matches parallax_map.glsl).
     */
    std::vector<uint8_t> toHeightRGBA() const;

    /**
     * @brief Albedo as RGBA8: cool-white snow blended to dark ground by depth,
     * plus a tiny per-cell hash, so bare ground reads dark and deep snow white.
     */
    std::vector<uint8_t> toAlbedoRGBA() const;

    /**
     * @brief Tangent-space normal map as RGBA8 derived from the snow-depth
     * gradient (flat = 128,128,255, OpenGL-style, matches applyNormalMap).
     */
    std::vector<uint8_t> toNormalRGBA() const;

    const std::vector<float> &data() const { return data_; }

private:
    static float clamp01(float v);

    int               width_ = 0;
    int               height_ = 0;
    std::vector<float> data_;
    bool              dirty_ = true;
};

}  // namespace eve::snow
