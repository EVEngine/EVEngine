#include "daynight/DayNight.h"

#include "graphics/Graphics.h"
#include "graphics/Volumetric.h"
#include "graphics/Light.h"
#include "graphics/ReflectionProbeCapture.h"
#include "graphics/Texture.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::daynight {

namespace {

// Solar orbit constants.
constexpr float kPi = 3.14159265f;
constexpr float kMaxElevationDeg = 70.f;   // solar elevation at local noon
constexpr float kSkyCubeSize = 128;        // per-face resolution of the procedural sky
constexpr int kMaxFireflies = 8;

// Night light names (script-facing) — index maps to the Impl flags array.

inline float deg2rad(float d) { return d * kPi / 180.f; }
inline float smoothstep(float e0, float e1, float x) {
    if (std::fabs(e1 - e0) < 1e-7f) return x < e0 ? 0.f : 1.f;
    const float t = std::clamp((x - e0) / (e1 - e0), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

struct Vec3 {
    float x, y, z;
};

inline Vec3 add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 mul(Vec3 a, Vec3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
inline Vec3 scale(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }

// Compact single-scattering atmosphere approximation.  The wavelength-dependent
// coefficients preserve the important physical relationships (blue Rayleigh sky,
// neutral Mie haze, warm attenuated low sun) while remaining cheap enough to build
// the IBL cubemap on the CPU.  It is deliberately isolated so a future GPU LUT
// implementation can consume the same public atmosphere parameters.
Vec3 atmosphereRadiance(Vec3 view, Vec3 sun, float turbidity, float mieStrength) {
    const float mu = std::clamp(view.x * sun.x + view.y * sun.y + view.z * sun.z, -1.f, 1.f);
    const float horizonMass = 1.f / std::max(0.08f, view.y + 0.14f);
    const float sunMass = 1.f / std::max(0.06f, sun.y + 0.12f);
    const Vec3 betaR{0.055f, 0.130f, 0.285f};
    const Vec3 betaM = scale(Vec3{0.18f, 0.17f, 0.15f}, mieStrength * (0.35f + turbidity * 0.09f));
    const Vec3 extinction = add(betaR, betaM);
    const Vec3 viewT{std::exp(-extinction.x * horizonMass),
                     std::exp(-extinction.y * horizonMass),
                     std::exp(-extinction.z * horizonMass)};
    const Vec3 sunT{std::exp(-extinction.x * sunMass),
                    std::exp(-extinction.y * sunMass),
                    std::exp(-extinction.z * sunMass)};
    const float rayleighPhase = 3.f / (16.f * kPi) * (1.f + mu * mu);
    const float g = std::clamp(0.72f + turbidity * 0.008f, 0.72f, 0.82f);
    const float gg = g * g;
    const float miePhase = (1.f - gg) /
        (4.f * kPi * std::pow(std::max(0.015f, 1.f + gg - 2.f * g * mu), 1.5f));
    const Vec3 scatter = add(scale(betaR, rayleighPhase * 13.f),
                             scale(betaM, miePhase * 2.2f));
    Vec3 sky = mul(mul(scatter, sunT), Vec3{1.f - viewT.x, 1.f - viewT.y, 1.f - viewT.z});
    // Multiple-scattering floor prevents a black antisolar horizon and approximates
    // light returned by the ground/atmosphere without an expensive integral.
    sky = add(sky, scale(mul(sunT, Vec3{0.18f, 0.22f, 0.30f}),
                         0.12f + 0.18f * (1.f - std::max(view.y, 0.f))));
    return sky;
}

Vec3 toneMapSky(Vec3 c, float exposure) {
    c = scale(c, std::max(exposure, 0.01f));
    // ACES fitted curve, followed by display gamma. Keeps a smooth solar halo in
    // the current RGBA8 backend rather than clipping radiance before conversion.
    auto channel = [](float x) {
        x = std::clamp((x * (2.51f * x + 0.03f)) /
                       (x * (2.43f * x + 0.59f) + 0.14f), 0.f, 1.f);
        return std::pow(x, 1.f / 2.2f);
    };
    return {channel(c.x), channel(c.y), channel(c.z)};
}

Vec3 attenuatedSunColor(float sunElevation, float turbidity, float mieStrength) {
    const float mass = 1.f / std::max(0.06f, sunElevation + 0.12f);
    const Vec3 extinction = add(Vec3{0.055f, 0.130f, 0.285f},
        scale(Vec3{0.18f, 0.17f, 0.15f}, mieStrength * (0.35f + turbidity * 0.09f)));
    Vec3 c{std::exp(-extinction.x * mass), std::exp(-extinction.y * mass),
           std::exp(-extinction.z * mass)};
    const float maxChannel = std::max({c.x, c.y, c.z, 1e-5f});
    return scale(c, 1.f / maxChannel);
}

Vec3 correlatedColorTemperature(float kelvin) {
    const float t = std::clamp(kelvin, 1000.f, 40000.f) / 100.f;
    const float r = t <= 66.f ? 1.f : 1.2929362f * std::pow(t - 60.f, -0.13320476f);
    const float g = t <= 66.f ? 0.39008158f * std::log(t) - 0.63184144f
                              : 1.1298909f * std::pow(t - 60.f, -0.07551485f);
    const float b = t >= 66.f ? 1.f
                    : t <= 19.f ? 0.f
                                : 0.5432068f * std::log(t - 10.f) - 1.1962541f;
    return {std::clamp(r, 0.f, 1.f), std::clamp(g, 0.f, 1.f),
            std::clamp(b, 0.f, 1.f)};
}

Vec3 manualSunColor(const PcgManualSunState &sun) {
    const Vec3 temperature = correlatedColorTemperature(sun.kelvin);
    return {sun.red * temperature.x, sun.green * temperature.y,
            sun.blue * temperature.z};
}

// Convert an elevation/azimuth to a unit direction pointing at the sun.
// azimuth measured clockwise from +Z, elevation above the horizon.
inline void sunDirection(float elevDeg, float azimDeg, float &dx, float &dy, float &dz) {
    const float el = deg2rad(elevDeg);
    const float az = deg2rad(azimDeg);
    const float he = std::cos(el);
    dx = he * std::sin(az);
    dy = std::sin(el);
    dz = he * std::cos(az);
}

// Tiny deterministic hash for stars (no <random> dependency).
inline uint32_t hash13(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
inline float hashUnit(uint32_t x) { return float(hash13(x) % 10000u) / 9999.f; }

// Fill one cubemap face's RGBA. `face` in {0..5} order +X,-X,+Y,-Y,+Z,-Z.
// dirAt(x,y) writes the world direction (unnormalized ok) for a pixel.
void fillSkyFace(std::vector<uint8_t> &px, int size, int face,
                 const float sunDir[3], const Vec3 &directSunColor, float sunEnergy,
                 float nightAmount,
                 float turbidity, float mieStrength, float exposure, float cloudiness,
                 float rotationDegrees, const Vec3 &tint,
                 void (*dirAt)(int face, int size, int x, int y, float out[3])) {
    const int n = size;
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            float d[3];
            dirAt(face, n, x, y, d);
            const float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            if (len < 1e-6f) { d[0] = 0.f; d[1] = 1.f; d[2] = 0.f; }
            else { d[0] /= len; d[1] /= len; d[2] /= len; }
            if (rotationDegrees != 0.f) {
                const float angle = deg2rad(rotationDegrees);
                const float x = d[0] * std::cos(angle) + d[2] * std::sin(angle);
                d[2] = -d[0] * std::sin(angle) + d[2] * std::cos(angle);
                d[0] = x;
            }

            const float up = d[1];
            const Vec3 view{d[0], std::max(d[1], 0.002f), d[2]};
            const Vec3 sun{sunDir[0], sunDir[1], sunDir[2]};
            Vec3 day = atmosphereRadiance(view, sun, turbidity, mieStrength);
            Vec3 night{0.006f + 0.012f * std::max(up, 0.f),
                       0.010f + 0.020f * std::max(up, 0.f),
                       0.035f + 0.070f * std::max(up, 0.f)};
            Vec3 color = add(scale(day, (1.f - nightAmount) * sunEnergy),
                             scale(night, nightAmount));

            // Sun disc: a tight highlight around the sun direction.
            const float dot = d[0] * sunDir[0] + d[1] * sunDir[1] + d[2] * sunDir[2];
            // Physical solar angular radius is about 0.27 degrees.  Slightly enlarge it
            // to remain stable in a 128px cubemap; the Mie term supplies the broad halo.
            const float disc = smoothstep(std::cos(deg2rad(0.65f)),
                                          std::cos(deg2rad(0.35f)), dot) * sunEnergy;
            const float sunMass = 1.f / std::max(0.06f, sun.y + 0.12f);
            const Vec3 atmosphericSun{std::exp(-0.16f * sunMass),
                                      std::exp(-0.28f * sunMass),
                                      std::exp(-0.58f * sunMass)};
            const Vec3 sunColor{atmosphericSun.x * directSunColor.x,
                                atmosphericSun.y * directSunColor.y,
                                atmosphericSun.z * directSunColor.z};
            color = add(color, scale(sunColor, disc * 12.f));

            // Stars (only at night, only in the sky hemisphere, avoid the sun).
            float star = 0.f;
            if (nightAmount > 0.5f && up > 0.05f && dot < 0.98f) {
                uint32_t h = hash13(uint32_t((x * 73856093) ^ (y * 19349663) ^ (face * 83492791)));
                if (h % 40u == 0u) {
                    const float tw = 0.6f + 0.4f * hashUnit(h + 1u);
                    star = nightAmount * tw;
                }
            }
            color = add(color, scale(Vec3{0.9f, 0.95f, 1.f}, star * (1.f - cloudiness)));
            const Vec3 overcast{0.075f + 0.035f * std::max(up, 0.f),
                                0.090f + 0.045f * std::max(up, 0.f),
                                0.125f + 0.060f * std::max(up, 0.f)};
            color = add(scale(color, 1.f - cloudiness * 0.82f),
                        scale(overcast, cloudiness));
            color = {color.x * tint.x, color.y * tint.y, color.z * tint.z};
            color = toneMapSky(color, exposure);

            const int i = (y * n + x) * 4;
            px[i + 0] = uint8_t(std::clamp(color.x * 255.f, 0.f, 255.f));
            px[i + 1] = uint8_t(std::clamp(color.y * 255.f, 0.f, 255.f));
            px[i + 2] = uint8_t(std::clamp(color.z * 255.f, 0.f, 255.f));
            px[i + 3] = 255;
        }
    }
}

// Standard cubemap direction mapping for each face (u,v in [0,size]).
void cubeDir(int face, int size, int x, int y, float out[3]) {
    const float u = (2.f * (float(x) + 0.5f) / float(size)) - 1.f;  // -1..1
    const float v = (2.f * (float(y) + 0.5f) / float(size)) - 1.f;  // -1..1
    switch (face) {
        case 0: out[0] = 1.f;  out[1] = -v; out[2] = -u; break;  // +X
        case 1: out[0] = -1.f; out[1] = -v; out[2] =  u; break;  // -X
        case 2: out[0] =  u;   out[1] =  1.f; out[2] = v; break;  // +Y
        case 3: out[0] =  u;   out[1] = -1.f; out[2] = -v; break; // -Y
        case 4: out[0] =  u;   out[1] = -v; out[2] =  1.f; break; // +Z
        default: out[0] = -u;  out[1] = -v; out[2] = -1.f; break; // -Z
    }
}

}  // namespace

struct DayNight::Impl {
    graphics::Graphics *gfx = nullptr;
    bool built = false;

    // clock
    float timeOfDay = 9.f;      // hours 0..24
    float speed = 0.5f;         // simulated hours per real second
    bool paused = false;

    // derived sun
    float elevDeg = 0.f;
    float azimDeg = 0.f;
    float sunDir[3] = {0.f, 1.f, 0.f};
    float sunEnergy = 1.f;
    float turbidity = 2.5f;
    float skyExposure = 1.f;
    float mieStrength = 1.f;
    float weatherCloudiness = 0.f;
    float weatherFlash = 0.f;
    PcgManualSunState manualSun;
    PcgSkyboxState pcgSkybox;
    PcgFogState pcgFog;
    PcgAmbientLightState pcgAmbient;
    graphics::Volumetric *pcgFogTarget = nullptr;
    bool pcgFogWasActive = false;
    std::string pcgFogBaseMode = "screenspace";
    std::string pcgFogBaseQuality = "medium";
    float pcgFogBaseDensity = 0.85f;
    float pcgFogBaseStart = 2.f;
    float pcgFogBaseEnd = 40.f;

    // sky cache (regenerate only when the sun bucket changes)
    bool skyboxEnabled = true;
    graphics::Texture *skyCube = nullptr;
    std::array<graphics::Texture *, 6> skyFaces{};
    std::array<float, 6> skyFaceCenterLuminance{};
    int lastSkyBucket = -1;

    // night lights
    bool nightLight[4] = {true, true, false, true};  // moonlight, starlight, fire, fireflies
    float fireX = 0.f, fireY = 0.5f, fireZ = 0.f;

    struct Fly {
        float x, y, z;      // base anchor
        float seed;         // animation phase
    };
    std::vector<Fly> flies;
    graphics::Light3D *moonLight = nullptr;
    graphics::Light3D *fireLight = nullptr;
    std::vector<graphics::Light3D *> flyLights;
};

const char *const DayNight::kNamedLights[] = {"moonlight", "starlight", "fire", "fireflies"};
const int DayNight::kNamedLightCount = 4;

DayNight::DayNight() : impl_(new Impl()) {}
DayNight::~DayNight() { delete impl_; }

// ---------------------------------------------------------------------------
// Clock / derived state
// ---------------------------------------------------------------------------

void DayNight::setTimeOfDay(float hours) {
    float h = std::fmod(hours, 24.f);
    if (h < 0.f) h += 24.f;
    impl_->timeOfDay = h;
}
float DayNight::getTimeOfDay() const { return impl_->timeOfDay; }

void DayNight::setSpeed(float hprs) { impl_->speed = hprs < 0.f ? 0.f : hprs; }
float DayNight::getSpeed() const { return impl_->speed; }
void DayNight::setPaused(bool p) { impl_->paused = p; }
bool DayNight::isPaused() const { return impl_->paused; }

bool DayNight::isNight() const { return impl_->elevDeg < 0.f; }

float DayNight::getSunElevation() const { return impl_->elevDeg; }
float DayNight::getSunAzimuth() const { return impl_->azimDeg; }
float DayNight::getSunDirX() const { return impl_->sunDir[0]; }
float DayNight::getSunDirY() const { return impl_->sunDir[1]; }
float DayNight::getSunDirZ() const { return impl_->sunDir[2]; }
float DayNight::getSunIntensity() const { return impl_->sunEnergy; }
float DayNight::getSunR() const {
    const Vec3 color = impl_->manualSun.enabled
                           ? manualSunColor(impl_->manualSun)
                           : attenuatedSunColor(impl_->sunDir[1], impl_->turbidity,
                                                impl_->mieStrength);
    const float multiplier = impl_->pcgAmbient.active
                                 ? impl_->pcgAmbient.globalLightMultiplier
                                 : 1.f;
    return color.x * impl_->sunEnergy * (1.f - 0.82f * impl_->weatherCloudiness) * multiplier;
}
float DayNight::getSunG() const {
    const Vec3 color = impl_->manualSun.enabled
                           ? manualSunColor(impl_->manualSun)
                           : attenuatedSunColor(impl_->sunDir[1], impl_->turbidity,
                                                impl_->mieStrength);
    const float multiplier = impl_->pcgAmbient.active
                                 ? impl_->pcgAmbient.globalLightMultiplier
                                 : 1.f;
    return color.y * impl_->sunEnergy * (1.f - 0.82f * impl_->weatherCloudiness) * multiplier;
}
float DayNight::getSunB() const {
    const Vec3 color = impl_->manualSun.enabled
                           ? manualSunColor(impl_->manualSun)
                           : attenuatedSunColor(impl_->sunDir[1], impl_->turbidity,
                                                impl_->mieStrength);
    const float multiplier = impl_->pcgAmbient.active
                                 ? impl_->pcgAmbient.globalLightMultiplier
                                 : 1.f;
    return color.z * impl_->sunEnergy * (1.f - 0.82f * impl_->weatherCloudiness) * multiplier;
}

Result<void> DayNight::setPcgManualSun(const PcgManualSunState &state) {
    const bool finite = std::isfinite(state.pitchDegrees) &&
                        std::isfinite(state.rotationDegrees) &&
                        std::isfinite(state.intensity) && std::isfinite(state.red) &&
                        std::isfinite(state.green) && std::isfinite(state.blue) &&
                        std::isfinite(state.kelvin);
    if (!finite || state.pitchDegrees < 0.f || state.pitchDegrees > 360.f ||
        state.rotationDegrees < 0.f || state.rotationDegrees > 360.f ||
        state.intensity < 0.f || state.intensity > 8.f || state.red < 0.f ||
        state.green < 0.f || state.blue < 0.f || state.kelvin < 1500.f ||
        state.kelvin > 20000.f) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "invalid Pcg manual-sun state", {}, {},
            "daynight.pcgManualSun"));
    }
    impl_->manualSun = state;
    impl_->lastSkyBucket = -1;
    return Result<void>::success();
}

PcgManualSunState DayNight::getPcgManualSun() const noexcept { return impl_->manualSun; }

Result<void> DayNight::setPcgSkybox(const PcgSkyboxState &state) {
    const bool finite = std::isfinite(state.rotationDegrees) &&
                        std::isfinite(state.exposure) && std::isfinite(state.tintRed) &&
                        std::isfinite(state.tintGreen) && std::isfinite(state.tintBlue);
    if (!finite || state.rotationDegrees < 0.f || state.rotationDegrees > 360.f ||
        state.exposure < 0.f || state.exposure > 30.f || state.tintRed < 0.f ||
        state.tintGreen < 0.f || state.tintBlue < 0.f) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "invalid Pcg skybox state", {}, {},
            "daynight.pcgSkybox"));
    }
    impl_->pcgSkybox = state;
    impl_->lastSkyBucket = -1;
    return Result<void>::success();
}

PcgSkyboxState DayNight::getPcgSkybox() const noexcept { return impl_->pcgSkybox; }

Result<void> DayNight::setPcgFog(const PcgFogState &state) {
    const float values[] = {state.additionalLinearDistance, state.additionalExponentialDensity,
                            state.red, state.green, state.blue, state.density,
                            state.startDistance, state.endDistance,
                            state.globalDensityMultiplier, state.densityAlbedoRed,
                            state.densityAlbedoGreen, state.densityAlbedoBlue,
                            state.densityVolumeDistance};
    for (float value : values)
        if (!std::isfinite(value))
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "Pcg fog values must be finite", {}, {},
                "daynight.pcgFog"));
    const bool invalid = state.additionalLinearDistance < -5000.f ||
                         state.additionalLinearDistance > 5000.f ||
                         state.additionalExponentialDensity < 0.f ||
                         state.additionalExponentialDensity > 0.05f || state.mode < 0 ||
                         state.mode > 2 || state.red < 0.f || state.green < 0.f ||
                         state.blue < 0.f || state.density < 0.f || state.density > 0.05f ||
                         state.startDistance < 0.f || state.startDistance > 5000.f ||
                         state.endDistance < 0.f || state.endDistance > 5000.f ||
                         state.globalDensityMultiplier < 0.f ||
                         state.globalDensityMultiplier > 5.f ||
                         state.densityAlbedoRed < 0.f || state.densityAlbedoGreen < 0.f ||
                         state.densityAlbedoBlue < 0.f || state.densityVolumeDistance < 0.01f ||
                         state.densityVolumeDistance > 1500.f ||
                         state.densityVolumeEffect < 0 || state.densityVolumeEffect > 4 ||
                         state.densityVolumeTiling < 0 || state.densityVolumeTiling > 5;
    if (invalid)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "invalid Pcg fog state", {}, {},
            "daynight.pcgFog"));
    impl_->pcgFog = state;
    return Result<void>::success();
}

PcgFogState DayNight::getPcgFog() const noexcept { return impl_->pcgFog; }

Result<void> DayNight::setPcgAmbientLight(const PcgAmbientLightState &state) {
    const float values[] = {state.intensity, state.skyRed, state.skyGreen, state.skyBlue,
                            state.equatorRed, state.equatorGreen, state.equatorBlue,
                            state.groundRed, state.groundGreen, state.groundBlue,
                            state.globalLightMultiplier};
    for (float value : values) {
        if (!std::isfinite(value) || value < 0.f)
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument,
                "Pcg ambient-light values must be finite and non-negative", {}, {},
                "daynight.pcgAmbientLight"));
    }
    if (state.intensity > 10.f || state.globalLightMultiplier > 5.f)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "invalid Pcg ambient-light range", {}, {},
            "daynight.pcgAmbientLight"));
    impl_->pcgAmbient = state;
    return Result<void>::success();
}

PcgAmbientLightState DayNight::getPcgAmbientLight() const noexcept {
    return impl_->pcgAmbient;
}
void DayNight::setTurbidity(float v) {
    impl_->turbidity = std::clamp(v, 1.5f, 10.f);
    impl_->lastSkyBucket = -1;
}
float DayNight::getTurbidity() const { return impl_->turbidity; }
void DayNight::setSkyExposure(float v) {
    impl_->skyExposure = std::clamp(v, 0.05f, 8.f);
    impl_->lastSkyBucket = -1;
}
float DayNight::getSkyExposure() const { return impl_->skyExposure; }
void DayNight::setMieStrength(float v) {
    impl_->mieStrength = std::clamp(v, 0.f, 4.f);
    impl_->lastSkyBucket = -1;
}
float DayNight::getMieStrength() const { return impl_->mieStrength; }

// Sky / ambient colors are functions of the sun energy and night amount.
float DayNight::getSkyR() const {
    const float angle = deg2rad(impl_->pcgSkybox.enabled ? impl_->pcgSkybox.rotationDegrees : 0.f);
    const Vec3 c = toneMapSky(atmosphereRadiance({std::sin(angle), 0.04f, std::cos(angle)},
        {impl_->sunDir[0], impl_->sunDir[1], impl_->sunDir[2]}, impl_->turbidity,
        impl_->mieStrength), impl_->pcgSkybox.enabled ? impl_->pcgSkybox.exposure : impl_->skyExposure);
    const float clear = c.x * impl_->sunEnergy + 0.012f * (1.f - impl_->sunEnergy);
    const float result = clear * (1.f - impl_->weatherCloudiness * 0.72f) +
                         impl_->weatherCloudiness * 0.075f + impl_->weatherFlash * 0.38f;
    return result * (impl_->pcgSkybox.enabled ? impl_->pcgSkybox.tintRed : 1.f);
}
float DayNight::getSkyG() const {
    const float angle = deg2rad(impl_->pcgSkybox.enabled ? impl_->pcgSkybox.rotationDegrees : 0.f);
    const Vec3 c = toneMapSky(atmosphereRadiance({std::sin(angle), 0.04f, std::cos(angle)},
        {impl_->sunDir[0], impl_->sunDir[1], impl_->sunDir[2]}, impl_->turbidity,
        impl_->mieStrength), impl_->pcgSkybox.enabled ? impl_->pcgSkybox.exposure : impl_->skyExposure);
    const float clear = c.y * impl_->sunEnergy + 0.020f * (1.f - impl_->sunEnergy);
    const float result = clear * (1.f - impl_->weatherCloudiness * 0.72f) +
                         impl_->weatherCloudiness * 0.090f + impl_->weatherFlash * 0.48f;
    return result * (impl_->pcgSkybox.enabled ? impl_->pcgSkybox.tintGreen : 1.f);
}
float DayNight::getSkyB() const {
    const float angle = deg2rad(impl_->pcgSkybox.enabled ? impl_->pcgSkybox.rotationDegrees : 0.f);
    const Vec3 c = toneMapSky(atmosphereRadiance({std::sin(angle), 0.04f, std::cos(angle)},
        {impl_->sunDir[0], impl_->sunDir[1], impl_->sunDir[2]}, impl_->turbidity,
        impl_->mieStrength), impl_->pcgSkybox.enabled ? impl_->pcgSkybox.exposure : impl_->skyExposure);
    const float clear = c.z * impl_->sunEnergy + 0.060f * (1.f - impl_->sunEnergy);
    const float result = clear * (1.f - impl_->weatherCloudiness * 0.62f) +
                         impl_->weatherCloudiness * 0.125f + impl_->weatherFlash * 0.68f;
    return result * (impl_->pcgSkybox.enabled ? impl_->pcgSkybox.tintBlue : 1.f);
}
float DayNight::getAmbientBrightness() const {
    const float night = impl_->nightLight[1] ? 1.0f : 0.6f;  // starlight boost
    return 0.05f * night + impl_->sunEnergy * 0.5f *
           (1.f - impl_->weatherCloudiness * 0.68f) + impl_->weatherFlash * 0.45f;
}

void DayNight::setWeatherInfluence(float cloudiness, float lightningFlash) {
    const float nextCloudiness = std::clamp(cloudiness, 0.f, 1.f);
    if (std::fabs(nextCloudiness - impl_->weatherCloudiness) >= 0.04f)
        impl_->lastSkyBucket = -1;
    impl_->weatherCloudiness = nextCloudiness;
    impl_->weatherFlash = std::clamp(lightningFlash, 0.f, 1.f);
}
float DayNight::getWeatherCloudiness() const { return impl_->weatherCloudiness; }
float DayNight::getWeatherFlash() const { return impl_->weatherFlash; }
float DayNight::getAmbientR() const {
    if (impl_->pcgAmbient.active) {
        const auto &a = impl_->pcgAmbient;
        return (a.skyRed * 0.5f + a.equatorRed * 0.35f + a.groundRed * 0.15f) *
               a.intensity;
    }
    const float ab = getAmbientBrightness();
    return ab * 0.95f;
}
float DayNight::getAmbientG() const {
    if (impl_->pcgAmbient.active) {
        const auto &a = impl_->pcgAmbient;
        return (a.skyGreen * 0.5f + a.equatorGreen * 0.35f + a.groundGreen * 0.15f) *
               a.intensity;
    }
    const float ab = getAmbientBrightness();
    return ab * (0.95f + 0.05f * impl_->sunEnergy);  // greener in daylight
}
float DayNight::getAmbientB() const {
    if (impl_->pcgAmbient.active) {
        const auto &a = impl_->pcgAmbient;
        return (a.skyBlue * 0.5f + a.equatorBlue * 0.35f + a.groundBlue * 0.15f) *
               a.intensity;
    }
    const float ab = getAmbientBrightness();
    return ab * (0.95f + 0.15f * impl_->sunEnergy);  // bluer in daylight
}

void DayNight::applyAtmosphere(graphics::Volumetric *fog) const {
    if (!fog) return;
    fog->setLightDirection(getSunDirX(), getSunDirY(), getSunDirZ());
    fog->setFogColor(getSkyR(), getSkyG(), getSkyB());
    fog->setCloudLightColor(getSunR() + impl_->weatherFlash * 0.5f,
                            getSunG() + impl_->weatherFlash * 0.65f,
                            getSunB() + impl_->weatherFlash * 0.9f);
    fog->setIntensity(std::max(0.08f, impl_->sunEnergy + impl_->weatherFlash));
    fog->setTime(impl_->timeOfDay * 18.f);
    const PcgFogState &pcg = impl_->pcgFog;
    const bool hasWeatherOffset = pcg.additionalLinearDistance != 0.f ||
                                  pcg.additionalExponentialDensity != 0.f;
    const bool pcgFogActive = pcg.overrideDensityVolume || pcg.overrideFog ||
                               hasWeatherOffset;
    if (impl_->pcgFogTarget != fog) {
        impl_->pcgFogTarget = fog;
        impl_->pcgFogWasActive = false;
    }
    if (pcgFogActive && !impl_->pcgFogWasActive) {
        impl_->pcgFogBaseMode = fog->getMode();
        impl_->pcgFogBaseQuality = fog->getQuality();
        impl_->pcgFogBaseDensity = fog->getFloat("density");
        impl_->pcgFogBaseStart = fog->getFloat("fogStart");
        impl_->pcgFogBaseEnd = fog->getFloat("fogEnd");
    } else if (!pcgFogActive && impl_->pcgFogWasActive) {
        fog->setMode(impl_->pcgFogBaseMode);
        fog->setQuality(impl_->pcgFogBaseQuality);
        fog->setDensity(impl_->pcgFogBaseDensity);
        fog->setFogStart(impl_->pcgFogBaseStart);
        fog->setFogEnd(impl_->pcgFogBaseEnd);
    }
    impl_->pcgFogWasActive = pcgFogActive;
    if (pcg.overrideDensityVolume) {
        static constexpr float hazeDensity[] = {0.0025f, 0.005f, 0.01f, 0.02f, 0.04f};
        fog->setMode("fog");
        fog->setQuality(pcg.densityVolumeTiling < 2 ? "low" :
                        pcg.densityVolumeTiling < 4 ? "medium" : "high");
        fog->setFogColor(pcg.densityAlbedoRed, pcg.densityAlbedoGreen,
                         pcg.densityAlbedoBlue);
        fog->setDensity(hazeDensity[pcg.densityVolumeEffect] *
                        pcg.globalDensityMultiplier);
        fog->setFogStart(0.f);
        fog->setFogEnd(pcg.densityVolumeDistance);
    } else if (pcg.overrideFog || hasWeatherOffset) {
        fog->setMode("fog");
        fog->setFogColor(pcg.red, pcg.green, pcg.blue);
        const float modeScale = pcg.mode == 2 ? 1.5f : 1.f;
        fog->setDensity((pcg.density + pcg.additionalExponentialDensity) *
                        pcg.globalDensityMultiplier * modeScale);
        const float start = std::max(0.f, pcg.startDistance + pcg.additionalLinearDistance);
        fog->setFogStart(start);
        fog->setFogEnd(std::max(start + 1.f,
                                pcg.endDistance + pcg.additionalLinearDistance));
    }
}

void DayNight::applyReflectionProbeSky(graphics::ReflectionProbeCapture *probe) const {
    if (!probe) return;
    static constexpr Vec3 directions[6] = {
        {1.f, 0.f, 0.f}, {-1.f, 0.f, 0.f}, {0.f, 1.f, 0.f},
        {0.f, -1.f, 0.f}, {0.f, 0.f, 1.f}, {0.f, 0.f, -1.f},
    };
    const Vec3 sun{impl_->sunDir[0], impl_->sunDir[1], impl_->sunDir[2]};
    const float nightAmount = std::clamp((-impl_->elevDeg) / 12.f, 0.f, 1.f);
    const float skyRotation = deg2rad(
        impl_->pcgSkybox.enabled ? impl_->pcgSkybox.rotationDegrees : 0.f);
    const Vec3 skyTint = impl_->pcgSkybox.enabled
                             ? Vec3{impl_->pcgSkybox.tintRed, impl_->pcgSkybox.tintGreen,
                                    impl_->pcgSkybox.tintBlue}
                             : Vec3{1.f, 1.f, 1.f};
    for (int face = 0; face < 6; ++face) {
        Vec3 direction = directions[face];
        const float rotatedX = direction.x * std::cos(skyRotation) +
                               direction.z * std::sin(skyRotation);
        direction.z = -direction.x * std::sin(skyRotation) +
                      direction.z * std::cos(skyRotation);
        direction.x = rotatedX;
        const float up = direction.y;
        const Vec3 view{direction.x, std::max(direction.y, 0.002f), direction.z};
        const Vec3 day = atmosphereRadiance(view, sun, impl_->turbidity, impl_->mieStrength);
        const Vec3 night{0.006f + 0.012f * std::max(up, 0.f),
                         0.010f + 0.020f * std::max(up, 0.f),
                         0.035f + 0.070f * std::max(up, 0.f)};
        Vec3 color = add(scale(day, (1.f - nightAmount) * impl_->sunEnergy),
                         scale(night, nightAmount));
        const Vec3 overcast{0.075f + 0.035f * std::max(up, 0.f),
                            0.090f + 0.045f * std::max(up, 0.f),
                            0.125f + 0.060f * std::max(up, 0.f)};
        color = add(scale(color, 1.f - impl_->weatherCloudiness * 0.82f),
                    scale(overcast, impl_->weatherCloudiness));
        color = add(color, scale(Vec3{0.75f, 0.90f, 1.20f}, impl_->weatherFlash));
        color = scale(color, impl_->pcgSkybox.enabled ? impl_->pcgSkybox.exposure
                                                       : impl_->skyExposure);
        color = {color.x * skyTint.x, color.y * skyTint.y, color.z * skyTint.z};
        probe->setSkyFaceColor(face, color.x, color.y, color.z);
        probe->setSkyFaceTexture(face, impl_->skyFaces[static_cast<size_t>(face)]);
        const float linearLuminance =
            color.x * 0.2126f + color.y * 0.7152f + color.z * 0.0722f;
        const float encodedLuminance =
            impl_->skyFaceCenterLuminance[static_cast<size_t>(face)];
        probe->setSkyFaceTextureScale(
            face, encodedLuminance > 1e-4f ? linearLuminance / encodedLuminance : 1.f);
    }
    probe->setEnvironmentLighting(impl_->skyCube, 0.5f + 0.5f * impl_->sunEnergy);
}

// ---------------------------------------------------------------------------
// Skybox
// ---------------------------------------------------------------------------

void DayNight::setSkyboxEnabled(bool enabled) {
    impl_->skyboxEnabled = enabled;
    impl_->lastSkyBucket = -1;  // force regenerate if re-enabled
}
bool DayNight::isSkyboxEnabled() const { return impl_->skyboxEnabled; }

// ---------------------------------------------------------------------------
// Night lights
// ---------------------------------------------------------------------------

void DayNight::setNightLight(const std::string &name, bool enabled) {
    for (int i = 0; i < kNamedLightCount; ++i) {
        if (name == kNamedLights[i]) {
            impl_->nightLight[i] = enabled;
            return;
        }
    }
}
bool DayNight::isNightLight(const std::string &name) const {
    for (int i = 0; i < kNamedLightCount; ++i) {
        if (name == kNamedLights[i]) return impl_->nightLight[i];
    }
    return false;
}

void DayNight::setFirePosition(float x, float y, float z) {
    impl_->fireX = x; impl_->fireY = y; impl_->fireZ = z;
    if (impl_->fireLight) {
        impl_->fireLight->setPosition(x, y, z);
        impl_->fireLight->setColor(1.0f, 0.55f, 0.2f, 1.2f);
        impl_->fireLight->setRadius(6.f);
    }
}

void DayNight::addFirefly(float x, float y, float z) {
    if (int(impl_->flies.size()) >= kMaxFireflies) return;
    Impl::Fly f;
    f.x = x; f.y = y; f.z = z;
    f.seed = float(impl_->flies.size()) * 1.7f;
    impl_->flies.push_back(f);
    if (impl_->built) {
        graphics::Light3D *l = graphics::Light3D::createLight("point");
        l->setColor(0.6f, 0.9f, 0.3f, 0.9f);
        l->setRadius(2.5f);
        l->setEnabled(false);
        l->setPosition(x, y, z);
        impl_->flyLights.push_back(l);
    }
}
void DayNight::clearFireflies() {
    impl_->flies.clear();
    impl_->flyLights.clear();
}
int DayNight::getFireflyCount() const { return int(impl_->flies.size()); }

// ---------------------------------------------------------------------------
// init / update
// ---------------------------------------------------------------------------

void DayNight::init(graphics::Graphics *gfx) {
    if (impl_->built) return;
    impl_->built = true;
    impl_->gfx = gfx;
    if (!gfx) return;

    // Moon: a cool directional light, driven at night.
    impl_->moonLight = graphics::Light3D::createLight("dir");
    impl_->moonLight->setColor(0.55f, 0.65f, 0.9f, 0.35f);
    impl_->moonLight->setDirection(0.2f, -0.8f, 0.4f);
    impl_->moonLight->setEnabled(false);

    // Fire: a warm point light (position set by setFirePosition).
    impl_->fireLight = graphics::Light3D::createLight("point");
    impl_->fireLight->setColor(1.0f, 0.55f, 0.2f, 1.2f);
    impl_->fireLight->setRadius(6.f);
    impl_->fireLight->setPosition(impl_->fireX, impl_->fireY, impl_->fireZ);
    impl_->fireLight->setEnabled(false);

    // Fireflies.
    for (const auto &f : impl_->flies) {
        graphics::Light3D *l = graphics::Light3D::createLight("point");
        l->setColor(0.6f, 0.9f, 0.3f, 0.9f);
        l->setRadius(2.5f);
        l->setEnabled(false);
        l->setPosition(f.x, f.y, f.z);
        impl_->flyLights.push_back(l);
    }
}

void DayNight::update(float dt, graphics::Graphics *gfx) {
    if (!impl_->built) init(gfx);
    if (!impl_->gfx) return;

    if (!impl_->paused) {
        impl_->timeOfDay += dt * impl_->speed;
        impl_->timeOfDay = std::fmod(impl_->timeOfDay, 24.f);
        if (impl_->timeOfDay < 0.f) impl_->timeOfDay += 24.f;
    }
    const float hours = impl_->timeOfDay;

    // Solar elevation: sine curve peaking at noon (hours=12).
    const float frac = (hours - 6.f) / 12.f;  // -1 at 6h, 0 at 12h, +1 at 18h
    float elevDeg = kMaxElevationDeg * std::sin(kPi * frac);
    float azimDeg = (hours / 24.f) * 360.f;  // full rotation per day
    if (impl_->manualSun.enabled) {
        const float pitch = deg2rad(impl_->manualSun.pitchDegrees);
        const float rotation = deg2rad(impl_->manualSun.rotationDegrees);
        impl_->sunDir[0] = -std::cos(pitch) * std::sin(rotation);
        impl_->sunDir[1] = std::sin(pitch);
        impl_->sunDir[2] = -std::cos(pitch) * std::cos(rotation);
        elevDeg = std::asin(std::clamp(impl_->sunDir[1], -1.f, 1.f)) * 180.f / kPi;
        azimDeg = std::atan2(impl_->sunDir[0], impl_->sunDir[2]) * 180.f / kPi;
        if (azimDeg < 0.f) azimDeg += 360.f;
    } else {
        sunDirection(elevDeg, azimDeg, impl_->sunDir[0], impl_->sunDir[1],
                     impl_->sunDir[2]);
    }
    impl_->elevDeg = elevDeg;
    impl_->azimDeg = azimDeg;

    // Sun energy: ramps up a few degrees above the horizon.
    impl_->sunEnergy = impl_->manualSun.enabled
                           ? impl_->manualSun.intensity
                           : std::clamp((elevDeg + 6.f) / 14.f, 0.f, 1.f);
    const float nightAmount = std::clamp((-elevDeg) / 12.f, 0.f, 1.f);

    // Push the directional sun (replaces the legacy directional when no other
    // dir Light3D is active; we keep moon as a Light3D instead so it can have
    // different color/intensity than the sun slot).
    const Vec3 directSun = impl_->manualSun.enabled
                               ? manualSunColor(impl_->manualSun)
                               : attenuatedSunColor(impl_->sunDir[1], impl_->turbidity,
                                                    impl_->mieStrength);
    const float weatherSun = 1.f - 0.82f * impl_->weatherCloudiness;
    const Vec3 flashLight{impl_->weatherFlash * 0.75f, impl_->weatherFlash * 0.90f,
                          impl_->weatherFlash * 1.20f};
    const float globalLight = impl_->pcgAmbient.active
                                  ? impl_->pcgAmbient.globalLightMultiplier
                                  : 1.f;
    gfx->setDirectionalLight(impl_->sunDir[0], impl_->sunDir[1], impl_->sunDir[2],
                             directSun.x * impl_->sunEnergy * weatherSun * globalLight + flashLight.x,
                             directSun.y * impl_->sunEnergy * weatherSun * globalLight + flashLight.y,
                             directSun.z * impl_->sunEnergy * weatherSun * globalLight + flashLight.z);

    // Background matches the sky at the horizon for the clear color.
    const float skyR = getSkyR(), skyG = getSkyG(), skyB = getSkyB();
    gfx->setBackgroundColorRGBA(skyR, skyG, skyB, 1.f);

    // --- procedural skybox (IBL env), regenerated per sun bucket ---
    if (impl_->skyboxEnabled) {
        const int bucket = int(elevDeg) + int(azimDeg / 4.f) * 1000 +
                           int(impl_->weatherCloudiness * 10.f) * 100000;
        if (bucket != impl_->lastSkyBucket) {
            impl_->lastSkyBucket = bucket;
            std::vector<uint8_t> faces(
                size_t(kSkyCubeSize) * size_t(kSkyCubeSize) * 4 * 6);
            for (int f = 0; f < 6; ++f) {
                std::vector<uint8_t> face(
                    size_t(kSkyCubeSize) * size_t(kSkyCubeSize) * 4);
                const Vec3 tint = impl_->pcgSkybox.enabled
                                      ? Vec3{impl_->pcgSkybox.tintRed, impl_->pcgSkybox.tintGreen,
                                             impl_->pcgSkybox.tintBlue}
                                      : Vec3{1.f, 1.f, 1.f};
                fillSkyFace(face, int(kSkyCubeSize), f, impl_->sunDir, directSun,
                            impl_->sunEnergy, nightAmount, impl_->turbidity,
                            impl_->mieStrength,
                            impl_->pcgSkybox.enabled ? impl_->pcgSkybox.exposure : impl_->skyExposure,
                            impl_->weatherCloudiness,
                            impl_->pcgSkybox.enabled ? impl_->pcgSkybox.rotationDegrees : 0.f,
                            tint, cubeDir);
                impl_->skyFaces[static_cast<size_t>(f)] =
                    gfx->newTexture(int(kSkyCubeSize), int(kSkyCubeSize), face.data());
                const size_t center =
                    (size_t(kSkyCubeSize / 2) * size_t(kSkyCubeSize) + size_t(kSkyCubeSize / 2)) *
                    4u;
                impl_->skyFaceCenterLuminance[static_cast<size_t>(f)] =
                    (float(face[center]) * 0.2126f + float(face[center + 1]) * 0.7152f +
                     float(face[center + 2]) * 0.0722f) /
                    255.f;
                std::memcpy(faces.data() + size_t(f) * face.size(), face.data(), face.size());
            }
            // Replace the previous env cube; Graphics owns old textures.
            impl_->skyCube = gfx->newCubemap(int(kSkyCubeSize), faces.data(),
                graphics::TextureCreateInfo::withMipmaps(true));
            gfx->setMesh3DEnv(impl_->skyCube, 0.5f + 0.5f * impl_->sunEnergy);
        }
    }

    // --- night light systems (only meaningful below the horizon) ---
    const bool night = elevDeg < 0.f;

    // Moonlight: a directional light at the opposite-ish angle of the sun.
    if (impl_->moonLight) {
        const bool on = night && impl_->nightLight[0];
        impl_->moonLight->setEnabled(on);
        if (on) {
            impl_->moonLight->setDirection(-impl_->sunDir[0], -impl_->sunDir[1],
                                           -impl_->sunDir[2]);
        }
    }

    // Fire.
    if (impl_->fireLight) {
        impl_->fireLight->setEnabled(night && impl_->nightLight[2]);
    }

    // Fireflies: gentle sinusoidal drift.
    if (impl_->flyLights.size() == impl_->flies.size()) {
        for (size_t i = 0; i < impl_->flies.size(); ++i) {
            graphics::Light3D *l = impl_->flyLights[i];
            const Impl::Fly &f = impl_->flies[i];
            const bool on = night && impl_->nightLight[3];
            l->setEnabled(on);
            if (on) {
                const float t = impl_->timeOfDay + f.seed;
                l->setPosition(f.x + std::sin(t * 0.9f) * 0.6f,
                               f.y + std::sin(t * 1.3f + 1.7f) * 0.4f,
                               f.z + std::cos(t * 0.8f) * 0.6f);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Script binding
// ---------------------------------------------------------------------------

void DayNight::expose(ssq::Table &table) {
    auto cls = table.addClass(name, DayNight::create, false);
    expose(cls);
}

void DayNight::expose(ssq::Class &cls) {
    cls.addFunc("getName", &DayNight::getName);
    cls.addFunc("init", &DayNight::init);
    cls.addFunc("update", &DayNight::update);
    cls.addFunc("setTimeOfDay", &DayNight::setTimeOfDay);
    cls.addFunc("getTimeOfDay", &DayNight::getTimeOfDay);
    cls.addFunc("setSpeed", &DayNight::setSpeed);
    cls.addFunc("getSpeed", &DayNight::getSpeed);
    cls.addFunc("setPaused", &DayNight::setPaused);
    cls.addFunc("isPaused", &DayNight::isPaused);
    cls.addFunc("isNight", &DayNight::isNight);
    cls.addFunc("getSunElevation", &DayNight::getSunElevation);
    cls.addFunc("getSunAzimuth", &DayNight::getSunAzimuth);
    cls.addFunc("getSunDirX", &DayNight::getSunDirX);
    cls.addFunc("getSunDirY", &DayNight::getSunDirY);
    cls.addFunc("getSunDirZ", &DayNight::getSunDirZ);
    cls.addFunc("getSunIntensity", &DayNight::getSunIntensity);
    cls.addFunc("getSunR", &DayNight::getSunR);
    cls.addFunc("getSunG", &DayNight::getSunG);
    cls.addFunc("getSunB", &DayNight::getSunB);
    cls.addFunc("setTurbidity", &DayNight::setTurbidity);
    cls.addFunc("getTurbidity", &DayNight::getTurbidity);
    cls.addFunc("setSkyExposure", &DayNight::setSkyExposure);
    cls.addFunc("getSkyExposure", &DayNight::getSkyExposure);
    cls.addFunc("setMieStrength", &DayNight::setMieStrength);
    cls.addFunc("getMieStrength", &DayNight::getMieStrength);
    cls.addFunc("getSkyR", &DayNight::getSkyR);
    cls.addFunc("getSkyG", &DayNight::getSkyG);
    cls.addFunc("getSkyB", &DayNight::getSkyB);
    cls.addFunc("getAmbientR", &DayNight::getAmbientR);
    cls.addFunc("getAmbientG", &DayNight::getAmbientG);
    cls.addFunc("getAmbientB", &DayNight::getAmbientB);
    cls.addFunc("getAmbientBrightness", &DayNight::getAmbientBrightness);
    cls.addFunc("applyAtmosphere", &DayNight::applyAtmosphere);
    cls.addFunc("applyReflectionProbeSky", &DayNight::applyReflectionProbeSky);
    cls.addFunc("setWeatherInfluence", &DayNight::setWeatherInfluence);
    cls.addFunc("getWeatherCloudiness", &DayNight::getWeatherCloudiness);
    cls.addFunc("getWeatherFlash", &DayNight::getWeatherFlash);
    cls.addFunc("setSkyboxEnabled", &DayNight::setSkyboxEnabled);
    cls.addFunc("isSkyboxEnabled", &DayNight::isSkyboxEnabled);
    cls.addFunc("setNightLight", &DayNight::setNightLight);
    cls.addFunc("isNightLight", &DayNight::isNightLight);
    cls.addFunc("setFirePosition", &DayNight::setFirePosition);
    cls.addFunc("addFirefly", &DayNight::addFirefly);
    cls.addFunc("clearFireflies", &DayNight::clearFireflies);
    cls.addFunc("getFireflyCount", &DayNight::getFireflyCount);
}

Module_IMPL(DayNight, new DayNight());

}  // namespace eve::daynight
