#include "weather/Weather.h"

#include "graphics/Graphics.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/Shader.h"
#include "graphics/RenderSystem3D.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <simplesquirrel/simplesquirrel.hpp>

#include "weather/shaders/weather_vert_spv.inc"
#include "weather/shaders/weather_frag_spv.inc"
#include "weather/shaders/bolt_vert_spv.inc"
#include "weather/shaders/bolt_frag_spv.inc"

namespace eve::weather {

namespace {

// ---- tiny deterministic RNG (no dependency on <random>) ----
struct Lcg {
    uint32_t s = 0x9e3779b9u;
    uint32_t next() {
        s = s * 1664525u + 1013904223u;
        return s;
    }
    float unit() { return float(next() % 10000u) / 9999.f; }   // [0,1]
    float range(float a, float b) { return a + (b - a) * unit(); }
};

constexpr int kRainCount = 1600;
constexpr int kSnowCount = 260;
constexpr int kBoltCount = 4;

constexpr float kBoxXZ = 26.f;  // half extent on X/Z
constexpr float kBoxY = 20.f;   // fall band height

// Push-constant slots are fixed and must match the shaders and
// declareWeatherParams(): 0=time 1=windX 2=windZ 3=speed 4=length 5=width
// 6=intensity 7..9=fog rgb 10=fogDensity 11=flash.

// ---------------------------------------------------------------------------
// Texture generation
// ---------------------------------------------------------------------------

// Vertical rain streak: bright core, alpha falloff toward the edges/ends.
void genRainTexture(std::vector<uint8_t> &rgba) {
    const int w = 8, h = 32;
    rgba.resize(size_t(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        const float v = (float(y) + 0.5f) / float(h);  // 0 top -> 1 bottom
        const float endFade = 1.f - 2.f * std::fabs(v - 0.5f);  // 0 at ends, 1 center
        for (int x = 0; x < w; ++x) {
            const float u = (float(x) + 0.5f) / float(w);
            const float edge = 1.f - 2.f * std::fabs(u - 0.5f);  // 0 at sides
            const float alpha = std::pow(edge, 2.f) * std::pow(endFade, 2.5f);
            const int i = (y * w + x) * 4;
            rgba[i + 0] = 255;
            rgba[i + 1] = 250;
            rgba[i + 2] = 255;
            rgba[i + 3] = uint8_t(std::min(255.f, alpha * 255.f));
        }
    }
}

// Soft round snowflake.
void genSnowTexture(std::vector<uint8_t> &rgba) {
    const int w = 16, h = 16;
    rgba.resize(size_t(w) * h * 4);
    const float cx = (float(w) - 1.f) * 0.5f;
    const float cy = (float(h) - 1.f) * 0.5f;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const float dx = (float(x) - cx) / cx;
            const float dy = (float(y) - cy) / cy;
            const float r = std::sqrt(dx * dx + dy * dy);
            float alpha = 1.f - r;
            if (alpha < 0.f) alpha = 0.f;
            alpha = std::pow(alpha, 2.2f);
            const int i = (y * w + x) * 4;
            rgba[i + 0] = 255;
            rgba[i + 1] = 255;
            rgba[i + 2] = 255;
            rgba[i + 3] = uint8_t(std::min(255.f, alpha * 255.f));
        }
    }
}

// ---------------------------------------------------------------------------
// Mesh builders
// ---------------------------------------------------------------------------

// A field of billboarded quads. Each quad shares an anchor in inPos; inUV
// encodes the local offset. `count` quads, `len` = quad height, `width` = width.
graphics::Mesh *buildFieldMesh(graphics::Graphics *gfx, Lcg &rng, int count, float len,
                               float width) {
    (void)len;
    (void)width;
    const int vertCount = count * 4;
    const int idxCount = count * 6;
    std::vector<float> pos(vertCount * 3);
    std::vector<float> nrm(vertCount * 3, 0.f);
    std::vector<float> uv(vertCount * 2);
    std::vector<uint32_t> idx(idxCount);

    for (int i = 0; i < count; ++i) {
        const float ax = rng.range(-kBoxXZ, kBoxXZ);
        const float ay = rng.range(0.f, kBoxY);
        const float az = rng.range(-kBoxXZ, kBoxXZ);
        const int v0 = i * 4;
        const int b = v0 * 3, t = v0 * 2;

        // bottom-left / top-left / top-right / bottom-right (uv-driven billboard).
        pos[b + 0] = ax; pos[b + 1] = ay; pos[b + 2] = az;
        pos[b + 3] = ax; pos[b + 4] = ay; pos[b + 5] = az;
        pos[b + 6] = ax; pos[b + 7] = ay; pos[b + 8] = az;
        pos[b + 9] = ax; pos[b + 10] = ay; pos[b + 11] = az;

        // Pack per-particle variation in the otherwise unused normal.
        const float lengthScale = rng.range(0.55f, 1.45f);
        const float widthScale = rng.range(0.65f, 1.25f);
        const float speedScale = rng.range(0.82f, 1.18f);
        for (int j = 0; j < 4; ++j) {
            nrm[b + j * 3 + 0] = lengthScale;
            nrm[b + j * 3 + 1] = widthScale;
            nrm[b + j * 3 + 2] = speedScale;
        }

        // uv.x = horizontal offset, uv.y = vertical offset (in [0,1]).
        uv[t + 0] = -0.5f; uv[t + 1] = 0.f;
        uv[t + 2] = -0.5f; uv[t + 3] = 1.f;
        uv[t + 4] = 0.5f;  uv[t + 5] = 1.f;
        uv[t + 6] = 0.5f;  uv[t + 7] = 0.f;

        const uint32_t base = uint32_t(v0);
        idx[i * 6 + 0] = base + 0;
        idx[i * 6 + 1] = base + 1;
        idx[i * 6 + 2] = base + 2;
        idx[i * 6 + 3] = base + 0;
        idx[i * 6 + 4] = base + 2;
        idx[i * 6 + 5] = base + 3;
    }

    return gfx->newMeshFromArrays(pos.data(), nrm.data(), uv.data(), vertCount, idx.data(),
                                  idxCount);
}

struct Pt {
    float x = 0, y = 0, z = 0;
};

void displacePath(std::vector<Pt> &path, Lcg &rng, float amount) {
    std::vector<Pt> refined;
    refined.reserve(path.size() * 2 - 1);
    for (size_t i = 0; i + 1 < path.size(); ++i) {
        const Pt &a = path[i];
        const Pt &b = path[i + 1];
        refined.push_back(a);
        refined.push_back({(a.x + b.x) * 0.5f + rng.range(-amount, amount),
                           (a.y + b.y) * 0.5f,
                           (a.z + b.z) * 0.5f + rng.range(-amount, amount)});
    }
    refined.push_back(path.back());
    path.swap(refined);
}

// Fractal midpoint displacement is the classic inexpensive lightning model.
// Crossed tapered ribbons keep the channel visible from every camera angle.
graphics::Mesh *buildBoltMesh(graphics::Graphics *gfx, Lcg &rng, Pt top, Pt ground) {
    std::vector<Pt> pts{top, ground};
    float displacement = 3.2f;
    for (int level = 0; level < 5; ++level) {
        displacePath(pts, rng, displacement);
        displacement *= 0.48f;
    }

    std::vector<std::vector<Pt>> paths{pts};
    const int branchStarts[] = {7, 12, 18, 23};
    for (int start : branchStarts) {
        if (start >= int(pts.size()) - 2) continue;
        const Pt origin = pts[start];
        const float side = (rng.unit() < 0.5f) ? -1.f : 1.f;
        Pt tip{origin.x + side * rng.range(2.5f, 5.0f),
               origin.y - rng.range(2.5f, 5.5f),
               origin.z + rng.range(-3.5f, 3.5f)};
        if (tip.y < 0.5f) tip.y = 0.5f;
        std::vector<Pt> branch{origin, tip};
        float branchDisp = 1.1f;
        for (int level = 0; level < 3; ++level) {
            displacePath(branch, rng, branchDisp);
            branchDisp *= 0.45f;
        }
        paths.push_back(branch);
    }

    std::vector<Pt> all;
    std::vector<float> allUv;
    auto emitStrip = [&](const std::vector<Pt> &path, float wTop, float wBottom, int plane) {
        const int n = int(path.size()) - 1;
        for (int i = 0; i < n; ++i) {
            Pt a = path[i], b = path[i + 1];
            float sx = plane == 0 ? 1.f : 0.f;
            float sz = plane == 0 ? 0.f : 1.f;
            float w = wBottom + (wTop - wBottom) * (float(i) / float(n));
            Pt p0 = {a.x + sx * w, a.y, a.z + sz * w};
            Pt p1 = {a.x - sx * w, a.y, a.z - sz * w};
            Pt p2 = {b.x - sx * w, b.y, b.z - sz * w};
            Pt p3 = {b.x + sx * w, b.y, b.z + sz * w};
            all.push_back(p0);
            all.push_back(p1);
            all.push_back(p2);
            all.push_back(p3);
            allUv.insert(allUv.end(), {-1.f, float(i) / n, 1.f, float(i) / n,
                                       1.f, float(i + 1) / n, -1.f, float(i + 1) / n});
        }
    };

    const float wMain = 0.20f;
    for (size_t p = 0; p < paths.size(); ++p) {
        const float scale = p == 0 ? 1.f : 0.55f;
        emitStrip(paths[p], wMain * scale, wMain * 0.10f * scale, 0);
        emitStrip(paths[p], wMain * scale, wMain * 0.10f * scale, 1);
    }

    const int vertCount = int(all.size());
    const int idxCount = vertCount / 4 * 6;
    std::vector<float> pos(vertCount * 3);
    std::vector<float> nrm(vertCount * 3, 0.f);
    std::vector<float> uv(vertCount * 2, 0.f);
    std::vector<uint32_t> idx(idxCount);

    for (int i = 0; i < vertCount; ++i) {
        pos[i * 3 + 0] = all[i].x;
        pos[i * 3 + 1] = all[i].y;
        pos[i * 3 + 2] = all[i].z;
        nrm[i * 3 + 1] = 1.f;
        uv[i * 2 + 0] = allUv[i * 2 + 0];
        uv[i * 2 + 1] = allUv[i * 2 + 1];
    }
    for (int q = 0; q < vertCount / 4; ++q) {
        const uint32_t base = uint32_t(q * 4);
        idx[q * 6 + 0] = base + 0;
        idx[q * 6 + 1] = base + 1;
        idx[q * 6 + 2] = base + 2;
        idx[q * 6 + 3] = base + 0;
        idx[q * 6 + 4] = base + 2;
        idx[q * 6 + 5] = base + 3;
    }

    return gfx->newMeshFromArrays(pos.data(), nrm.data(), uv.data(), vertCount, idx.data(),
                                  idxCount);
}

}  // namespace

// ---------------------------------------------------------------------------

struct Weather::Impl {
    graphics::Graphics *gfx = nullptr;
    bool built = false;
    float time = 0.f;

    // state
    int preset = 0;              // index into kPresetNames
    float intensity = 0.f;       // 0..1
    float intensityCur = 0.f;
    float windSpeed = 0.f;
    float windDirDeg = 0.f;
    bool lightningEnabled = true;
    float flash = 0.f;
    float flashTimer = 0.f;
    float nextStrike = 4.f;
    bool environmentEnabled = true;

    // mood
    float skyR = 0.45f, skyG = 0.53f, skyB = 0.62f;
    float sunIntensity = 1.f;
    float fogR = 0.55f, fogG = 0.58f, fogB = 0.62f;
    float fogDensity = 0.004f;

    // renderables
    graphics::Renderable3D *rain = nullptr;
    graphics::Renderable3D *snow = nullptr;
    std::vector<graphics::Renderable3D *> bolts;
    graphics::Material *rainMat = nullptr;
    graphics::Material *snowMat = nullptr;
    std::vector<graphics::Material *> boltMats;
    int activeBolt = -1;
    float boltLife = 0.f;
    bool boltOnceFired = false;

    Lcg rng;
};

const char *const Weather::kPresetNames[] = {"clear", "drizzle", "rain",
                                             "storm", "snow",    "fog"};
const int Weather::kPresetCount = 6;

Weather::Weather() : impl_(new Impl()) {}

Weather::~Weather() { delete impl_; }

// ---------------------------------------------------------------------------
// Private setup
// ---------------------------------------------------------------------------

namespace {
void pushWeatherParams(graphics::Material *mat, float time, float windX, float windZ,
                       float speed, float length, float width, float intensity,
                       float fogR, float fogG, float fogB, float fogDensity, float flash) {
    mat->setFloat("uTime", time);
    mat->setFloat("uWindX", windX);
    mat->setFloat("uWindZ", windZ);
    mat->setFloat("uSpeed", speed);
    mat->setFloat("uLength", length);
    mat->setFloat("uWidth", width);
    mat->setFloat("uIntensity", intensity);
    mat->setFloat("uFogR", fogR);
    mat->setFloat("uFogG", fogG);
    mat->setFloat("uFogB", fogB);
    mat->setFloat("uFogDensity", fogDensity);
    mat->setFloat("uFlash", flash);
}

// Declare the push-constant slots on a shader in the exact order above.
void declareWeatherParams(graphics::Shader *shader) {
    shader->declareFloat("uTime");
    shader->declareFloat("uWindX");
    shader->declareFloat("uWindZ");
    shader->declareFloat("uSpeed");
    shader->declareFloat("uLength");
    shader->declareFloat("uWidth");
    shader->declareFloat("uIntensity");
    shader->declareFloat("uFogR");
    shader->declareFloat("uFogG");
    shader->declareFloat("uFogB");
    shader->declareFloat("uFogDensity");
    shader->declareFloat("uFlash");
}
}  // namespace

void Weather::init(graphics::Graphics *gfx) {
    if (impl_->built) return;
    impl_->built = true;
    impl_->gfx = gfx;
    if (!gfx) return;

    // ---- textures ----
    std::vector<uint8_t> rainRgba, snowRgba;
    genRainTexture(rainRgba);
    genSnowTexture(snowRgba);
    graphics::Texture *rainTex = gfx->newTexture(8, 32, rainRgba.data(), true, true);
    graphics::Texture *snowTex = gfx->newTexture(16, 16, snowRgba.data(), true, true);

    // ---- shaders (precompiled SPIR-V; runtime GLSL compile is not available on Windows) ----
    std::vector<uint32_t> wv(weather_vert_spv, weather_vert_spv + weather_vert_spv_count);
    std::vector<uint32_t> wf(weather_frag_spv, weather_frag_spv + weather_frag_spv_count);
    std::vector<uint32_t> bv(bolt_vert_spv, bolt_vert_spv + bolt_vert_spv_count);
    std::vector<uint32_t> bf(bolt_frag_spv, bolt_frag_spv + bolt_frag_spv_count);
    graphics::Shader *weatherVert = gfx->newMeshShaderFromSpv(wv, wf);
    graphics::Shader *boltShader = gfx->newMeshShaderFromSpv(bv, bf);
    declareWeatherParams(weatherVert);
    declareWeatherParams(boltShader);

    // ---- rain ----
    graphics::Mesh *rainMesh = buildFieldMesh(gfx, impl_->rng, kRainCount, 0.82f, 0.024f);
    impl_->rain = graphics::Renderable3D::create();
    impl_->rain->setMesh(rainMesh);
    impl_->rain->setTexture(rainTex);
    impl_->rainMat = gfx->newMaterial();
    impl_->rainMat->setShadingModel("unlit");
    impl_->rainMat->setReceiveLight(false);
    impl_->rainMat->setReceiveShadow(false);
    impl_->rainMat->setCastShadow(false);
    impl_->rainMat->setTint(0.38f, 0.52f, 0.68f, 1.0f);
    impl_->rainMat->setShader(weatherVert);
    impl_->rain->setMaterial(impl_->rainMat);
    impl_->rain->setVisible(false);

    // ---- snow ----
    graphics::Mesh *snowMesh = buildFieldMesh(gfx, impl_->rng, kSnowCount, 0.22f, 0.10f);
    impl_->snow = graphics::Renderable3D::create();
    impl_->snow->setMesh(snowMesh);
    impl_->snow->setTexture(snowTex);
    impl_->snowMat = gfx->newMaterial();
    impl_->snowMat->setShadingModel("unlit");
    impl_->snowMat->setReceiveLight(false);
    impl_->snowMat->setReceiveShadow(false);
    impl_->snowMat->setCastShadow(false);
    impl_->snowMat->setTint(1.0f, 1.0f, 1.0f, 1.0f);
    impl_->snowMat->setShader(weatherVert);
    impl_->snow->setMaterial(impl_->snowMat);
    impl_->snow->setVisible(false);

    // ---- lightning bolts ----
    for (int i = 0; i < kBoltCount; ++i) {
        Pt top{impl_->rng.range(-4.f, 4.f), kBoxY, impl_->rng.range(-4.f, 4.f)};
        Pt ground{impl_->rng.range(-3.f, 3.f), 0.f, impl_->rng.range(-3.f, 3.f)};
        graphics::Mesh *m = buildBoltMesh(gfx, impl_->rng, top, ground);
        graphics::Renderable3D *b = graphics::Renderable3D::create();
        b->setMesh(m);
        graphics::Material *mm = gfx->newMaterial();
        mm->setShadingModel("unlit");
        mm->setReceiveLight(false);
        mm->setReceiveShadow(false);
        mm->setCastShadow(false);
        mm->setTint(1.0f, 1.0f, 1.0f, 1.0f);
        mm->setShader(boltShader);
        b->setMaterial(mm);
        b->setVisible(false);
        impl_->bolts.push_back(b);
        impl_->boltMats.push_back(mm);
    }

    pushWeatherParams(impl_->rainMat, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
    pushWeatherParams(impl_->snowMat, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void Weather::setPreset(const std::string &name) {
    for (int i = 0; i < kPresetCount; ++i) {
        if (name == kPresetNames[i]) {
            impl_->preset = i;
            return;
        }
    }
}

std::string Weather::getPreset() const { return kPresetNames[impl_->preset]; }

void Weather::setIntensity(float v) {
    impl_->intensity = v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
}
float Weather::getIntensity() const { return impl_->intensity; }

void Weather::setWindSpeed(float v) { impl_->windSpeed = v < 0.f ? 0.f : v; }
float Weather::getWindSpeed() const { return impl_->windSpeed; }

void Weather::setWindDirection(float deg) { impl_->windDirDeg = deg; }
float Weather::getWindDirection() const { return impl_->windDirDeg; }

void Weather::setLightningEnabled(bool on) { impl_->lightningEnabled = on; }
bool Weather::isLightningEnabled() const { return impl_->lightningEnabled; }

void Weather::strike() {
    if (!impl_->built || impl_->bolts.empty()) return;
    impl_->activeBolt = impl_->rng.next() % int(impl_->bolts.size());
    impl_->boltLife = 0.0f;
    impl_->boltOnceFired = false;
    impl_->flash = 1.0f;
    impl_->flashTimer = 0.12f;
    impl_->nextStrike = 3.0f + impl_->rng.unit() * 6.0f;
}

float Weather::getFlash() const { return impl_->flash; }

void Weather::setSkyColor(float r, float g, float b) {
    impl_->skyR = r; impl_->skyG = g; impl_->skyB = b;
}
float Weather::getSkyColorR() const { return impl_->skyR; }
float Weather::getSkyColorG() const { return impl_->skyG; }
float Weather::getSkyColorB() const { return impl_->skyB; }

void Weather::setSunIntensity(float v) { impl_->sunIntensity = v < 0.f ? 0.f : v; }
float Weather::getSunIntensity() const { return impl_->sunIntensity; }

void Weather::setFogColor(float r, float g, float b) {
    impl_->fogR = r; impl_->fogG = g; impl_->fogB = b;
}
float Weather::getFogColorR() const { return impl_->fogR; }
float Weather::getFogColorG() const { return impl_->fogG; }
float Weather::getFogColorB() const { return impl_->fogB; }

void Weather::setFogDensity(float v) { impl_->fogDensity = v < 0.f ? 0.f : v; }
float Weather::getFogDensity() const { return impl_->fogDensity; }

void Weather::setEnvironmentEnabled(bool enabled) { impl_->environmentEnabled = enabled; }
bool Weather::isEnvironmentEnabled() const { return impl_->environmentEnabled; }

float Weather::getAmbientBrightness() const {
    return 0.35f + (1.f - impl_->intensityCur) * 0.55f;
}

// ---------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------

void Weather::update(float dt, graphics::Graphics *gfx) {
    if (!impl_->built) init(gfx);
    if (!impl_->gfx) return;
    impl_->time += dt;

    // Smooth intensity toward target.
    const float target = impl_->intensity;
    impl_->intensityCur += (target - impl_->intensityCur) * std::min(1.f, dt * 3.f);
    if (std::fabs(target - impl_->intensityCur) < 0.002f) impl_->intensityCur = target;

    // Wind vector in world space (dirDeg 0 -> +Z, 90 -> -X).
    const float rad = impl_->windDirDeg * 0.0174532925f;
    const float windX = -std::sin(rad) * impl_->windSpeed;
    const float windZ = std::cos(rad) * impl_->windSpeed;

    const float time = impl_->time;
    const float fogR = impl_->fogR, fogG = impl_->fogG, fogB = impl_->fogB;
    const float fogD = impl_->fogDensity;
    const float intensity = impl_->intensityCur;

    // Rain visible for drizzle/rain/storm.
    const bool rainOn = impl_->preset >= 1 && impl_->preset <= 3;
    impl_->rain->setVisible(rainOn && intensity > 0.01f);
    if (rainOn) {
        const float speed = 26.f;
        pushWeatherParams(impl_->rainMat, time, windX, windZ, speed, 0.82f, 0.024f, intensity,
                          fogR, fogG, fogB, fogD, 0.f);
    }

    // Snow.
    const bool snowOn = impl_->preset == 4;
    impl_->snow->setVisible(snowOn && intensity > 0.01f);
    if (snowOn) {
        pushWeatherParams(impl_->snowMat, time, windX, windZ, 2.2f, 0.22f, 0.10f, intensity,
                          fogR, fogG, fogB, fogD, 0.f);
    }

    // Lightning during storm.
    const bool storm = impl_->preset == 3;
    if (storm && impl_->lightningEnabled) {
        impl_->nextStrike -= dt;
        if (impl_->nextStrike <= 0.f && impl_->activeBolt < 0) strike();
    }

    // Drive the active bolt's flash decay.
    if (impl_->activeBolt >= 0) {
        impl_->boltLife += dt;
        impl_->flashTimer -= dt;
        // Return-stroke profile: bright leader, dark gap, weaker second pulse.
        float f;
        if (impl_->boltLife < 0.055f) {
            f = 1.f - impl_->boltLife * 4.f;
        } else if (impl_->boltLife < 0.095f) {
            f = 0.06f;
        } else if (impl_->boltLife < 0.19f) {
            f = 0.72f * (1.f - (impl_->boltLife - 0.095f) / 0.095f);
        } else {
            f = std::max(0.f, 0.22f * (1.f - (impl_->boltLife - 0.19f) / 0.31f));
        }
        impl_->flash = std::clamp(f, 0.f, 1.f);

        const int bi = impl_->activeBolt;
        impl_->bolts[bi]->setVisible(impl_->flash > 0.02f);
        pushWeatherParams(impl_->boltMats[bi], time, windX, windZ, 0.f, 0.f, 0.f, 1.f, fogR, fogG,
                          fogB, fogD, impl_->flash);

        if (impl_->boltLife > 0.50f) {
            impl_->bolts[bi]->setVisible(false);
            impl_->activeBolt = -1;
            impl_->flash = 0.f;
        }
    } else {
        impl_->flash *= std::max(0.f, 1.f - dt * 8.f);
    }

    // Push ambient/sky mood onto the graphics state each frame.
    if (gfx && impl_->environmentEnabled) {
        const float dark = 1.f - 0.65f * impl_->intensityCur;
        const float skyFlash = impl_->flash * 0.55f;
        gfx->setBackgroundColorRGBA(
            std::min(1.f, impl_->skyR * dark + skyFlash * 0.68f),
            std::min(1.f, impl_->skyG * dark + skyFlash * 0.78f),
            std::min(1.f, impl_->skyB * dark + skyFlash), 1.f);
        const float flashLight = impl_->flash * 1.35f;
        gfx->setDirectionalLight(-0.4f, 0.75f, 0.5f,
                                 impl_->sunIntensity * (1.f - 0.6f * impl_->intensityCur) + flashLight * 0.72f,
                                 impl_->sunIntensity * (1.f - 0.6f * impl_->intensityCur) * 0.95f + flashLight * 0.84f,
                                 impl_->sunIntensity * (1.f - 0.6f * impl_->intensityCur) * 0.88f + flashLight);
    }
}

// ---------------------------------------------------------------------------
// Script binding
// ---------------------------------------------------------------------------

void Weather::expose(ssq::Table &table) {
    auto cls = table.addClass(name, Weather::create, false);
    expose(cls);
}

void Weather::expose(ssq::Class &cls) {
    cls.addFunc("getName", &Weather::getName);
    cls.addFunc("init", &Weather::init);
    cls.addFunc("update", &Weather::update);
    cls.addFunc("setPreset", &Weather::setPreset);
    cls.addFunc("getPreset", &Weather::getPreset);
    cls.addFunc("setIntensity", &Weather::setIntensity);
    cls.addFunc("getIntensity", &Weather::getIntensity);
    cls.addFunc("setWindSpeed", &Weather::setWindSpeed);
    cls.addFunc("getWindSpeed", &Weather::getWindSpeed);
    cls.addFunc("setWindDirection", &Weather::setWindDirection);
    cls.addFunc("getWindDirection", &Weather::getWindDirection);
    cls.addFunc("setLightningEnabled", &Weather::setLightningEnabled);
    cls.addFunc("isLightningEnabled", &Weather::isLightningEnabled);
    cls.addFunc("strike", &Weather::strike);
    cls.addFunc("getFlash", &Weather::getFlash);
    cls.addFunc("setSkyColor", &Weather::setSkyColor);
    cls.addFunc("getSkyColorR", &Weather::getSkyColorR);
    cls.addFunc("getSkyColorG", &Weather::getSkyColorG);
    cls.addFunc("getSkyColorB", &Weather::getSkyColorB);
    cls.addFunc("setSunIntensity", &Weather::setSunIntensity);
    cls.addFunc("getSunIntensity", &Weather::getSunIntensity);
    cls.addFunc("setFogColor", &Weather::setFogColor);
    cls.addFunc("getFogColorR", &Weather::getFogColorR);
    cls.addFunc("getFogColorG", &Weather::getFogColorG);
    cls.addFunc("getFogColorB", &Weather::getFogColorB);
    cls.addFunc("setFogDensity", &Weather::setFogDensity);
    cls.addFunc("getFogDensity", &Weather::getFogDensity);
    cls.addFunc("setEnvironmentEnabled", &Weather::setEnvironmentEnabled);
    cls.addFunc("isEnvironmentEnabled", &Weather::isEnvironmentEnabled);
    cls.addFunc("getAmbientBrightness", &Weather::getAmbientBrightness);
}

Module_IMPL(Weather, new Weather());

}  // namespace eve::weather
