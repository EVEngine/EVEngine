#include "graphics/fog/FogInteractor.h"

#include "common/Diagnostic.h"
#include "graphics/fog/MacFluidGrid.h"

#include <algorithm>
#include <cmath>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

namespace eve::graphics::fog {
namespace {

[[nodiscard]] bool finiteVec(const glm::vec3& v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

[[nodiscard]] glm::vec3 safeNormalize(const glm::vec3& v, const glm::vec3& fallback) noexcept {
    const float len2 = glm::dot(v, v);
    if (len2 < 1e-12f) return fallback;
    return v * (1.f / std::sqrt(len2));
}

}  // namespace

Result<void> FogInteractor::setProxies(std::vector<FogSolidProxy> proxies) {
    for (std::size_t i = 0; i < proxies.size(); ++i) {
        const auto& p = proxies[i];
        if (!finiteVec(p.position) || !finiteVec(p.velocity) || !finiteVec(p.extents) ||
            !finiteVec(p.axis) || !finiteVec(p.axisX) || !finiteVec(p.axisY) || !finiteVec(p.axisZ)) {
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "proxy vectors must be finite", "proxies", {},
                "graphics.fog"));
        }
        if (p.extents.x <= 0.f || p.extents.y <= 0.f || p.extents.z <= 0.f) {
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "proxy extents must be positive", "extents", {},
                "graphics.fog"));
        }
        if (!std::isfinite(p.drag) || p.drag < 0.f || p.drag > 1.f || !std::isfinite(p.wakeStrength) ||
            p.wakeStrength < 0.f) {
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "drag must be in [0,1] and wakeStrength >= 0",
                "drag", {}, "graphics.fog"));
        }
    }
    proxies_ = std::move(proxies);
    return Result<void>::success();
}

const FogSolidProxy& FogInteractor::proxyAt(std::size_t index) const {
    return proxies_.at(index);
}

float FogInteractor::signedDistance(const FogSolidProxy& proxy, const glm::vec3& world) const noexcept {
    switch (proxy.shape) {
        case FogProxyShape::Sphere: {
            return glm::length(world - proxy.position) - proxy.extents.x;
        }
        case FogProxyShape::Capsule: {
            const glm::vec3 a = proxy.position - proxy.axis;
            const glm::vec3 b = proxy.position + proxy.axis;
            const glm::vec3 pa = world - a;
            const glm::vec3 ba = b - a;
            const float h = std::clamp(glm::dot(pa, ba) / std::max(glm::dot(ba, ba), 1e-8f), 0.f, 1.f);
            return glm::length(pa - ba * h) - proxy.extents.x;
        }
        case FogProxyShape::Obb: {
            const glm::vec3 d = world - proxy.position;
            const glm::vec3 local(glm::dot(d, proxy.axisX), glm::dot(d, proxy.axisY),
                                 glm::dot(d, proxy.axisZ));
            const glm::vec3 q = glm::abs(local) - proxy.extents;
            return glm::length(glm::max(q, glm::vec3(0.f))) +
                   std::min(std::max(q.x, std::max(q.y, q.z)), 0.f);
        }
    }
    return 1.f;
}

glm::vec3 FogInteractor::closestPoint(const FogSolidProxy& proxy, const glm::vec3& world) const noexcept {
    const float sd = signedDistance(proxy, world);
    if (sd >= 0.f) {
        const glm::vec3 n = surfaceNormal(proxy, world);
        return world - n * sd;
    }
    const glm::vec3 n = surfaceNormal(proxy, world);
    return world - n * sd;
}

glm::vec3 FogInteractor::surfaceNormal(const FogSolidProxy& proxy, const glm::vec3& world) const noexcept {
    constexpr float e = 1e-3f;
    const float dx = signedDistance(proxy, world + glm::vec3(e, 0.f, 0.f)) -
                     signedDistance(proxy, world - glm::vec3(e, 0.f, 0.f));
    const float dy = signedDistance(proxy, world + glm::vec3(0.f, e, 0.f)) -
                     signedDistance(proxy, world - glm::vec3(0.f, e, 0.f));
    const float dz = signedDistance(proxy, world + glm::vec3(0.f, 0.f, e)) -
                     signedDistance(proxy, world - glm::vec3(0.f, 0.f, e));
    return safeNormalize(glm::vec3(dx, dy, dz), glm::vec3(0.f, 1.f, 0.f));
}

bool FogInteractor::isSolidWorld(const glm::vec3& world) const noexcept {
    for (const auto& p : proxies_) {
        if (!p.enabled) continue;
        if (signedDistance(p, world) <= 0.f) return true;
    }
    return false;
}

Result<void> FogInteractor::applyToGrid(MacFluidGrid& grid, float dt) const {
    if (!std::isfinite(dt) || dt < 0.f) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "dt must be finite and >= 0", "dt", {}, "graphics.fog"));
    }
    if (grid.width() <= 0) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed, "MAC grid is empty",
                                                       "grid", {}, "graphics.fog"));
    }

    const FogWorldBounds& bounds = grid.bounds_;
    const glm::vec3 cell = grid.cellSize_;

    std::fill(grid.solid_.begin(), grid.solid_.end(), 0);

    for (int z = 0; z < grid.depth_; ++z) {
        for (int y = 0; y < grid.height_; ++y) {
            for (int x = 0; x < grid.width_; ++x) {
                const glm::vec3 world =
                    bounds.minimum + glm::vec3((x + 0.5f) * cell.x, (y + 0.5f) * cell.y,
                                               (z + 0.5f) * cell.z);
                if (!isSolidWorld(world)) continue;
                grid.solid_[grid.cellIndex(x, y, z)] = 1;
                grid.setDensity(x, y, z, 0.f);

                for (const auto& proxy : proxies_) {
                    if (!proxy.enabled) continue;
                    if (signedDistance(proxy, world) > cell.x) continue;
                    const glm::vec3 n = surfaceNormal(proxy, world);
                    const float vn = glm::dot(proxy.velocity, n);
                    // Windward push, leeward suction, tangential drag.
                    const glm::vec3 tangential = proxy.velocity - n * vn;
                    const glm::vec3 wake =
                        n * (vn * proxy.wakeStrength) + tangential * (1.f - proxy.drag);
                    // Stamp onto adjacent faces (wake pair).
                    if (x + 1 < grid.width_)
                        grid.u_[grid.uIndex(x + 1, y, z)] += wake.x * dt;
                    if (x > 0) grid.u_[grid.uIndex(x, y, z)] += wake.x * dt;
                    if (y + 1 < grid.height_)
                        grid.v_[grid.vIndex(x, y + 1, z)] += wake.y * dt;
                    if (y > 0) grid.v_[grid.vIndex(x, y, z)] += wake.y * dt;
                    if (z + 1 < grid.depth_)
                        grid.w_[grid.wIndex(x, y, z + 1)] += wake.z * dt;
                    if (z > 0) grid.w_[grid.wIndex(x, y, z)] += wake.z * dt;
                }
            }
        }
    }

    // Zero velocity into solid faces.
    for (int z = 0; z < grid.depth_; ++z) {
        for (int y = 0; y < grid.height_; ++y) {
            for (int i = 0; i <= grid.width_; ++i) {
                const bool leftSolid = i > 0 && grid.solid_[grid.cellIndex(i - 1, y, z)];
                const bool rightSolid = i < grid.width_ && grid.solid_[grid.cellIndex(i, y, z)];
                if (leftSolid || rightSolid) grid.u_[grid.uIndex(i, y, z)] = 0.f;
            }
        }
    }
    for (int z = 0; z < grid.depth_; ++z) {
        for (int j = 0; j <= grid.height_; ++j) {
            for (int x = 0; x < grid.width_; ++x) {
                const bool downSolid = j > 0 && grid.solid_[grid.cellIndex(x, j - 1, z)];
                const bool upSolid = j < grid.height_ && grid.solid_[grid.cellIndex(x, j, z)];
                if (downSolid || upSolid) grid.v_[grid.vIndex(x, j, z)] = 0.f;
            }
        }
    }
    for (int k = 0; k <= grid.depth_; ++k) {
        for (int y = 0; y < grid.height_; ++y) {
            for (int x = 0; x < grid.width_; ++x) {
                const bool backSolid = k > 0 && grid.solid_[grid.cellIndex(x, y, k - 1)];
                const bool frontSolid = k < grid.depth_ && grid.solid_[grid.cellIndex(x, y, k)];
                if (backSolid || frontSolid) grid.w_[grid.wIndex(x, y, k)] = 0.f;
            }
        }
    }
    return Result<void>::success();
}

glm::vec3 FogInteractor::clipAdvection(const glm::vec3& startLocal, const glm::vec3& deltaLocal,
                                       const FogWorldBounds& bounds,
                                       const glm::vec3& cellSize) const noexcept {
    if (proxies_.empty()) return startLocal + deltaLocal;

    const float len = glm::length(deltaLocal);
    if (len < 1e-8f) return startLocal;

    const int steps = std::max(1, static_cast<int>(std::ceil(len * 2.f)));
    const glm::vec3 step = deltaLocal / static_cast<float>(steps);
    glm::vec3 p = startLocal;
    for (int i = 0; i < steps; ++i) {
        const glm::vec3 next = p + step;
        const glm::vec3 world =
            bounds.minimum + glm::vec3((next.x + 0.5f) * cellSize.x, (next.y + 0.5f) * cellSize.y,
                                      (next.z + 0.5f) * cellSize.z);
        if (isSolidWorld(world)) return p;
        p = next;
    }
    return p;
}

}  // namespace eve::graphics::fog
