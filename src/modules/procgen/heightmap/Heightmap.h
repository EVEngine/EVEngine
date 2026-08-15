#pragma once

#include "procgen/Grid2D.h"
#include "procgen/heightmap/TerrainSampler.h"

#include <cstdint>
#include <vector>

namespace eve::procgen {

/**
 * In-memory terrain heightmap: a dense float grid (row-major,
 * index = y * width + x) materialized from a TerrainSampler or filled by hand.
 *
 * Use `generate()` to sample a TerrainSampler per-pixel, `height(x, y)` /
 * `sampleBilinear(x, y)` to read values back, and `toGrid()` to classify the
 * heights into a semantic Grid2D ready for tilemap generation.
 */
class Heightmap {
public:
    Heightmap() = default;
    Heightmap(int width, int height);

    void resize(int width, int height);
    int  getWidth() const;
    int  getHeight() const;
    bool inBounds(int x, int y) const;

    void  setHeight(int x, int y, float h);
    float height(int x, int y) const;

    /** Bilinear height at continuous coordinate within [0, w) x [0, h). */
    float sampleBilinear(float x, float y) const;
    /** Bilinear height with wrap-around (seamless tiling). */
    float sampleBilinearSeamless(float x, float y) const;

    const std::vector<float> &data() const { return data_; }

    /** Materialize a Heightmap by sampling `sampler` at every pixel center. */
    static Heightmap generate(const TerrainSampler &sampler, int width, int height);

    /** Classify heights into a semantic Grid2D (source for tilemap generation). */
    bool toGrid(Grid2D &out, const TerrainBands &bands) const;

private:
    int               width_  = 0;
    int               height_ = 0;
    std::vector<float> data_;
};

}  // namespace eve::procgen
