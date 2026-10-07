#include "graphics/ShadowScheme.h"

#include "common/Diagnostic.h"
#include "graphics/ClipSpace.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace eve::graphics {
namespace {

ShadowSchemeSettings g_shadowSchemeSettings{};

bool equalsIgnoreCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const char ca = (a[i] >= 'A' && a[i] <= 'Z') ? char(a[i] - 'A' + 'a') : a[i];
        const char cb = (b[i] >= 'A' && b[i] <= 'Z') ? char(b[i] - 'A' + 'a') : b[i];
        if (ca != cb) return false;
    }
    return true;
}

glm::vec3 safeNormalize(const glm::vec3& v, const glm::vec3& fallback) {
    const float len = glm::length(v);
    return len > 1e-6f ? v / len : fallback;
}

}  // namespace

ShadowSchemeSettings& ShadowSchemeSettings::current() { return g_shadowSchemeSettings; }

Result<ShadowMethod> parseShadowMethod(std::string_view name) {
    if (equalsIgnoreCase(name, "auto")) return Result<ShadowMethod>::success(ShadowMethod::Auto);
    if (equalsIgnoreCase(name, "none") || equalsIgnoreCase(name, "off"))
        return Result<ShadowMethod>::success(ShadowMethod::None);
    if (equalsIgnoreCase(name, "csm") || equalsIgnoreCase(name, "cascaded") ||
        equalsIgnoreCase(name, "directional"))
        return Result<ShadowMethod>::success(ShadowMethod::CascadedDirectional);
    if (equalsIgnoreCase(name, "perspective") || equalsIgnoreCase(name, "spot"))
        return Result<ShadowMethod>::success(ShadowMethod::PerspectiveSpot);
    if (equalsIgnoreCase(name, "cube") || equalsIgnoreCase(name, "cubemap") ||
        equalsIgnoreCase(name, "point"))
        return Result<ShadowMethod>::success(ShadowMethod::CubePoint);
    return Result<ShadowMethod>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, "Unknown shadow method", "graphics.shadow"));
}

const char* shadowMethodName(ShadowMethod method) {
    switch (method) {
    case ShadowMethod::Auto:
        return "auto";
    case ShadowMethod::None:
        return "none";
    case ShadowMethod::CascadedDirectional:
        return "csm";
    case ShadowMethod::PerspectiveSpot:
        return "perspective";
    case ShadowMethod::CubePoint:
        return "cube";
    }
    return "none";
}

Result<ShadowMethod> resolveShadowMethod(std::string_view lightType, ShadowMethod requested) {
    ShadowMethod method = requested;
    if (method == ShadowMethod::Auto) {
        if (lightType == "dir") method = ShadowMethod::CascadedDirectional;
        else if (lightType == "spot")
            method = ShadowMethod::PerspectiveSpot;
        else if (lightType == "point")
            method = ShadowMethod::CubePoint;
        else
            method = ShadowMethod::None;
    }
    if (method == ShadowMethod::None) return Result<ShadowMethod>::success(method);
    if (lightType == "dir" && method != ShadowMethod::CascadedDirectional) {
        return Result<ShadowMethod>::failure(Diagnostic::error(
            DiagnosticCode::Unsupported, "Directional lights only support csm shadows", "graphics.shadow"));
    }
    if (lightType == "spot" && method != ShadowMethod::PerspectiveSpot) {
        return Result<ShadowMethod>::failure(Diagnostic::error(
            DiagnosticCode::Unsupported, "Spot lights only support perspective shadows", "graphics.shadow"));
    }
    if (lightType == "point" && method != ShadowMethod::CubePoint) {
        return Result<ShadowMethod>::failure(Diagnostic::error(
            DiagnosticCode::Unsupported, "Point lights only support cube shadows", "graphics.shadow"));
    }
    return Result<ShadowMethod>::success(method);
}

glm::mat4 buildSpotShadowVP(const glm::vec3& position, const glm::vec3& direction, float range,
                            float outerAngleDeg) {
    const glm::vec3 forward = safeNormalize(direction, glm::vec3(0.f, -1.f, 0.f));
    glm::vec3 up(0.f, 1.f, 0.f);
    if (std::abs(glm::dot(forward, up)) > 0.95f) up = glm::vec3(0.f, 0.f, 1.f);
    const float farZ = std::max(range, 1e-2f);
    const float nearZ = std::max(farZ * 0.01f, 0.05f);
    const float fovY = std::clamp(outerAngleDeg, 0.1f, 89.f) * 2.f * 0.017453292519943295f;
    const glm::mat4 view = glm::lookAtRH(position, position + forward, up);
    const glm::mat4 proj = perspectiveVulkanRH_ZO(fovY, 1.f, nearZ, farZ);
    return proj * view;
}

Result<glm::mat4> buildPointShadowFaceVP(const glm::vec3& position, int faceIndex, float range) {
    if (faceIndex < 0 || faceIndex > 5) {
        return Result<glm::mat4>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                            "Point shadow face must be 0..5",
                                                            "graphics.shadow"));
    }
    static const glm::vec3 kForward[6] = {
        {1.f, 0.f, 0.f}, {-1.f, 0.f, 0.f}, {0.f, 1.f, 0.f},
        {0.f, -1.f, 0.f}, {0.f, 0.f, 1.f}, {0.f, 0.f, -1.f},
    };
    static const glm::vec3 kUp[6] = {
        {0.f, -1.f, 0.f}, {0.f, -1.f, 0.f}, {0.f, 0.f, 1.f},
        {0.f, 0.f, -1.f}, {0.f, -1.f, 0.f}, {0.f, -1.f, 0.f},
    };
    const float farZ = std::max(range, 1e-2f);
    const float nearZ = std::max(farZ * 0.01f, 0.05f);
    const glm::mat4 view = glm::lookAtRH(position, position + kForward[faceIndex], kUp[faceIndex]);
    const glm::mat4 proj = perspectiveVulkanRH_ZO(1.5707963267948966f, 1.f, nearZ, farZ);
    return Result<glm::mat4>::success(proj * view);
}

void selectShadowCasters(const std::vector<Light3D::Data*>& lights, const std::vector<bool>& isPointFlags,
                         const ShadowSchemeSettings& settings, Light3D::Data*& directionalCaster,
                         std::vector<LocalShadowSlot>& localSlots) {
    directionalCaster = nullptr;
    localSlots.clear();
    float bestDirI = -1.f;
    struct SpotCandidate {
        Light3D::Data* light = nullptr;
        size_t         index = 0;
        float          intensity = 0.f;
    };
    std::vector<SpotCandidate> spots;
    spots.reserve(lights.size());

    for (size_t i = 0; i < lights.size(); ++i) {
        Light3D::Data* d = lights[i];
        if (!d) continue;
        d->shadowLocalSlot = -1;
        if (!d->enabled || !d->castShadow || d->volumetricOnly) continue;
        const bool isPoint = i < isPointFlags.size() ? isPointFlags[i] : (d->type != "dir");
        auto resolved = resolveShadowMethod(d->type, static_cast<ShadowMethod>(d->shadowMethod));
        if (!resolved.ok()) continue;
        const ShadowMethod method = resolved.value();
        if (method == ShadowMethod::None) continue;

        if (!isPoint && method == ShadowMethod::CascadedDirectional) {
            if (!settings.enableDirectionalCsm) continue;
            if (d->intensity > bestDirI) {
                bestDirI = d->intensity;
                directionalCaster = d;
            }
            continue;
        }
        if (d->type == "spot" && method == ShadowMethod::PerspectiveSpot) {
            if (!settings.enableSpotPerspective) continue;
            spots.push_back({d, i, d->intensity});
        }
        // Point cube casters: selected only when the atlas path is enabled.
        if (d->type == "point" && method == ShadowMethod::CubePoint && settings.enablePointCube &&
            settings.maxPointShadowCasters > 0) {
            // Reserved for the cube-atlas pass; no slot allocation yet.
        }
    }

    std::stable_sort(spots.begin(), spots.end(),
                     [](const SpotCandidate& a, const SpotCandidate& b) { return a.intensity > b.intensity; });
    const int maxSpots = std::max(0, std::min(settings.maxSpotShadowCasters, ShadowConfig::kLocalSlots));
    for (int s = 0; s < maxSpots && s < int(spots.size()); ++s) {
        Light3D::Data* d = spots[size_t(s)].light;
        LocalShadowSlot slot;
        slot.light = d;
        slot.method = ShadowMethod::PerspectiveSpot;
        slot.layer = ShadowConfig::kCascades + s;
        slot.lightIndex = int(spots[size_t(s)].index);
        const glm::vec3 dir = safeNormalize(glm::vec3(d->dx, d->dy, d->dz), glm::vec3(0.f, -1.f, 0.f));
        slot.lightVP =
            buildSpotShadowVP(glm::vec3(d->x, d->y, d->z), dir, std::max(d->radius, 0.1f), d->spotAngleDeg);
        slot.bias = d->shadowBias > 0.f ? d->shadowBias : settings.defaultSpotBias;
        slot.strength = d->shadowStrength;
        d->shadowLocalSlot = s;
        localSlots.push_back(slot);
    }
}

}  // namespace eve::graphics
