#include "graphics/Shadow.h"

#include "graphics/ClipSpace.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace eve::graphics {

namespace {

glm::mat4 cascadeVP(const glm::vec3 &lightDir, const glm::vec3 &eye, const glm::vec3 &target,
                    const glm::vec3 &up, float fovYRad, float aspect, float nearZ, float farZ,
                    float *texelWorldOut, float *zRangeOut) {
    const glm::mat4 view = glm::lookAtRH(eye, target, up);
    // Match the camera projection used by RenderSystem3D so frustum corners align.
    const glm::mat4 proj = perspectiveVulkanRH_ZO(fovYRad, aspect, nearZ, farZ);
    const glm::mat4 inv = glm::inverse(proj * view);

    // NDC cube corners → world
    glm::vec3 corners[8];
    int idx = 0;
    for (int z = 0; z < 2; ++z)
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 2; ++x) {
                const glm::vec4 ndc(x ? 1.f : -1.f, y ? 1.f : -1.f, z ? 1.f : 0.f, 1.f);
                glm::vec4 w = inv * ndc;
                w /= w.w;
                corners[idx++] = glm::vec3(w);
            }

    glm::vec3 worldCenter(0.f);
    for (int i = 0; i < 8; ++i) worldCenter += corners[i];
    worldCenter *= 0.125f;
    float radius = 0.5f;
    for (int i = 0; i < 8; ++i)
        radius = std::max(radius, glm::length(corners[i] - worldCenter));

    glm::vec3 L = glm::normalize(lightDir);
    if (glm::length(L) < 1e-6f) L = glm::vec3(0.f, 1.f, 0.f);
    glm::vec3 lightUp(0.f, 1.f, 0.f);
    if (std::abs(glm::dot(L, lightUp)) > 0.95f) lightUp = glm::vec3(0.f, 0.f, 1.f);
    // Camera-independent view: rotation from L only. If lookAt follows the
    // frustum center, static receivers crawl in shadow UV as the camera moves
    // (Cornell floor: large jagged umbrae swimming/flickering).
    const glm::mat4 lightView = glm::lookAtRH(L * 50.f, glm::vec3(0.f), lightUp);

    glm::vec3 minB(1e9f), maxB(-1e9f);
    for (int i = 0; i < 8; ++i) {
        const glm::vec3 lp = glm::vec3(lightView * glm::vec4(corners[i], 1.f));
        minB = glm::min(minB, lp);
        maxB = glm::max(maxB, lp);
    }
    glm::vec3 centerLS = glm::vec3(lightView * glm::vec4(worldCenter, 1.f));

    const float zPad = (maxB.z - minB.z) * 0.1f + 0.5f;
    minB.z -= zPad;
    // Pad toward the light so casters just outside the view frustum still
    // write depth. Cornell: camera looks slightly down, the ceiling sits
    // above the cascade near plane and never occludes the left/top creases.
    maxB.z += zPad + 2.f;
    if (zRangeOut) *zRangeOut = std::max(-minB.z - (-maxB.z), 1e-3f);

    const float mapSize = float(ShadowConfig::kMapSize);
    // World-space frustum sphere radius is pose-invariant for a given split, so
    // the texel grid does not rescale as the camera rotates.
    const float texelWorld = (2.f * radius) / mapSize;
    if (texelWorldOut) *texelWorldOut = texelWorld;

    auto snap = [&](float v) { return std::floor(v / texelWorld) * texelWorld; };
    centerLS.x = snap(centerLS.x);
    centerLS.y = snap(centerLS.y);

    const glm::mat4 lightProj =
        orthoVulkanRH_ZO(centerLS.x - radius, centerLS.x + radius, centerLS.y - radius,
                         centerLS.y + radius, -maxB.z, -minB.z);
    return lightProj * lightView;
}

}  // namespace

ShadowUpload buildDirectionalCSM(const glm::vec3 &lightDirTowardSurface, const glm::vec3 &eye,
                                 const glm::vec3 &target, const glm::vec3 &up, float fovYRad,
                                 float aspect, float nearZ, float farZ, float bias, float strength) {
    ShadowUpload out{};
    out.active = true;
    const float n = std::max(nearZ, 1e-3f);
    const float fCam = std::max(farZ, n + 1e-2f);
    const float f = std::min(fCam, std::max(n + 1e-2f, ShadowConfig::kMaxDistance));
    const float ratio = f / n;
    float splits[ShadowConfig::kCascades + 1];
    splits[0] = n;
    for (int i = 1; i <= ShadowConfig::kCascades; ++i) {
        const float p = float(i) / float(ShadowConfig::kCascades);
        const float logS = n * std::pow(ratio, p);
        const float uniS = n + (f - n) * p;
        splits[i] = ShadowConfig::kSplitLambda * logS + (1.f - ShadowConfig::kSplitLambda) * uniS;
    }

    float cascadeNdc[ShadowConfig::kCascades]{};
    float cascadeTexel[ShadowConfig::kCascades]{};
    for (int i = 0; i < ShadowConfig::kCascades; ++i) {
        float texelWorld = 0.f;
        float zRange = 1.f;
        out.ubo.lightVP[i] =
            cascadeVP(lightDirTowardSurface, eye, target, up, fovYRad, aspect, splits[i],
                      splits[i + 1], &texelWorld, &zRange);
        // ~0.6 texels of world bias. More than that, projected onto a grazing
        // Cornell wall, becomes a multi-pixel bright rim on the left/top of
        // the umbra (peter-panning at the box/wall crease).
        const float autoNdc = std::clamp((0.6f * texelWorld) / zRange, 1e-5f, 0.01f);
        cascadeNdc[i] = std::max(std::max(0.f, bias), autoNdc);
        cascadeTexel[i] = texelWorld;
    }
    const float perceptualStrength = std::sqrt(std::clamp(strength, 0.f, 1.f));
    out.ubo.splits = glm::vec4(splits[1], splits[2], splits[3], perceptualStrength);
    out.ubo.cascadeBias = glm::vec4(cascadeNdc[0], cascadeNdc[1], cascadeNdc[2], 0.f);
    out.ubo.cascadeTexel = glm::vec4(cascadeTexel[0], cascadeTexel[1], cascadeTexel[2], 0.f);
    out.ubo.bias = glm::vec4(cascadeNdc[0], 1.f, 1.f, 0.02f);
    return out;
}

}  // namespace eve::graphics
