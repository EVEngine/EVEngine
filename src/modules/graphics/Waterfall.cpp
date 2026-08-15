#include "graphics/Waterfall.h"

#include "graphics/Graphics.h"
#include "graphics/Mesh.h"

#include <cmath>
#include <string>
#include <vector>

namespace eve::graphics {

namespace {

// Push-constant layout (data[32]):
//   0 time, 1 flowSpeed, 2 turbulence, 3 streakCount, 4 streakScale,
//   5 topFoam, 6 bottomFoam, 7 foamAmt, 8 reflIntensity,
//   9 sunIntensity, 10..12 waterColor.
const char *kFallShaderFrag = R"GLSL(#version 450
layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUV;
layout(location = 2) in vec4 vTint;
layout(location = 3) in vec3 vWorldPos;
layout(location = 4) in vec3 vCameraPos;
layout(location = 5) in vec3 vViewPos;

struct Light3D { vec4 posRadius; vec4 color; };
layout(set = 0, binding = 0, std140) uniform Frame {
    mat4 mvp;
    mat4 model;
    vec4 lightDirIntensity;
    vec4 lightColor;
    vec4 tint;
    vec4 cameraPos;
    vec4 ambient;
    Light3D lights[8];
    vec4 texBomb;
    vec4 parallax;
    mat4 view;
    vec4 clipInfo;
} ubo;

layout(set = 0, binding = 1) uniform sampler2D albedo;
layout(set = 0, binding = 3) uniform samplerCube env;

layout(push_constant) uniform Externals { float data[32]; } u;
layout(location = 0) out vec4 outColor;

const float PI = 3.14159265;

// Classic pseudo-random value noise.
float hash(float n) { return fract(sin(n) * 43758.5453123); }
float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 s = f * f * (3.0 - 2.0 * f);
    float a = hash(i.x + i.y * 57.0);
    float b = hash(i.x + 1.0 + i.y * 57.0);
    float c = hash(i.x + (i.y + 1.0) * 57.0);
    float d = hash(i.x + 1.0 + (i.y + 1.0) * 57.0);
    return mix(mix(a, b, s.x), mix(c, d, s.x), s.y);
}

// 2D fractal noise, driven by a downward-scrolling coordinate so the water
// always flows toward the pool at the bottom.
float flowNoise(vec2 uv, float t) {
    float speed = u.data[1];            // flowSpeed
    vec2 q = vec2(uv.x * 3.0, uv.y * 4.0 - t * speed);
    float n = 0.0;
    float amp = 0.5;
    float freq = 1.0;
    for (int o = 0; o < 4; ++o) {
        n += amp * noise(q * freq + float(o) * 13.7);
        freq *= 2.1;
        amp *= 0.5;
    }
    return n;
}

// Vertical flowing streaks: elongated in the fall direction by streakScale.
float streaks(vec2 uv, float t) {
    float scale = u.data[4];            // streakScale
    int n = int(u.data[3] + 0.5);       // streakCount
    float s = 0.0;
    for (int i = 0; i < 6; ++i) {
        if (i >= n) break;
        float x = fract(hash(float(i) * 3.1) + uv.x * 0.35) - 0.5;
        float phase = hash(float(i) * 9.7) * 10.0;
        float y = (uv.y - 0.5 - t * u.data[1] * 0.6) * scale + phase;
        float dx = exp(-abs(x) * 3.5);
        float dy = exp(-abs(y) * 1.5);
        s += dx * dy;
    }
    return s;
}

// Turbulent white-water crest intensity over the sheet (streaks + noise).
float cascade(vec2 uv, float t) {
    float turb = u.data[2];             // turbulence
    float n = flowNoise(uv, t);
    float st = streaks(uv, t);
    // Bands that slide downward, wider near the bottom like a churning fall.
    float band = 0.5 + 0.5 * sin((uv.y * 22.0 - t * u.data[1] * 3.0) * PI);
    float v = n * 0.6 + st * (0.5 + turb * 0.5) + band * 0.2;
    return clamp(v * turb, 0.0, 1.0);
}

void main() {
    float t = u.data[0];

    // Surface normal from the analytic displacement (finite differences on a
    // function of the unscrolled coordinate so streaks read as geometry bumps).
    vec2 p0 = vUV;
    float eps = 1e-3;
    vec2 pL = p0 - vec2(eps, 0.0);
    vec2 pR = p0 + vec2(eps, 0.0);
    vec2 pD = p0 - vec2(0.0, eps);
    vec2 pU = p0 + vec2(0.0, eps);
    float h  = cascade(p0, t);
    float hL = cascade(pL, t);
    float hR = cascade(pR, t);
    float hD = cascade(pD, t);
    float hU = cascade(pU, t);
    vec2 grad = vec2((hR - hL) / (2.0 * eps), (hU - hD) / (2.0 * eps));
    vec3 N = normalize(vec3(-grad.x, -grad.y, 1.0));   // plane faces +Z

    vec3 V = normalize(ubo.cameraPos.xyz - vWorldPos);
    vec3 R = reflect(-V, N);

    // Sky reflection via the env cubemap, Fresnel-weighted.
    float ndv = max(dot(V, N), 0.0);
    float lod = 1.0 + (1.0 - ndv) * 3.0;
    vec3 refl = textureLod(env, R, lod).rgb;
    float fresnel = 0.02 + 0.98 * pow(1.0 - ndv, 5.0);

    vec3 waterCol = vec3(u.data[10], u.data[11], u.data[12]);
    float reflectAmt = clamp(fresnel * u.data[8] + 0.05, 0.0, 1.0);
    vec3 color = mix(waterCol, refl, reflectAmt);

    // Sun glint highlight.
    vec3 L = normalize(ubo.lightDirIntensity.xyz);
    vec3 H = normalize(V + L);
    float spec = pow(max(dot(N, H), 0.0), 96.0);
    color += ubo.lightColor.rgb * spec * u.data[9];

    // White-water foam: crests in the body plus foam bands at the top lip and
    // the splash pool at the bottom.
    float foam = cascade(p0, t) * u.data[7];
    float top = u.data[5];                              // topFoam
    float bottom = u.data[6];                           // bottomFoam
    float topBand = clamp((top - p0.y) / max(top, 1e-4), 0.0, 1.0);
    float bottomBand = clamp((p0.y - (1.0 - bottom)) / max(bottom, 1e-4), 0.0, 1.0);
    float edge = clamp(topBand + bottomBand, 0.0, 1.0);
    float foamy = clamp(foam + edge * 0.9, 0.0, 1.0);
    vec3 foamCol = vec3(0.90, 0.95, 1.0);
    color = mix(color, foamCol, foamy);

    // Fade to slightly transparent at the very bottom so it melts into the pool.
    outColor = vec4(color, 1.0 - bottomBand * 0.5);
}
)GLSL";

const char *kUniformNames[] = {
    "time", "flowSpeed", "turbulence", "streakCnt", "streakScale",
    "topFoam", "bottomFoam", "foamAmt", "reflInten", "sunInten", "waterCol",
};
const int kUniformCount = int(sizeof(kUniformNames) / sizeof(kUniformNames[0]));

}  // namespace

Shader *newWaterfallShader(Graphics *gfx) {
    Shader *sh = gfx->newMeshShader("", kFallShaderFrag);
    for (int i = 0; i < kUniformCount; ++i) {
        if (std::string(kUniformNames[i]) == "waterCol")
            sh->declareVec3(kUniformNames[i]);
        else
            sh->declareFloat(kUniformNames[i]);
    }
    return sh;
}

int Waterfall::paramCount() { return kUniformCount; }

std::string Waterfall::paramName(int index) {
    if (index < 0 || index >= kUniformCount) return {};
    return kUniformNames[index];
}

Waterfall::Waterfall(Graphics *gfx) : gfx_(gfx) {
    shader_ = newWaterfallShader(gfx);
    bindParams();
}

void Waterfall::createSheet(float width, float height, int segX, int segY) {
    segX = std::max(1, segX);
    segY = std::max(1, segY);
    std::vector<float> pos, nrm, uv;
    std::vector<uint32_t> idx;
    for (int y = 0; y <= segY; ++y) {
        for (int x = 0; x <= segX; ++x) {
            pos.push_back(-width * 0.5f + width * float(x) / segX);
            pos.push_back(-height * 0.5f + height * float(y) / segY);
            pos.push_back(0.f);
            nrm.push_back(0.f);
            nrm.push_back(0.f);
            nrm.push_back(1.f);
            uv.push_back(float(x) / segX);
            uv.push_back(float(y) / segY);
        }
    }
    for (int y = 0; y < segY; ++y) {
        for (int x = 0; x < segX; ++x) {
            const uint32_t a = uint32_t(y * (segX + 1) + x);
            const uint32_t b = a + 1;
            const uint32_t c = a + uint32_t(segX + 1);
            const uint32_t d = c + 1;
            idx.push_back(a);
            idx.push_back(b);
            idx.push_back(c);
            idx.push_back(b);
            idx.push_back(d);
            idx.push_back(c);
        }
    }
    mesh_ = gfx_->newMeshFromArrays(pos.data(), nrm.data(), uv.data(), int(pos.size() / 3),
                                    idx.data(), int(idx.size()));
}

void Waterfall::update(float dt) {
    time_ += dt;
    bindParams();
}

void Waterfall::setTime(float seconds) {
    time_ = seconds;
    bindParams();
}

void Waterfall::setFlowSpeed(float v) { flowSpeed_ = v; }
void Waterfall::setTurbulence(float v) { turbulence_ = std::max(0.f, v); }
void Waterfall::setStreakCount(int v) { streakCount_ = std::max(0, v); }
void Waterfall::setStreakScale(float v) { streakScale_ = std::max(0.1f, v); }
void Waterfall::setTopFoam(float v) { topFoam_ = std::clamp(v, 0.001f, 0.5f); }
void Waterfall::setBottomFoam(float v) { bottomFoam_ = std::clamp(v, 0.001f, 0.5f); }
void Waterfall::setFoamAmount(float v) { foamAmount_ = std::max(0.f, v); }
void Waterfall::setWaterColor(float r, float g, float b) {
    waterColor_[0] = r;
    waterColor_[1] = g;
    waterColor_[2] = b;
}
void Waterfall::setReflectionIntensity(float v) { reflectionIntensity_ = std::max(0.f, v); }
void Waterfall::setSunIntensity(float v) { sunIntensity_ = std::max(0.f, v); }

void Waterfall::bindParams() {
    if (!shader_) return;
    shader_->sendFloat("time", time_);
    shader_->sendFloat("flowSpeed", flowSpeed_);
    shader_->sendFloat("turbulence", turbulence_);
    shader_->sendFloat("streakCnt", float(streakCount_));
    shader_->sendFloat("streakScale", streakScale_);
    shader_->sendFloat("topFoam", topFoam_);
    shader_->sendFloat("bottomFoam", bottomFoam_);
    shader_->sendFloat("foamAmt", foamAmount_);
    shader_->sendFloat("reflInten", reflectionIntensity_);
    shader_->sendFloat("sunInten", sunIntensity_);
    shader_->sendVec3("waterCol", waterColor_[0], waterColor_[1], waterColor_[2]);
}

void Waterfall::draw() {
    if (!gfx_ || !mesh_ || !shader_) return;
    gfx_->drawMeshShader(mesh_, glm::mat4(1.f), nullptr, glm::vec4(1.f), shader_);
}

}  // namespace eve::graphics
