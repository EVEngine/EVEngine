#pragma once

#include <cstdint>

namespace eve::procgen {

/**
 * Deterministic, seamlessly-tiling, time-animated procedural cloud field.
 *
 * Samples a 2D cloud coverage over the XZ ground plane: coverageAt(x, z, t)
 * returns [0,1] where 0 = clear sky and 1 = dense cloud. The field is built
 * from domain-warped value-noise fBm, thresholded into billowy puffs with
 * high-frequency wisp detail, and drifts with a constant wind vector over
 * time. When `seamless` is enabled the field tiles over a `worldScale`-wide
 * grid so it can be repeated across an arbitrarily large ground plane.
 */
class CloudField {
public:
    struct Params {
        uint32_t seed        = 1337;
        float    worldScale  = 96.f;   // world units per tile (larger = larger clouds)
        float    coverage    = 0.55f;  // 0..1 fraction of sky covered (threshold)
        float    softness    = 0.12f;  // 0..1 edge softness of cloud blobs
        float    detail      = 0.5f;   // 0..1 high-frequency wisp detail amount
        float    windSpeed   = 4.f;    // world units / second drift speed
        float    windAngle   = 0.f;    // drift direction (radians, +z axis = 0)
        int      octaves     = 4;
        float    lacunarity  = 2.f;
        float    gain        = 0.5f;
        float    warp        = 2.2f;   // domain-warp amplitude (organic puffs)
        bool     seamless    = true;
    };

    CloudField() = default;
    explicit CloudField(const Params &params) { setParams(params); }

    const Params &params() const { return params_; }
    void          setParams(const Params &params);

    void setSeed(uint32_t seed);
    void setWorldScale(float worldScale);
    void setCoverage(float coverage);
    void setSoftness(float softness);
    void setDetail(float detail);
    void setWind(float speed, float angleRad);
    void setOctaves(int octaves);
    void setWarp(float warp);
    void setSeamless(bool seamless);

    /** Cloud coverage at world (x, z) and time t, in [0,1]. */
    float coverageAt(float x, float z, float time) const;

    /** Fill a width*height buffer with coverage for a world region. */
    void sample(float *out, int width, int height, float time, float x0, float z0,
                float extent) const;

    /** Drift vector (world units/sec) implied by windSpeed + windAngle. */
    void windVelocity(float &vx, float &vz) const;

    /** Internal lattice resolution per tile (also the noise wrap period). */
    static constexpr int kLattice = 64;

private:
    Params params_;
};

}  // namespace eve::procgen
