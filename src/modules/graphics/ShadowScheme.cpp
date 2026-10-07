#include "graphics/ShadowScheme.h"

#include "common/Diagnostic.h"
#include "graphics/ClipSpace.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace eve::graphics {
namespace {

ShadowSchemeSettings g_shadowSchemeSettings{};

struct LocalPageCacheEntry {
    Light3D::Data* light = nullptr;
    glm::mat4      lightVP{1.f};
    float          bias = 0.002f;
    float          x = 0.f, y = 0.f, z = 0.f;
    float          dx = 0.f, dy = 0.f, dz = 0.f;
    float          radius = 0.f;
    float          spotAngleDeg = 0.f;
    float          shadowStrength = 1.f;
    bool           occupied = false;
};

struct LocalPageCache {
    LocalPageCacheEntry slots[ShadowConfig::kLocalSlots]{};
};

LocalPageCache g_localPageCache{};

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

bool lightParamsMatchCache(const Light3D::Data& d, const LocalPageCacheEntry& e, float resolvedBias) {
    constexpr float kEps = 1e-4f;
    return e.occupied && e.light == &d && std::abs(e.x - d.x) < kEps && std::abs(e.y - d.y) < kEps &&
           std::abs(e.z - d.z) < kEps && std::abs(e.dx - d.dx) < kEps && std::abs(e.dy - d.dy) < kEps &&
           std::abs(e.dz - d.dz) < kEps && std::abs(e.radius - d.radius) < kEps &&
           std::abs(e.spotAngleDeg - d.spotAngleDeg) < kEps &&
           std::abs(e.shadowStrength - d.shadowStrength) < kEps && std::abs(e.bias - resolvedBias) < kEps;
}

void storeCacheEntry(LocalPageCacheEntry& e, Light3D::Data* d, const glm::mat4& vp, float bias) {
    e = LocalPageCacheEntry{};
    e.occupied = d != nullptr;
    e.light    = d;
    e.lightVP  = vp;
    e.bias     = bias;
    if (!d) return;
    e.x              = d->x;
    e.y              = d->y;
    e.z              = d->z;
    e.dx             = d->dx;
    e.dy             = d->dy;
    e.dz             = d->dz;
    e.radius         = d->radius;
    e.spotAngleDeg   = d->spotAngleDeg;
    e.shadowStrength = d->shadowStrength;
}

int findCachedSlot(Light3D::Data* light) {
    if (!light) return -1;
    for (int s = 0; s < ShadowConfig::kLocalSlots; ++s) {
        if (g_localPageCache.slots[s].occupied && g_localPageCache.slots[s].light == light) return s;
    }
    return -1;
}

}  // namespace

ShadowSchemeSettings& ShadowSchemeSettings::current() { return g_shadowSchemeSettings; }

void resetShadowLocalPageCache() {
    for (auto& slot : g_localPageCache.slots) slot = LocalPageCacheEntry{};
}

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

float scoreSpotShadowCandidate(const Light3D::Data& light, const ShadowPagingView& view,
                               const ShadowSchemeSettings& settings, bool hadSlot) {
    float score = std::max(light.intensity, 0.f);
    if (settings.enableLocalPaging && view.valid) {
        const glm::vec3 pos(light.x, light.y, light.z);
        const float dist = std::max(glm::length(pos - view.eye), 0.1f);
        const float range = std::max(light.radius, 0.1f);
        const float atten = std::clamp(1.f - dist / (range * 2.f + dist), 0.f, 1.f);
        const float coverage = std::clamp(range / dist, 0.f, 4.f);
        const glm::vec4 clip = view.viewProj * glm::vec4(pos, 1.f);
        float frustum = 0.02f;
        if (clip.w > 1e-5f) {
            const float invW = 1.f / clip.w;
            const float ndcX = clip.x * invW;
            const float ndcY = clip.y * invW;
            const float ndcZ = clip.z * invW;
            const float pad = coverage * 0.5f;
            if (std::abs(ndcX) < 1.f + pad && std::abs(ndcY) < 1.f + pad && ndcZ > -pad &&
                ndcZ < 1.f + pad) {
                const float edge = std::max(std::abs(ndcX), std::abs(ndcY));
                frustum = std::clamp(1.f - 0.5f * edge, 0.15f, 1.f);
            } else {
                frustum = 0.05f;
            }
        }
        score = light.intensity * atten * (0.35f + 0.65f * std::min(coverage, 1.5f) / 1.5f) *
                (0.25f + 0.75f * frustum);
    }
    if (hadSlot) score *= (1.f + std::max(settings.hysteresisBonus, 0.f));
    return score;
}

void selectShadowCasters(const std::vector<Light3D::Data*>& lights, const std::vector<bool>& isPointFlags,
                         const ShadowSchemeSettings& settings, const ShadowPagingView& view,
                         Light3D::Data*& directionalCaster, std::vector<LocalShadowSlot>& localSlots) {
    directionalCaster = nullptr;
    localSlots.clear();
    float bestDirI = -1.f;

    struct SpotCandidate {
        Light3D::Data* light = nullptr;
        size_t         index = 0;
        float          score = 0.f;
        int            prevSlot = -1;
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
            const int prev = findCachedSlot(d);
            const float score = scoreSpotShadowCandidate(*d, view, settings, prev >= 0);
            if (settings.enableLocalPaging && view.valid && score < settings.minPageScore) continue;
            spots.push_back({d, i, score, prev});
        }
        if (d->type == "point" && method == ShadowMethod::CubePoint && settings.enablePointCube &&
            settings.maxPointShadowCasters > 0) {
            // Reserved for the cube-atlas pass.
        }
    }

    std::stable_sort(spots.begin(), spots.end(),
                     [](const SpotCandidate& a, const SpotCandidate& b) { return a.score > b.score; });
    const int maxSpots = std::max(0, std::min(settings.maxSpotShadowCasters, ShadowConfig::kLocalSlots));
    if (maxSpots == 0 || spots.empty()) {
        resetShadowLocalPageCache();
        return;
    }

    const size_t winnerCount = std::min(size_t(maxSpots), spots.size());
    std::vector<bool> winnerTaken(winnerCount, false);

    Light3D::Data* assigned[ShadowConfig::kLocalSlots]{};
    float          assignedScore[ShadowConfig::kLocalSlots]{};
    size_t         assignedIndex[ShadowConfig::kLocalSlots]{};

    // Sticky: keep previous physical slot when that light is still a winner.
    for (size_t w = 0; w < winnerCount; ++w) {
        const int prev = spots[w].prevSlot;
        if (prev < 0 || prev >= maxSpots) continue;
        if (assigned[prev]) continue;
        assigned[prev] = spots[w].light;
        assignedScore[prev] = spots[w].score;
        assignedIndex[prev] = spots[w].index;
        winnerTaken[w] = true;
    }

    for (size_t w = 0; w < winnerCount; ++w) {
        if (winnerTaken[w]) continue;
        int slot = -1;
        for (int s = 0; s < maxSpots; ++s) {
            if (!assigned[s]) {
                slot = s;
                break;
            }
        }
        if (slot < 0) {
            int victim = -1;
            float worst = 1e30f;
            for (int s = 0; s < maxSpots; ++s) {
                if (assignedScore[s] < worst) {
                    worst = assignedScore[s];
                    victim = s;
                }
            }
            if (victim < 0 || assignedScore[victim] >= spots[w].score) continue;
            slot = victim;
        }
        assigned[slot] = spots[w].light;
        assignedScore[slot] = spots[w].score;
        assignedIndex[slot] = spots[w].index;
        winnerTaken[w] = true;
    }

    struct UpdateCandidate {
        int   slot = -1;
        int   priority = 0;
        float score = 0.f;
    };
    std::vector<UpdateCandidate> updates;
    updates.reserve(size_t(maxSpots));
    LocalPageCacheEntry nextCache[ShadowConfig::kLocalSlots]{};
    // Map physical slot → localSlots index for later update-budget edits.
    int slotToLocalIndex[ShadowConfig::kLocalSlots];
    for (int& v : slotToLocalIndex) v = -1;

    for (int s = 0; s < maxSpots; ++s) {
        Light3D::Data* d = assigned[s];
        if (!d) continue;

        const float bias = d->shadowBias > 0.f ? d->shadowBias : settings.defaultSpotBias;
        const glm::vec3 dir = safeNormalize(glm::vec3(d->dx, d->dy, d->dz), glm::vec3(0.f, -1.f, 0.f));
        const glm::mat4 vp =
            buildSpotShadowVP(glm::vec3(d->x, d->y, d->z), dir, std::max(d->radius, 0.1f), d->spotAngleDeg);

        LocalShadowSlot slot;
        slot.light = d;
        slot.method = ShadowMethod::PerspectiveSpot;
        slot.layer = ShadowConfig::kCascades + s;
        slot.lightIndex = int(assignedIndex[s]);
        slot.lightVP = vp;
        slot.bias = bias;
        slot.strength = d->shadowStrength;
        slot.score = assignedScore[s];
        slot.needsUpdate = true;

        const LocalPageCacheEntry& prev = g_localPageCache.slots[s];
        const bool sameLight = prev.occupied && prev.light == d;
        const bool paramsSame = sameLight && lightParamsMatchCache(*d, prev, bias);
        if (paramsSame) {
            slot.needsUpdate = false;
            slot.lightVP = prev.lightVP;
            slot.bias = prev.bias;
        } else {
            int priority = sameLight ? 2 : 3;
            updates.push_back({s, priority, slot.score});
        }

        d->shadowLocalSlot = s;
        storeCacheEntry(nextCache[s], d, slot.lightVP, slot.bias);
        slotToLocalIndex[s] = int(localSlots.size());
        localSlots.push_back(slot);
    }

    const int updateBudget =
        std::max(0, std::min(settings.maxLocalUpdatesPerFrame, ShadowConfig::kLocalSlots));
    std::stable_sort(updates.begin(), updates.end(), [](const UpdateCandidate& a, const UpdateCandidate& b) {
        if (a.priority != b.priority) return a.priority > b.priority;
        return a.score > b.score;
    });
    for (size_t i = size_t(updateBudget); i < updates.size(); ++i) {
        const int phys = updates[i].slot;
        const int li = slotToLocalIndex[phys];
        if (li < 0) continue;
        LocalShadowSlot& slot = localSlots[size_t(li)];
        slot.needsUpdate = false;
        const LocalPageCacheEntry& prev = g_localPageCache.slots[phys];
        if (prev.occupied && prev.light == slot.light) {
            slot.lightVP = prev.lightVP;
            slot.bias = prev.bias;
            nextCache[phys].lightVP = prev.lightVP;
            nextCache[phys].bias = prev.bias;
        }
    }

    for (int s = 0; s < ShadowConfig::kLocalSlots; ++s) g_localPageCache.slots[s] = nextCache[s];
}

}  // namespace eve::graphics
