#include "graphics/fog/FogDensityField.h"
#include <algorithm>
#include <cmath>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include "common/Diagnostic.h"
#include "graphics/AtmosphereVolume.h"
#include "graphics/fog/FogFroxelBridge.h"
#include "graphics/fog/FogInteractor.h"
#include "graphics/fog/FogProfile.h"
#include "graphics/fog/MacFluidGrid.h"

// FogDensityField.cpp
namespace eve::graphics::fog {
namespace {

[[nodiscard]] float hash31(int x, int y, int z, std::uint32_t seed) noexcept {
    std::uint32_t n = static_cast<std::uint32_t>(x) * 374761393u + static_cast<std::uint32_t>(y) * 668265263u +
                      static_cast<std::uint32_t>(z) * 2147483647u + seed * 1013904223u;
    n = (n ^ (n >> 13)) * 1274126177u;
    n ^= n >> 16;
    return static_cast<float>(n & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

}  // namespace

Result<void> FogDensityField::resize(int width, int height, int depth, const FogWorldBounds& bounds) {
    if (width < 2 || height < 2 || depth < 2 || width > 256 || height > 256 || depth > 256) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "density field dimensions must be in [2,256]", "size", {},
                                                       "graphics.fog"));
    }
    const glm::vec3 size = bounds.maximum - bounds.minimum;
    if (!(size.x > 0.f && size.y > 0.f && size.z > 0.f) || !std::isfinite(size.x) || !std::isfinite(size.y) ||
        !std::isfinite(size.z)) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "world bounds must be finite with positive extent", "bounds", {},
                                                       "graphics.fog"));
    }

    width_    = width;
    height_   = height;
    depth_    = depth;
    bounds_   = bounds;
    cellSize_ = glm::vec3(size.x / static_cast<float>(width), size.y / static_cast<float>(height),
                          size.z / static_cast<float>(depth));
    const std::size_t count =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * static_cast<std::size_t>(depth);
    density_.assign(count, 0.f);
    curl_.assign(count, glm::vec3(0.f));
    lightAssist_.assign(count, glm::vec3(0.f));
    ++revision_;
    return Result<void>::success();
}

void FogDensityField::clear() {
    std::fill(density_.begin(), density_.end(), 0.f);
    std::fill(curl_.begin(), curl_.end(), glm::vec3(0.f));
    std::fill(lightAssist_.begin(), lightAssist_.end(), glm::vec3(0.f));
    ++revision_;
}

std::size_t FogDensityField::index(int x, int y, int z) const noexcept {
    x = std::clamp(x, 0, std::max(width_ - 1, 0));
    y = std::clamp(y, 0, std::max(height_ - 1, 0));
    z = std::clamp(z, 0, std::max(depth_ - 1, 0));
    return (static_cast<std::size_t>(z) * static_cast<std::size_t>(height_) + static_cast<std::size_t>(y)) *
               static_cast<std::size_t>(width_) +
           static_cast<std::size_t>(x);
}

glm::vec3 FogDensityField::cellCenter(int x, int y, int z) const noexcept {
    return bounds_.minimum + glm::vec3((static_cast<float>(x) + 0.5f) * cellSize_.x,
                                       (static_cast<float>(y) + 0.5f) * cellSize_.y,
                                       (static_cast<float>(z) + 0.5f) * cellSize_.z);
}

void FogDensityField::setDensity(int x, int y, int z, float density) {
    if (density_.empty()) return;
    density_[index(x, y, z)] = std::max(density, 0.f);
    ++revision_;
}

float FogDensityField::densityAt(int x, int y, int z) const noexcept {
    if (density_.empty()) return 0.f;
    return density_[index(x, y, z)];
}

void FogDensityField::setCurlVelocity(int x, int y, int z, const glm::vec3& velocity) {
    if (curl_.empty()) return;
    curl_[index(x, y, z)] = velocity;
    ++revision_;
}

glm::vec3 FogDensityField::curlVelocityAt(int x, int y, int z) const noexcept {
    if (curl_.empty()) return {};
    return curl_[index(x, y, z)];
}

void FogDensityField::setLightAssist(int x, int y, int z, const glm::vec3& assist) {
    if (lightAssist_.empty()) return;
    lightAssist_[index(x, y, z)] = assist;
    ++revision_;
}

glm::vec3 FogDensityField::lightAssistAt(int x, int y, int z) const noexcept {
    if (lightAssist_.empty()) return {};
    return lightAssist_[index(x, y, z)];
}

void FogDensityField::sampleLattice(const glm::vec3& world, int& x0, int& y0, int& z0, int& x1, int& y1, int& z1,
                                    float& fx, float& fy, float& fz) const noexcept {
    const glm::vec3 local = (world - bounds_.minimum) / cellSize_ - glm::vec3(0.5f);
    const float     lx    = std::clamp(local.x, 0.f, static_cast<float>(std::max(width_ - 1, 0)));
    const float     ly    = std::clamp(local.y, 0.f, static_cast<float>(std::max(height_ - 1, 0)));
    const float     lz    = std::clamp(local.z, 0.f, static_cast<float>(std::max(depth_ - 1, 0)));
    x0                    = static_cast<int>(std::floor(lx));
    y0                    = static_cast<int>(std::floor(ly));
    z0                    = static_cast<int>(std::floor(lz));
    x1                    = std::min(x0 + 1, width_ - 1);
    y1                    = std::min(y0 + 1, height_ - 1);
    z1                    = std::min(z0 + 1, depth_ - 1);
    fx                    = lx - static_cast<float>(x0);
    fy                    = ly - static_cast<float>(y0);
    fz                    = lz - static_cast<float>(z0);
}

float FogDensityField::sampleDensity(const glm::vec3& world) const noexcept {
    if (density_.empty() || !bounds_.contains(world)) {
        // Soft falloff just outside bounds keeps ray edges continuous.
        if (density_.empty()) return 0.f;
        const glm::vec3 c = glm::clamp(world, bounds_.minimum, bounds_.maximum);
        if (glm::length(world - c) > cellSize_.x) return 0.f;
    }
    int   x0, y0, z0, x1, y1, z1;
    float fx, fy, fz;
    sampleLattice(world, x0, y0, z0, x1, y1, z1, fx, fy, fz);
    const auto  lerp = [](float a, float b, float t) { return a + (b - a) * t; };
    const float c00  = lerp(densityAt(x0, y0, z0), densityAt(x1, y0, z0), fx);
    const float c10  = lerp(densityAt(x0, y1, z0), densityAt(x1, y1, z0), fx);
    const float c01  = lerp(densityAt(x0, y0, z1), densityAt(x1, y0, z1), fx);
    const float c11  = lerp(densityAt(x0, y1, z1), densityAt(x1, y1, z1), fx);
    return lerp(lerp(c00, c10, fy), lerp(c01, c11, fy), fz);
}

glm::vec3 FogDensityField::sampleCurlVelocity(const glm::vec3& world) const noexcept {
    if (curl_.empty()) return {};
    int   x0, y0, z0, x1, y1, z1;
    float fx, fy, fz;
    sampleLattice(world, x0, y0, z0, x1, y1, z1, fx, fy, fz);
    const auto      lerp3 = [](const glm::vec3& a, const glm::vec3& b, float t) { return a + (b - a) * t; };
    const glm::vec3 c00   = lerp3(curlVelocityAt(x0, y0, z0), curlVelocityAt(x1, y0, z0), fx);
    const glm::vec3 c10   = lerp3(curlVelocityAt(x0, y1, z0), curlVelocityAt(x1, y1, z0), fx);
    const glm::vec3 c01   = lerp3(curlVelocityAt(x0, y0, z1), curlVelocityAt(x1, y0, z1), fx);
    const glm::vec3 c11   = lerp3(curlVelocityAt(x0, y1, z1), curlVelocityAt(x1, y1, z1), fx);
    return lerp3(lerp3(c00, c10, fy), lerp3(c01, c11, fy), fz);
}

glm::vec3 FogDensityField::sampleLightAssist(const glm::vec3& world) const noexcept {
    if (lightAssist_.empty()) return {};
    int   x0, y0, z0, x1, y1, z1;
    float fx, fy, fz;
    sampleLattice(world, x0, y0, z0, x1, y1, z1, fx, fy, fz);
    const auto      lerp3 = [](const glm::vec3& a, const glm::vec3& b, float t) { return a + (b - a) * t; };
    const glm::vec3 c00   = lerp3(lightAssistAt(x0, y0, z0), lightAssistAt(x1, y0, z0), fx);
    const glm::vec3 c10   = lerp3(lightAssistAt(x0, y1, z0), lightAssistAt(x1, y1, z0), fx);
    const glm::vec3 c01   = lerp3(lightAssistAt(x0, y0, z1), lightAssistAt(x1, y0, z1), fx);
    const glm::vec3 c11   = lerp3(lightAssistAt(x0, y1, z1), lightAssistAt(x1, y1, z1), fx);
    return lerp3(lerp3(c00, c10, fy), lerp3(c01, c11, fy), fz);
}

Result<void> FogDensityField::seedHeightBand(float baseDensity, float baseHeight, float falloff, float curlScale,
                                             std::uint32_t seed) {
    if (density_.empty()) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "density field is not allocated", "resize", {}, "graphics.fog"));
    }
    if (!std::isfinite(baseDensity) || baseDensity < 0.f || !std::isfinite(falloff) || falloff < 0.f ||
        !std::isfinite(curlScale)) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "height-band parameters must be finite and non-negative",
                                                       "seedHeightBand", {}, "graphics.fog"));
    }

    for (int z = 0; z < depth_; ++z) {
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                const glm::vec3 p = cellCenter(x, y, z);
                const float     h = std::max(p.y - baseHeight, 0.f);
                float           d = baseDensity * std::exp(-falloff * h);
                const float     n = 0.65f + 0.35f * hash31(x, y, z, seed);
                d *= n;
                setDensity(x, y, z, d);

                // Analytic curl-noise style assist: divergence-free on average.
                const float     a = hash31(x + 17, y, z, seed) * 6.2831853f;
                const float     b = hash31(x, y + 31, z, seed) * 6.2831853f;
                const glm::vec3 curl(curlScale * std::sin(a) * std::cos(b), curlScale * 0.35f * std::sin(a + b),
                                     curlScale * std::cos(a) * std::sin(b));
                setCurlVelocity(x, y, z, curl);
            }
        }
    }
    // setDensity already bumps revision many times; collapse to one bump.
    ++revision_;
    return Result<void>::success();
}

bool FogDensityField::isOccupied(int x, int y, int z, float threshold) const noexcept {
    return densityAt(x, y, z) > threshold;
}

std::span<float>       FogDensityField::densitySpan() noexcept { return density_; }
std::span<const float> FogDensityField::densitySpan() const noexcept { return density_; }

}  // namespace eve::graphics::fog

// FogFroxelBridge.cpp
namespace eve::graphics::fog {
namespace {

/** Reconstruct a froxel center the same way AtmosphereVolume injects height fog. */
[[nodiscard]] glm::vec3 reconstructFroxelCenter(const AtmosphereVolume& volume, int x, int y, int z,
                                                const glm::mat4& invViewProj) {
    const int   width     = std::max(volume.getWidth(), 1);
    const int   height    = std::max(volume.getHeight(), 1);
    const float ndcX      = ((static_cast<float>(x) + 0.5f) / static_cast<float>(width)) * 2.f - 1.f;
    const float ndcY      = ((static_cast<float>(y) + 0.5f) / static_cast<float>(height)) * 2.f - 1.f;
    auto        unproject = [&](float ndcZ) {
        const glm::vec4 homogeneous = invViewProj * glm::vec4(ndcX, ndcY, ndcZ, 1.f);
        return glm::vec3(homogeneous) / homogeneous.w;
    };
    const glm::vec3 nearPoint = unproject(0.f);
    const glm::vec3 farPoint  = unproject(1.f);
    const float     span      = std::max(volume.getFarDistance() - volume.getNearDistance(), 1e-6f);
    const float     depth01   = (volume.sliceDistance(z) - volume.getNearDistance()) / span;
    return glm::mix(nearPoint, farPoint, depth01);
}

}  // namespace

Result<int> FogFroxelBridge::inject(AtmosphereVolume& volume, const FogDensityField& field, const FogProfile& profile,
                                    const glm::mat4& invViewProj, bool occupancySkip) {
    if (volume.getWidth() <= 0 || field.width() <= 0) {
        return Result<int>::failure(Diagnostic::error(DiagnosticCode::Failed, "froxel/density volumes are empty",
                                                      "inject", {}, "graphics.fog"));
    }

    int written = 0;
    for (int z = 0; z < volume.getDepth(); ++z) {
        for (int y = 0; y < volume.getHeight(); ++y) {
            for (int x = 0; x < volume.getWidth(); ++x) {
                const glm::vec3 world   = reconstructFroxelCenter(volume, x, y, z, invViewProj);
                const float     density = field.sampleDensity(world);
                if (occupancySkip && density < 1e-4f) continue;
                auto&       froxel = volume.at(x, y, z);
                const float sigmaT = profile.extinctionAt(density);
                froxel.extinction += sigmaT;
                froxel.scattering += profile.scatteringAt(density);
                froxel.anisotropy = profile.anisotropy();
                froxel.emissive += field.sampleLightAssist(world);
                ++written;
            }
        }
    }
    return Result<int>::success(written);
}

Result<void> FogFroxelBridge::integrate(AtmosphereVolume& volume, const FogDensityField& field,
                                        const FogProfile& profile, const glm::mat4& invViewProj,
                                        const glm::vec3& lightDir, const glm::vec3& lightColor, float intensity,
                                        const BeerLightCache* beerCache) {
    if (volume.getWidth() <= 0) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "froxel volume empty", "integrate", {}, "graphics.fog"));
    }
    const float     len        = glm::length(lightDir);
    const glm::vec3 dir        = len > 1e-6f ? lightDir / len : glm::vec3(0.f, 1.f, 0.f);
    const float     phaseScale = profile.phase(glm::dot(dir, glm::vec3(0.f, 0.f, -1.f)));

    if (beerCache && beerCache->valid()) {
        for (int z = 0; z < volume.getDepth(); ++z) {
            for (int y = 0; y < volume.getHeight(); ++y) {
                for (int x = 0; x < volume.getWidth(); ++x) {
                    const glm::vec3 world = reconstructFroxelCenter(volume, x, y, z, invViewProj);
                    volume.setLightVisibility(x, y, z, beerCache->sampleTransmittance(field, world));
                }
            }
        }
    }

    volume.integrate(lightColor * intensity, phaseScale);
    return Result<void>::success();
}

}  // namespace eve::graphics::fog

// FogInteractor.cpp
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
        if (!finiteVec(p.position) || !finiteVec(p.velocity) || !finiteVec(p.extents) || !finiteVec(p.axis) ||
            !finiteVec(p.axisX) || !finiteVec(p.axisY) || !finiteVec(p.axisZ)) {
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "proxy vectors must be finite", "proxies", {}, "graphics.fog"));
        }
        if (p.extents.x <= 0.f || p.extents.y <= 0.f || p.extents.z <= 0.f) {
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "proxy extents must be positive", "extents", {}, "graphics.fog"));
        }
        if (!std::isfinite(p.drag) || p.drag < 0.f || p.drag > 1.f || !std::isfinite(p.wakeStrength) ||
            p.wakeStrength < 0.f) {
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           "drag must be in [0,1] and wakeStrength >= 0", "drag", {},
                                                           "graphics.fog"));
        }
    }
    proxies_ = std::move(proxies);
    return Result<void>::success();
}

const FogSolidProxy& FogInteractor::proxyAt(std::size_t index) const { return proxies_.at(index); }

float FogInteractor::signedDistance(const FogSolidProxy& proxy, const glm::vec3& world) const noexcept {
    switch (proxy.shape) {
        case FogProxyShape::Sphere: {
            return glm::length(world - proxy.position) - proxy.extents.x;
        }
        case FogProxyShape::Capsule: {
            const glm::vec3 a  = proxy.position - proxy.axis;
            const glm::vec3 b  = proxy.position + proxy.axis;
            const glm::vec3 pa = world - a;
            const glm::vec3 ba = b - a;
            const float     h  = std::clamp(glm::dot(pa, ba) / std::max(glm::dot(ba, ba), 1e-8f), 0.f, 1.f);
            return glm::length(pa - ba * h) - proxy.extents.x;
        }
        case FogProxyShape::Obb: {
            const glm::vec3 d = world - proxy.position;
            const glm::vec3 local(glm::dot(d, proxy.axisX), glm::dot(d, proxy.axisY), glm::dot(d, proxy.axisZ));
            const glm::vec3 q = glm::abs(local) - proxy.extents;
            return glm::length(glm::max(q, glm::vec3(0.f))) + std::min(std::max(q.x, std::max(q.y, q.z)), 0.f);
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
    const float     dx =
        signedDistance(proxy, world + glm::vec3(e, 0.f, 0.f)) - signedDistance(proxy, world - glm::vec3(e, 0.f, 0.f));
    const float dy =
        signedDistance(proxy, world + glm::vec3(0.f, e, 0.f)) - signedDistance(proxy, world - glm::vec3(0.f, e, 0.f));
    const float dz =
        signedDistance(proxy, world + glm::vec3(0.f, 0.f, e)) - signedDistance(proxy, world - glm::vec3(0.f, 0.f, e));
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
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "dt must be finite and >= 0", "dt", {}, "graphics.fog"));
    }
    if (grid.width() <= 0) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "MAC grid is empty", "grid", {}, "graphics.fog"));
    }

    const FogWorldBounds& bounds = grid.bounds_;
    const glm::vec3       cell   = grid.cellSize_;

    std::fill(grid.solid_.begin(), grid.solid_.end(), 0);

    for (int z = 0; z < grid.depth_; ++z) {
        for (int y = 0; y < grid.height_; ++y) {
            for (int x = 0; x < grid.width_; ++x) {
                const glm::vec3 world =
                    bounds.minimum + glm::vec3((x + 0.5f) * cell.x, (y + 0.5f) * cell.y, (z + 0.5f) * cell.z);
                if (!isSolidWorld(world)) continue;
                grid.solid_[grid.cellIndex(x, y, z)] = 1;
                grid.setDensity(x, y, z, 0.f);

                for (const auto& proxy : proxies_) {
                    if (!proxy.enabled) continue;
                    if (signedDistance(proxy, world) > cell.x) continue;
                    const glm::vec3 n  = surfaceNormal(proxy, world);
                    const float     vn = glm::dot(proxy.velocity, n);
                    // Windward push, leeward suction, tangential drag.
                    const glm::vec3 tangential = proxy.velocity - n * vn;
                    const glm::vec3 wake       = n * (vn * proxy.wakeStrength) + tangential * (1.f - proxy.drag);
                    // Stamp onto adjacent faces (wake pair).
                    if (x + 1 < grid.width_) grid.u_[grid.uIndex(x + 1, y, z)] += wake.x * dt;
                    if (x > 0) grid.u_[grid.uIndex(x, y, z)] += wake.x * dt;
                    if (y + 1 < grid.height_) grid.v_[grid.vIndex(x, y + 1, z)] += wake.y * dt;
                    if (y > 0) grid.v_[grid.vIndex(x, y, z)] += wake.y * dt;
                    if (z + 1 < grid.depth_) grid.w_[grid.wIndex(x, y, z + 1)] += wake.z * dt;
                    if (z > 0) grid.w_[grid.wIndex(x, y, z)] += wake.z * dt;
                }
            }
        }
    }

    // Zero velocity into solid faces.
    for (int z = 0; z < grid.depth_; ++z) {
        for (int y = 0; y < grid.height_; ++y) {
            for (int i = 0; i <= grid.width_; ++i) {
                const bool leftSolid  = i > 0 && grid.solid_[grid.cellIndex(i - 1, y, z)];
                const bool rightSolid = i < grid.width_ && grid.solid_[grid.cellIndex(i, y, z)];
                if (leftSolid || rightSolid) grid.u_[grid.uIndex(i, y, z)] = 0.f;
            }
        }
    }
    for (int z = 0; z < grid.depth_; ++z) {
        for (int j = 0; j <= grid.height_; ++j) {
            for (int x = 0; x < grid.width_; ++x) {
                const bool downSolid = j > 0 && grid.solid_[grid.cellIndex(x, j - 1, z)];
                const bool upSolid   = j < grid.height_ && grid.solid_[grid.cellIndex(x, j, z)];
                if (downSolid || upSolid) grid.v_[grid.vIndex(x, j, z)] = 0.f;
            }
        }
    }
    for (int k = 0; k <= grid.depth_; ++k) {
        for (int y = 0; y < grid.height_; ++y) {
            for (int x = 0; x < grid.width_; ++x) {
                const bool backSolid  = k > 0 && grid.solid_[grid.cellIndex(x, y, k - 1)];
                const bool frontSolid = k < grid.depth_ && grid.solid_[grid.cellIndex(x, y, k)];
                if (backSolid || frontSolid) grid.w_[grid.wIndex(x, y, k)] = 0.f;
            }
        }
    }
    return Result<void>::success();
}

glm::vec3 FogInteractor::clipAdvection(const glm::vec3& startLocal, const glm::vec3& deltaLocal,
                                       const FogWorldBounds& bounds, const glm::vec3& cellSize) const noexcept {
    if (proxies_.empty()) return startLocal + deltaLocal;

    const float len = glm::length(deltaLocal);
    if (len < 1e-8f) return startLocal;

    const int       steps = std::max(1, static_cast<int>(std::ceil(len * 2.f)));
    const glm::vec3 step  = deltaLocal / static_cast<float>(steps);
    glm::vec3       p     = startLocal;
    for (int i = 0; i < steps; ++i) {
        const glm::vec3 next  = p + step;
        const glm::vec3 world = bounds.minimum + glm::vec3((next.x + 0.5f) * cellSize.x, (next.y + 0.5f) * cellSize.y,
                                                           (next.z + 0.5f) * cellSize.z);
        if (isSolidWorld(world)) return p;
        p = next;
    }
    return p;
}

}  // namespace eve::graphics::fog

// FogProfile.cpp
namespace eve::graphics::fog {
namespace {

[[nodiscard]] bool finiteVec3(const glm::vec3& v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

[[nodiscard]] float henyeyGreenstein(float g, float cosTheta) noexcept {
    const float g2      = g * g;
    const float denom   = 1.f + g2 - 2.f * g * cosTheta;
    const float denom32 = denom * std::sqrt(std::max(denom, 1e-8f));
    return (1.f - g2) / (4.f * 3.14159265f * std::max(denom32, 1e-8f));
}

}  // namespace

Result<void> FogProfile::configure(float extinctionPerDensity, const glm::vec3& albedo, float ambientOcclusion,
                                   float ambientUp, float ambientDown, float anisotropy) {
    if (!std::isfinite(extinctionPerDensity) || extinctionPerDensity < 0.f) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "extinctionPerDensity must be finite and >= 0",
                                                       "extinctionPerDensity", {}, "graphics.fog"));
    }
    if (!finiteVec3(albedo) || albedo.x < 0.f || albedo.y < 0.f || albedo.z < 0.f || albedo.x > 1.f || albedo.y > 1.f ||
        albedo.z > 1.f) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "albedo components must be finite and in [0,1]", "albedo", {},
                                                       "graphics.fog"));
    }
    auto in01 = [](float v) { return std::isfinite(v) && v >= 0.f && v <= 1.f; };
    if (!in01(ambientOcclusion) || !in01(ambientUp) || !in01(ambientDown)) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "ambient terms must be finite and in [0,1]", "ambient", {},
                                                       "graphics.fog"));
    }
    if (!std::isfinite(anisotropy) || anisotropy < -0.99f || anisotropy > 0.99f) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "anisotropy must be finite and in [-0.99,0.99]", "anisotropy",
                                                       {}, "graphics.fog"));
    }

    extinctionPerDensity_ = extinctionPerDensity;
    albedo_               = albedo;
    ambientOcclusion_     = ambientOcclusion;
    ambientUp_            = ambientUp;
    ambientDown_          = ambientDown;
    anisotropy_           = anisotropy;
    return Result<void>::success();
}

float FogProfile::extinctionAt(float density) const noexcept { return extinctionPerDensity_ * std::max(density, 0.f); }

glm::vec3 FogProfile::scatteringAt(float density) const noexcept { return albedo_ * extinctionAt(density); }

float FogProfile::phase(float cosTheta) const noexcept {
    const float c = std::clamp(cosTheta, -1.f, 1.f);
    const float g = anisotropy_;
    // Dual-lobe: forward HG + mild isotropic back lobe for ambient fill.
    return 0.85f * henyeyGreenstein(g, c) + 0.15f * henyeyGreenstein(-0.25f * g, c);
}

glm::vec3 FogProfile::ambientRadiance(const glm::vec3& skyUp, const glm::vec3& groundDown,
                                      float density) const noexcept {
    const float ao = 1.f - ambientOcclusion_ * std::clamp(density, 0.f, 1.f);
    return ao * (ambientUp_ * skyUp + ambientDown_ * groundDown);
}

}  // namespace eve::graphics::fog
