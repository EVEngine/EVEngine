#include "graphics/Shadow.h"

#include "graphics/ClipSpace.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace eve::graphics {

namespace {

glm::mat4 cascadeVP(const glm::vec3 &lightDir, const glm::vec3 &eye, const glm::vec3 &target,
                    const glm::vec3 &up, float fovYRad, float aspect, float nearZ, float farZ,
                    float *texelWorldOut) {
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

    glm::vec3 center(0.f);
    for (int i = 0; i < 8; ++i) center += corners[i];
    center *= 0.125f;

    glm::vec3 L = glm::normalize(lightDir);
    if (glm::length(L) < 1e-6f) L = glm::vec3(0.f, 1.f, 0.f);
    // Light looks along -L (from light toward scene). Place eye along +L from center.
    const float radius = [&]() {
        float r = 0.f;
        for (int i = 0; i < 8; ++i) r = std::max(r, glm::length(corners[i] - center));
        return std::max(r, 0.5f);
    }();

    glm::vec3 lightUp(0.f, 1.f, 0.f);
    if (std::abs(glm::dot(L, lightUp)) > 0.95f) lightUp = glm::vec3(0.f, 0.f, 1.f);
    const glm::mat4 lightView = glm::lookAtRH(center + L * radius, center, lightUp);

    glm::vec3 minB(1e9f), maxB(-1e9f);
    for (int i = 0; i < 8; ++i) {
        const glm::vec3 lp = glm::vec3(lightView * glm::vec4(corners[i], 1.f));
        minB = glm::min(minB, lp);
        maxB = glm::max(maxB, lp);
    }
    // Extend depth a bit so casters slightly outside the slice still contribute.
    const float zPad = (maxB.z - minB.z) * 0.25f + 1.f;
    minB.z -= zPad;
    maxB.z += zPad;

    const float mapSize = float(ShadowConfig::kMapSize);
    const float extentX = std::max(maxB.x - minB.x, 1e-3f);
    const float extentY = std::max(maxB.y - minB.y, 1e-3f);
    const float texelWorld = std::max(extentX, extentY) / mapSize;
    if (texelWorldOut) *texelWorldOut = texelWorld;

    // Snap ortho bounds to texel-sized increments so cascades don't swim as the camera moves.
    auto snap = [&](float v) { return std::floor(v / texelWorld) * texelWorld; };
    minB.x = snap(minB.x);
    minB.y = snap(minB.y);
    maxB.x = snap(maxB.x) + texelWorld;
    maxB.y = snap(maxB.y) + texelWorld;

    const glm::mat4 lightProj =
        orthoVulkanRH_ZO(minB.x, maxB.x, minB.y, maxB.y, -maxB.z, -minB.z);
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

    float maxTexelWorld = 0.f;
    for (int i = 0; i < ShadowConfig::kCascades; ++i) {
        float texelWorld = 0.f;
        out.ubo.lightVP[i] =
            cascadeVP(lightDirTowardSurface, eye, target, up, fovYRad, aspect, splits[i],
                      splits[i + 1], &texelWorld);
        maxTexelWorld = std::max(maxTexelWorld, texelWorld);
    }
    out.ubo.splits = glm::vec4(splits[1], splits[2], splits[3], std::max(0.f, strength));
    // Depth-compare bias in NDC. Scale with world texel size so large cascades don't acne
    // while Cornell-sized frustums keep a small constant. With 2048 maps the texel is
    // half of the 1024-era size, so the coefficient is scaled down accordingly.
    const float scaledBias =
        std::max(std::max(0.f, bias), 0.0008f + maxTexelWorld * 0.008f);
    out.ubo.bias = glm::vec4(scaledBias, 1.f, 1.f, 0.f);
    return out;
}

}  // namespace eve::graphics
