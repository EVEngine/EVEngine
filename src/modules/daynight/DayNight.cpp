#include "daynight/DayNight.h"

#include "graphics/Graphics.h"
#include "graphics/Light.h"
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
                 const float sunDir[3], float sunEnergy, float nightAmount,
                 float turbidity, float mieStrength, float exposure,
                 void (*dirAt)(int face, int size, int x, int y, float out[3])) {
    const int n = size;
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            float d[3];
            dirAt(face, n, x, y, d);
            const float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            if (len < 1e-6f) { d[0] = 0.f; d[1] = 1.f; d[2] = 0.f; }
            else { d[0] /= len; d[1] /= len; d[2] /= len; }

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
            const Vec3 sunColor{std::exp(-0.16f * sunMass), std::exp(-0.28f * sunMass),
                                std::exp(-0.58f * sunMass)};
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
            color = add(color, scale(Vec3{0.9f, 0.95f, 1.f}, star));
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

    // sky cache (regenerate only when the sun bucket changes)
    bool skyboxEnabled = true;
    graphics::Texture *skyCube = nullptr;
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
    const Vec3 c = toneMapSky(atmosphereRadiance({0.f, 0.04f, 1.f},
        {impl_->sunDir[0], impl_->sunDir[1], impl_->sunDir[2]}, impl_->turbidity,
        impl_->mieStrength), impl_->skyExposure);
    return c.x * impl_->sunEnergy + 0.012f * (1.f - impl_->sunEnergy);
}
float DayNight::getSkyG() const {
    const Vec3 c = toneMapSky(atmosphereRadiance({0.f, 0.04f, 1.f},
        {impl_->sunDir[0], impl_->sunDir[1], impl_->sunDir[2]}, impl_->turbidity,
        impl_->mieStrength), impl_->skyExposure);
    return c.y * impl_->sunEnergy + 0.020f * (1.f - impl_->sunEnergy);
}
float DayNight::getSkyB() const {
    const Vec3 c = toneMapSky(atmosphereRadiance({0.f, 0.04f, 1.f},
        {impl_->sunDir[0], impl_->sunDir[1], impl_->sunDir[2]}, impl_->turbidity,
        impl_->mieStrength), impl_->skyExposure);
    return c.z * impl_->sunEnergy + 0.060f * (1.f - impl_->sunEnergy);
}
float DayNight::getAmbientBrightness() const {
    const float night = impl_->nightLight[1] ? 1.0f : 0.6f;  // starlight boost
    return 0.05f * night + impl_->sunEnergy * 0.5f;
}
float DayNight::getAmbientR() const {
    const float ab = getAmbientBrightness();
    return ab * 0.95f;
}
float DayNight::getAmbientG() const {
    const float ab = getAmbientBrightness();
    return ab * (0.95f + 0.05f * impl_->sunEnergy);  // greener in daylight
}
float DayNight::getAmbientB() const {
    const float ab = getAmbientBrightness();
    return ab * (0.95f + 0.15f * impl_->sunEnergy);  // bluer in daylight
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
    const float elevDeg = kMaxElevationDeg * std::sin(kPi * frac);
    const float azimDeg = (hours / 24.f) * 360.f;  // full rotation per day
    impl_->elevDeg = elevDeg;
    impl_->azimDeg = azimDeg;

    // Sun energy: ramps up a few degrees above the horizon.
    impl_->sunEnergy = std::clamp((elevDeg + 6.f) / 14.f, 0.f, 1.f);
    const float nightAmount = std::clamp((-elevDeg) / 12.f, 0.f, 1.f);

    sunDirection(elevDeg, azimDeg, impl_->sunDir[0], impl_->sunDir[1], impl_->sunDir[2]);

    // Push the directional sun (replaces the legacy directional when no other
    // dir Light3D is active; we keep moon as a Light3D instead so it can have
    // different color/intensity than the sun slot).
    const Vec3 directSun = attenuatedSunColor(impl_->sunDir[1], impl_->turbidity,
                                               impl_->mieStrength);
    gfx->setDirectionalLight(impl_->sunDir[0], impl_->sunDir[1], impl_->sunDir[2],
                             directSun.x * impl_->sunEnergy,
                             directSun.y * impl_->sunEnergy,
                             directSun.z * impl_->sunEnergy);

    // Background matches the sky at the horizon for the clear color.
    const float skyR = getSkyR(), skyG = getSkyG(), skyB = getSkyB();
    gfx->setBackgroundColorRGBA(skyR, skyG, skyB, 1.f);

    // --- procedural skybox (IBL env), regenerated per sun bucket ---
    if (impl_->skyboxEnabled) {
        const int bucket = int(elevDeg) + int(azimDeg / 4.f) * 1000;
        if (bucket != impl_->lastSkyBucket) {
            impl_->lastSkyBucket = bucket;
            std::vector<uint8_t> faces(
                size_t(kSkyCubeSize) * size_t(kSkyCubeSize) * 4 * 6);
            for (int f = 0; f < 6; ++f) {
                std::vector<uint8_t> face(
                    size_t(kSkyCubeSize) * size_t(kSkyCubeSize) * 4);
                fillSkyFace(face, int(kSkyCubeSize), f, impl_->sunDir,
                            impl_->sunEnergy, nightAmount, impl_->turbidity,
                            impl_->mieStrength, impl_->skyExposure, cubeDir);
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
