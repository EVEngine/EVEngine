#include "graphics/Volumetric.h"

#include "common/ECS.h"
#include "common/Exception.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/gtc/matrix_inverse.hpp>

namespace eve::graphics {
namespace {

float packShadowAnisotropy(float shadowSteps, float anisotropy) {
    const float steps = std::clamp(std::floor(shadowSteps + 0.5f), 0.f, 32.f);
    const float g = std::clamp(anisotropy, -0.99f, 0.99f);
    // fract in [0.005, 0.995] so a pure integer upload still reads as legacy g=0.6.
    const float frac = (g + 0.99f) / 1.98f;
    return steps + std::clamp(frac, 0.005f, 0.995f);
}

bool projectWorldToScreenUV(const glm::mat4 &viewProj, const glm::vec3 &world, float &u,
                            float &v) {
    const glm::vec4 clip = viewProj * glm::vec4(world, 1.f);
    if (std::fabs(clip.w) < 1e-6f) return false;
    const float ndcX = clip.x / clip.w;
    const float ndcY = clip.y / clip.w;
    // Match Vulkan Y-down UV used by volumetric shaders.
    u = ndcX * 0.5f + 0.5f;
    v = 1.f - (ndcY * 0.5f + 0.5f);
    return std::isfinite(u) && std::isfinite(v);
}

struct PackedVol2D {
    Light2D::Data *data = nullptr;
    float score = 0.f;
};

void collectVolumetricLights2D(Canvas *canvasFilter, std::vector<PackedVol2D> &out) {
    out.clear();
    if (ecs::current()->getManager<Light2D>() == nullptr) return;
    auto view = ecs::View<Light2D, Light2D::Data>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [d] = *it;
        if (!d->enabled || !d->volumetric) continue;
        if (d->canvas != canvasFilter) continue;
        PackedVol2D pl;
        pl.data = d;
        pl.score = d->intensity * d->volumetricIntensity *
            std::max({d->r, d->g, d->b, 0.f});
        out.push_back(pl);
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const PackedVol2D &a, const PackedVol2D &b) { return a.score > b.score; });
}

void sendShadowAnisotropy(Shader *rayShader, float shadowSteps, float anisotropy) {
    if (!rayShader || !rayShader->hasUniform("shadowAnisoPack")) return;
    rayShader->sendFloat("shadowAnisoPack", packShadowAnisotropy(shadowSteps, anisotropy));
}

}  // namespace

void Volumetric::uploadRayMarchShadowAnisotropy() {
    if (!rayShader_ || !rayShader_->hasUniform("shadowAnisoPack")) return;
    float shadowSteps = 8.f;
    float existing = 0.f;
    if (rayShader_->getFromVar("shadowAnisoPack", &existing, sizeof(existing)) ==
        int(sizeof(existing)))
        shadowSteps = std::floor(existing);
    sendShadowAnisotropy(rayShader_, shadowSteps, anisotropy_);
}

void Volumetric::clearPendingEmissiveProxies() { pendingEmissiveProxies_.clear(); }

void Volumetric::setAnisotropy(float g) {
    anisotropy_ = std::clamp(g, -0.99f, 0.99f);
    uploadRayMarchShadowAnisotropy();
}

float Volumetric::getAnisotropy() const { return anisotropy_; }

Result<int> Volumetric::collectSceneLights3D(std::vector<VolumetricLight> &out, int maxCount) {
    out.clear();
    if (maxCount < 1)
        return Result<int>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Volumetric.collectSceneLights3D: maxCount must be >= 1"));
    if (ecs::current()->getManager<Light3D>() == nullptr) return Result<int>::success(0);

    struct Scored {
        VolumetricLight light;
        float score = 0.f;
    };
    std::vector<Scored> scored;
    auto view = ecs::View<Light3D, Light3D::Data>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [d] = *it;
        if (!d->enabled || !d->volumetric) continue;
        if (d->type == "dir") {
            // Directional lights are handled by driveFromLight3D / uniform integrate;
            // local froxel lights need a position, so skip dirs here.
            continue;
        }
        VolumetricLight light;
        light.position = glm::vec3(d->x, d->y, d->z);
        light.color = glm::vec3(std::max(d->r, 0.f), std::max(d->g, 0.f), std::max(d->b, 0.f));
        light.radius = std::max(d->radius, 0.f);
        light.intensity = std::max(d->intensity, 0.f) * std::max(d->volumetricIntensity, 0.f);
        light.enabled = true;
        Scored s;
        s.light = light;
        s.score = light.intensity * std::max({light.color.x, light.color.y, light.color.z});
        scored.push_back(s);
    }
    std::stable_sort(scored.begin(), scored.end(),
                     [](const Scored &a, const Scored &b) { return a.score > b.score; });
    const int n = std::min(int(scored.size()), maxCount);
    out.reserve(std::size_t(n));
    for (int i = 0; i < n; ++i) out.push_back(scored[std::size_t(i)].light);
    return Result<int>::success(n);
}

Result<void> Volumetric::driveFromLight3D(Light3D *light, float viewportW, float viewportH) {
    if (!light)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "Volumetric.driveFromLight3D: null light"));
    if (!(viewportW > 0.f) || !(viewportH > 0.f) || !std::isfinite(viewportW) ||
        !std::isfinite(viewportH))
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Volumetric.driveFromLight3D: viewport must be positive"));

    auto d = light->data();
    if (!d->enabled)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "Volumetric.driveFromLight3D: light disabled"));

    const float volScale = std::max(d->volumetricIntensity, 0.f);
    setShaftColor(std::max(d->r, 0.f), std::max(d->g, 0.f), std::max(d->b, 0.f));
    setIntensity(std::max(d->intensity, 0.f) * volScale);

    if (d->type == "dir") {
        setLightDirection(d->dx, d->dy, d->dz);
        // Place the SS occlusion target toward the light direction on the far sky.
        const glm::mat4 viewProj = glm::inverse(invViewProj_);
        const glm::vec4 eyeH = invViewProj_ * glm::vec4(0.f, 0.f, 0.f, 1.f);
        const glm::vec3 eye = glm::vec3(eyeH) / std::max(eyeH.w, 1e-6f);
        const glm::vec3 towardLight = eye + glm::normalize(glm::vec3(d->dx, d->dy, d->dz)) * farZ_;
        float u = 0.7f, v = 0.2f;
        if (projectWorldToScreenUV(viewProj, towardLight, u, v))
            setLightScreenUV(std::clamp(u, 0.f, 1.f), std::clamp(v, 0.f, 1.f));
        return Result<void>::success();
    }

    const glm::mat4 viewProj = glm::inverse(invViewProj_);
    const glm::vec4 eyeH = invViewProj_ * glm::vec4(0.f, 0.f, 0.f, 1.f);
    const glm::vec3 eye = glm::vec3(eyeH) / std::max(eyeH.w, 1e-6f);
    const glm::vec3 lightPos(d->x, d->y, d->z);
    const glm::vec3 toLight = lightPos - eye;
    if (glm::dot(toLight, toLight) > 1e-8f)
        setLightDirection(toLight.x, toLight.y, toLight.z);
    float u = 0.5f, v = 0.5f;
    if (projectWorldToScreenUV(viewProj, lightPos, u, v))
        setLightScreenUV(std::clamp(u, -0.5f, 1.5f), std::clamp(v, -0.5f, 1.5f));
    (void)viewportW;
    (void)viewportH;
    return Result<void>::success();
}

Result<void> Volumetric::driveFromPrimarySceneLight3D(float viewportW, float viewportH) {
    if (ecs::current()->getManager<Light3D>() == nullptr)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::NotFound, "Volumetric.driveFromPrimarySceneLight3D: no Light3D manager"));

    Light3D *best = nullptr;
    float bestScore = -1.f;
    auto view = ecs::View<Light3D, Light3D::Data>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [d] = *it;
        if (!d->enabled || !d->volumetric || !d->entity) continue;
        const float score = d->intensity * d->volumetricIntensity *
            std::max({d->r, d->g, d->b, 0.f});
        if (score > bestScore) {
            bestScore = score;
            best = d->entity;
        }
    }
    if (!best)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::NotFound,
            "Volumetric.driveFromPrimarySceneLight3D: no volumetric Light3D"));
    return driveFromLight3D(best, viewportW, viewportH);
}

Result<void> Volumetric::integrateFroxelFromSceneLights(float ambientR, float ambientG,
                                                        float ambientB, const glm::vec3 &worldMin,
                                                        const glm::vec3 &worldMax, int maxLights) {
    if (!atmosphereVolume_)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::Failed, "Volumetric.integrateFroxelFromSceneLights: no atmosphere volume"));
    if (maxLights < 1)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument,
            "Volumetric.integrateFroxelFromSceneLights: maxLights must be >= 1"));

    std::vector<VolumetricLight> lights;
    auto collected = collectSceneLights3D(lights, maxLights);
    if (!collected.ok()) return Result<void>::failure(collected.status());

    for (const VolumetricLight &proxy : pendingEmissiveProxies_) {
        if (int(lights.size()) >= maxLights) break;
        lights.push_back(proxy);
    }
    clearPendingEmissiveProxies();

    const glm::vec3 ambient(std::max(ambientR, 0.f), std::max(ambientG, 0.f),
                            std::max(ambientB, 0.f));
    if (lights.empty()) {
        atmosphereVolume_->integrate(ambient, 1.f);
        return Result<void>::success();
    }
    atmosphereVolume_->integrateLocalLights(lights, worldMin, worldMax, ambient);
    return Result<void>::success();
}

Result<int> Volumetric::beginOcclusionMapFromSceneLights2D(Graphics *gfx, Canvas *canvasFilter,
                                                           float defaultRadiusPixels) {
    if (!gfx)
        return Result<int>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument,
            "Volumetric.beginOcclusionMapFromSceneLights2D: null graphics"));

    std::vector<PackedVol2D> lights;
    collectVolumetricLights2D(canvasFilter, lights);
    gfx->clear(Color(0.f, 0.f, 0.f, 1.f), std::nullopt, std::nullopt);
    if (lights.empty()) return Result<int>::success(0);

    const float w = gfx->getCanvas() ? float(gfx->getCanvas()->getWidth()) : float(gfx->getWidth());
    const float h = gfx->getCanvas() ? float(gfx->getCanvas()->getHeight()) : float(gfx->getHeight());
    const float baseR = std::max(defaultRadiusPixels, 1.f);

    // Primary light drives the single-light UV used by scatter(); others still
    // contribute bright cores so multi-source applyFromScene / multipass works.
    const auto *primary = lights.front().data;
    setLightScreenPos(primary->x, primary->y, w, h);
    setShaftColor(std::max(primary->r, 0.f), std::max(primary->g, 0.f),
                  std::max(primary->b, 0.f));
    setIntensity(std::max(primary->intensity, 0.f) * std::max(primary->volumetricIntensity, 0.f));

    int drawn = 0;
    for (const PackedVol2D &pl : lights) {
        const auto *d = pl.data;
        const float boost = std::clamp(d->volumetricIntensity, 0.25f, 4.f);
        const float r = std::max(baseR * boost, 1.f);
        const float lum = std::clamp(std::max({d->r, d->g, d->b}) * d->intensity * boost, 0.35f,
                                     1.f);
        gfx->drawSolidRect(d->x - r, d->y - r, r * 2.f, r * 2.f,
                           Color(lum, lum, lum, 1.f));
        ++drawn;
    }
    return Result<int>::success(drawn);
}

Result<int> Volumetric::scatterFromSceneLights2D(Graphics *gfx, Texture *occlusion,
                                                 Canvas *canvasFilter) {
    if (!gfx || !occlusion)
        return Result<int>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument,
            "Volumetric.scatterFromSceneLights2D: graphics and occlusion required"));

    std::vector<PackedVol2D> lights;
    collectVolumetricLights2D(canvasFilter, lights);
    if (lights.empty()) return Result<int>::success(0);

    const float w = gfx->getCanvas() ? float(gfx->getCanvas()->getWidth()) : float(gfx->getWidth());
    const float h = gfx->getCanvas() ? float(gfx->getCanvas()->getHeight()) : float(gfx->getHeight());

    int passes = 0;
    for (const PackedVol2D &pl : lights) {
        const auto *d = pl.data;
        setLightScreenPos(d->x, d->y, w, h);
        setShaftColor(std::max(d->r, 0.f), std::max(d->g, 0.f), std::max(d->b, 0.f));
        setIntensity(std::max(d->intensity, 0.f) * std::max(d->volumetricIntensity, 0.f));
        scatter(gfx, occlusion);
        ++passes;
    }
    return Result<int>::success(passes);
}

Result<void> Volumetric::injectEmissiveLightProxy(float x, float y, float z, float r, float g,
                                                  float b, float radius, float intensity) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(r) ||
        !std::isfinite(g) || !std::isfinite(b) || !std::isfinite(radius) ||
        !std::isfinite(intensity))
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument,
            "Volumetric.injectEmissiveLightProxy: finite parameters required"));
    if (radius <= 0.f || intensity < 0.f)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument,
            "Volumetric.injectEmissiveLightProxy: radius > 0 and intensity >= 0 required"));

    VolumetricLight light;
    light.position = glm::vec3(x, y, z);
    light.color = glm::vec3(std::max(r, 0.f), std::max(g, 0.f), std::max(b, 0.f));
    light.radius = radius;
    light.intensity = intensity;
    light.enabled = true;
    pendingEmissiveProxies_.push_back(light);
    return Result<void>::success();
}

}  // namespace eve::graphics
