#pragma once

#include "procgen/texture/CloudField.h"

namespace eve::procgen {

/**
 * @brief Cloud shadows cast onto the ground plane.
 *
 * A cloud at altitude `cloudAltitude` above a ground point casts its shadow
 * where the sun ray from that cloud lands. Given a directional light `sunDir`
 * (normalized, pointing toward the light — e.g. +Y for a sun directly
 * overhead), the shadow coverage at ground position (x, z) equals the
 * CloudField coverage sampled at the cloud point directly up-sun:
 *
 *     cloudPoint = P + sunDir * (cloudAltitude / max(sunDir.y, eps))
 *
 * coverageAt() returns [0,1] cloud coverage (1 = fully shadowed);
 * shadowFactorAt() returns a light multiplier (1 - coverage * strength).
 */
class CloudShadow {
public:
    struct Params {
        CloudField field;               // the cloud field being shadowed
        float sunDirX = 0.f;            // normalized, toward the light (up)
        float sunDirY = 1.f;
        float sunDirZ = 0.f;
        float cloudAltitude = 60.f;     // cloud plane height above ground
        float strength     = 0.8f;      // 0..1 shadow darkness
    };

    CloudShadow() = default;
    explicit CloudShadow(const Params &params) { setParams(params); }

    const Params &params() const { return params_; }
    void          setParams(const Params &params);

    void setSunDirection(float x, float y, float z);
    void setCloudAltitude(float altitude);
    void setStrength(float strength);

    /** @brief Cloud coverage projected onto ground (x, z) at time t, in [0,1]. */
    float coverageAt(float x, float z, float time) const;

    /** @brief Light multiplier in [0,1]: 1 = fully lit, (1-strength) = fully shadowed. */
    float shadowFactorAt(float x, float z, float time) const;

    /** @brief World-space cloud-point offset implied by sun + altitude. */
    void cloudOffset(float &ox, float &oz) const;

    /** @brief Fill a buffer with projected coverage for a world region. */
    void sampleCoverage(float *out, int width, int height, float time, float x0, float z0,
                        float extent) const;

    /** @brief Fill a buffer with shadow light-multiplier factors for a world region. */
    void sampleFactor(float *out, int width, int height, float time, float x0, float z0,
                      float extent) const;

private:
    Params params_;
};

}  // namespace eve::procgen
