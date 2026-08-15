#include "graphics/Water.h"

#include "graphics/Graphics.h"
#include "graphics/Mesh.h"

#include <cmath>
#include <string>
#include <vector>

namespace eve::graphics {

namespace {

// Push-constant layout (data[32]):
//   0 time, 1 waveSpeed, 2 waveAmp, 3 rippleAmp, 4 edgeFalloff,
//   5 reflIntensity, 6 rippleCount, 7 rippleInterval, 8 waveScale,
//   9..11 waterColor, 12..14 reflTint, 15 sunIntensity.
const char *kWtrShaderFrag = R"GLSL(#version 450
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

float hash(float n) { return fract(sin(n) * 43758.5453123); }
vec2  hash2(int i) { return vec2(hash(float(i) * 7.31), hash(float(i) * 13.17 + 1.0)); }

// Expanding ring from a periodic drop at a near-center position.
float rippleRing(vec2 uv, int i) {
    float period = max(u.data[7], 1e-3);        // rippleInterval
    float local = mod(u.data[0], period);
    float startPhase = hash(float(i) * 3.7) * period;
    float age = local - startPhase;
    if (age < 0.0) return 0.0;
    float life = period * 0.7;
    if (age > life) return 0.0;
    vec2 center = hash2(i);
    center = mix(vec2(0.5), center, 0.72);      // keep drops near the middle
    vec2 d = uv - center;
    float r = length(d);
    float radius = age * 0.5;                   // expand outward
    float band = 0.055;
    float ring = 1.0 - clamp(abs(r - radius) / band, 0.0, 1.0);
    ring *= ring;
    float env = exp(-age * 1.6);                // fade out
    return ring * env * u.data[3];              // rippleAmp
}

// Water height displacement over UV.
float waterHeight(vec2 uv) {
    float t = u.data[0];
    float ws = u.data[8];
    vec2 edgeDist = min(uv, vec2(1.0) - uv);
    float edgeFactor = 1.0 - clamp(min(edgeDist.x, edgeDist.y) / max(u.data[4], 1e-4), 0.0, 1.0);
    float w = 0.0;
    // Shore-edge waves, strongest at the border and fading inward.
    w += edgeFactor * u.data[2] * (sin((uv.x * ws + t * u.data[1]) * PI * 2.0) +
                                   0.5 * sin((uv.y * ws * 0.7 - t * u.data[1] * 1.3) * PI * 2.0));
    // Fine detail everywhere.
    w += u.data[2] * 0.10 * sin((uv.x * 31.0 + uv.y * 17.0 + t * u.data[1] * 2.0) * PI * 2.0);
    // Occasional middle drop ripples.
    int n = int(u.data[6] + 0.5);
    for (int i = 0; i < 8; ++i) {
        if (i >= n) break;
        w += rippleRing(uv, i);
    }
    return w;
}

void main() {
    // Surface normal from the analytic displacement (finite differences).
    float eps = 1e-3;
    float hL = waterHeight(vUV - vec2(eps, 0.0));
    float hR = waterHeight(vUV + vec2(eps, 0.0));
    float hD = waterHeight(vUV - vec2(0.0, eps));
    float hU = waterHeight(vUV + vec2(0.0, eps));
    vec2 grad = vec2((hR - hL) / (2.0 * eps), (hU - hD) / (2.0 * eps));
    vec3 N = normalize(vec3(-grad.x, 1.0, -grad.y));

    vec3 V = normalize(ubo.cameraPos.xyz - vWorldPos);
    vec3 R = reflect(-V, N);

    // Sky reflection via the env cubemap, blurred more at grazing angles.
    // Clamp LOD to available mip levels so a non-mipmapped env is still safe.
    float ndv = max(dot(V, N), 0.0);
    float maxLod = float(max(textureQueryLevels(env) - 1, 0));
    float lod = min(1.0 + (1.0 - ndv) * 4.0, maxLod);
    vec3 refl = textureLod(env, R, lod).rgb * vec3(u.data[12], u.data[13], u.data[14]);
    float fresnel = 0.02 + 0.98 * pow(1.0 - ndv, 5.0);

    vec3 waterCol = vec3(u.data[9], u.data[10], u.data[11]);
    float reflectAmt = clamp(fresnel * u.data[5] + 0.30, 0.0, 1.0);
    vec3 color = mix(waterCol, refl, reflectAmt);

    // Sun glint highlight.
    vec3 L = normalize(ubo.lightDirIntensity.xyz);
    vec3 H = normalize(V + L);
    float spec = pow(max(dot(N, H), 0.0), 96.0);
    color += ubo.lightColor.rgb * spec * u.data[15];

    // Soft foam where waves meet the edge.
    vec2 edgeDist = min(vUV, vec2(1.0) - vUV);
    float edgeFactor = 1.0 - clamp(min(edgeDist.x, edgeDist.y) / max(u.data[4], 1e-4), 0.0, 1.0);
    color = mix(color, vec3(0.85, 0.93, 1.0), edgeFactor * 0.22);

    outColor = vec4(color, 1.0);
}
)GLSL";

const char *kUniformNames[] = {
    "time",      "waveSpeed", "waveAmp",  "rippleAmp",   "edgeFalloff",
    "reflInten", "rippleCnt", "rippleInt", "waveScale",   "waterCol",
    "reflTint",  "sunInten",
};
const int kUniformCount = int(sizeof(kUniformNames) / sizeof(kUniformNames[0]));

}  // namespace

Shader *newWaterShader(Graphics *gfx) {
    Shader *sh = gfx->newMeshShader("", kWtrShaderFrag);
    for (int i = 0; i < kUniformCount; ++i) {
        if (std::string(kUniformNames[i]) == "waterCol")
            sh->declareVec3(kUniformNames[i]);
        else if (std::string(kUniformNames[i]) == "reflTint")
            sh->declareVec3(kUniformNames[i]);
        else
            sh->declareFloat(kUniformNames[i]);
    }
    return sh;
}

int Water::paramCount() { return kUniformCount; }

std::string Water::paramName(int index) {
    if (index < 0 || index >= kUniformCount) return {};
    return kUniformNames[index];
}

Water::Water(Graphics *gfx) : gfx_(gfx) {
    shader_ = newWaterShader(gfx);
    bindParams();
}

void Water::createPlane(float sizeX, float sizeZ, int segX, int segZ) {
    segX = std::max(1, segX);
    segZ = std::max(1, segZ);
    std::vector<float> pos, nrm, uv;
    std::vector<uint32_t> idx;
    for (int z = 0; z <= segZ; ++z) {
        for (int x = 0; x <= segX; ++x) {
            pos.push_back(-sizeX * 0.5f + sizeX * float(x) / segX);
            pos.push_back(0.f);
            pos.push_back(-sizeZ * 0.5f + sizeZ * float(z) / segZ);
            nrm.push_back(0.f);
            nrm.push_back(1.f);
            nrm.push_back(0.f);
            uv.push_back(float(x) / segX);
            uv.push_back(float(z) / segZ);
        }
    }
    for (int z = 0; z < segZ; ++z) {
        for (int x = 0; x < segX; ++x) {
            const uint32_t a = uint32_t(z * (segX + 1) + x);
            const uint32_t b = a + 1;
            const uint32_t c = a + uint32_t(segX + 1);
            const uint32_t d = c + 1;
            idx.push_back(a);
            idx.push_back(c);
            idx.push_back(b);
            idx.push_back(b);
            idx.push_back(c);
            idx.push_back(d);
        }
    }
    mesh_ = gfx_->newMeshFromArrays(pos.data(), nrm.data(), uv.data(), int(pos.size() / 3),
                                    idx.data(), int(idx.size()));
}

void Water::update(float dt) {
    time_ += dt;
    bindParams();
}

void Water::setTime(float seconds) {
    time_ = seconds;
    bindParams();
}

void Water::setWaveSpeed(float v) { waveSpeed_ = v; }
void Water::setWaveAmplitude(float v) { waveAmplitude_ = v; }
void Water::setRippleAmplitude(float v) { rippleAmplitude_ = v; }
void Water::setEdgeFalloff(float v) { edgeFalloff_ = std::max(0.001f, v); }
void Water::setRippleCount(int v) { rippleCount_ = std::max(0, v); }
void Water::setRippleInterval(float v) { rippleInterval_ = std::max(0.05f, v); }
void Water::setWaveScale(float v) { waveScale_ = std::max(0.1f, v); }
void Water::setWaterColor(float r, float g, float b) {
    waterColor_[0] = r;
    waterColor_[1] = g;
    waterColor_[2] = b;
}
void Water::setReflectionTint(float r, float g, float b) {
    reflectionTint_[0] = r;
    reflectionTint_[1] = g;
    reflectionTint_[2] = b;
}
void Water::setReflectionIntensity(float v) { reflectionIntensity_ = std::max(0.f, v); }
void Water::setSunIntensity(float v) { sunIntensity_ = std::max(0.f, v); }

void Water::bindParams() {
    if (!shader_) return;
    shader_->sendFloat("time", time_);
    shader_->sendFloat("waveSpeed", waveSpeed_);
    shader_->sendFloat("waveAmp", waveAmplitude_);
    shader_->sendFloat("rippleAmp", rippleAmplitude_);
    shader_->sendFloat("edgeFalloff", edgeFalloff_);
    shader_->sendFloat("reflInten", reflectionIntensity_);
    shader_->sendFloat("rippleCnt", float(rippleCount_));
    shader_->sendFloat("rippleInt", rippleInterval_);
    shader_->sendFloat("waveScale", waveScale_);
    shader_->sendVec3("waterCol", waterColor_[0], waterColor_[1], waterColor_[2]);
    shader_->sendVec3("reflTint", reflectionTint_[0], reflectionTint_[1], reflectionTint_[2]);
    shader_->sendFloat("sunInten", sunIntensity_);
}

void Water::draw() {
    if (!gfx_ || !mesh_ || !shader_) return;
    gfx_->drawMeshShader(mesh_, glm::mat4(1.f), nullptr, glm::vec4(1.f), shader_);
}

}  // namespace eve::graphics
