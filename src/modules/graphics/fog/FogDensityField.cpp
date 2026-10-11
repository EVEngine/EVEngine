#include "graphics/fog/FogDensityField.h"

#include "common/Diagnostic.h"

#include <algorithm>
#include <cmath>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace eve::graphics::fog {
namespace {

[[nodiscard]] float hash31(int x, int y, int z, std::uint32_t seed) noexcept {
    std::uint32_t n = static_cast<std::uint32_t>(x) * 374761393u +
                      static_cast<std::uint32_t>(y) * 668265263u +
                      static_cast<std::uint32_t>(z) * 2147483647u + seed * 1013904223u;
    n = (n ^ (n >> 13)) * 1274126177u;
    n ^= n >> 16;
    return static_cast<float>(n & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

}  // namespace

Result<void> FogDensityField::resize(int width, int height, int depth, const FogWorldBounds& bounds) {
    if (width < 2 || height < 2 || depth < 2 || width > 256 || height > 256 || depth > 256) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "density field dimensions must be in [2,256]", "size", {},
            "graphics.fog"));
    }
    const glm::vec3 size = bounds.maximum - bounds.minimum;
    if (!(size.x > 0.f && size.y > 0.f && size.z > 0.f) || !std::isfinite(size.x) ||
        !std::isfinite(size.y) || !std::isfinite(size.z)) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "world bounds must be finite with positive extent",
            "bounds", {}, "graphics.fog"));
    }

    width_ = width;
    height_ = height;
    depth_ = depth;
    bounds_ = bounds;
    cellSize_ = glm::vec3(size.x / static_cast<float>(width), size.y / static_cast<float>(height),
                          size.z / static_cast<float>(depth));
    const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) *
                              static_cast<std::size_t>(depth);
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
    return (static_cast<std::size_t>(z) * static_cast<std::size_t>(height_) +
            static_cast<std::size_t>(y)) *
               static_cast<std::size_t>(width_) +
           static_cast<std::size_t>(x);
}

glm::vec3 FogDensityField::cellCenter(int x, int y, int z) const noexcept {
    return bounds_.minimum +
           glm::vec3((static_cast<float>(x) + 0.5f) * cellSize_.x,
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

void FogDensityField::sampleLattice(const glm::vec3& world, int& x0, int& y0, int& z0, int& x1, int& y1,
                                    int& z1, float& fx, float& fy, float& fz) const noexcept {
    const glm::vec3 local = (world - bounds_.minimum) / cellSize_ - glm::vec3(0.5f);
    const float lx = std::clamp(local.x, 0.f, static_cast<float>(std::max(width_ - 1, 0)));
    const float ly = std::clamp(local.y, 0.f, static_cast<float>(std::max(height_ - 1, 0)));
    const float lz = std::clamp(local.z, 0.f, static_cast<float>(std::max(depth_ - 1, 0)));
    x0 = static_cast<int>(std::floor(lx));
    y0 = static_cast<int>(std::floor(ly));
    z0 = static_cast<int>(std::floor(lz));
    x1 = std::min(x0 + 1, width_ - 1);
    y1 = std::min(y0 + 1, height_ - 1);
    z1 = std::min(z0 + 1, depth_ - 1);
    fx = lx - static_cast<float>(x0);
    fy = ly - static_cast<float>(y0);
    fz = lz - static_cast<float>(z0);
}

float FogDensityField::sampleDensity(const glm::vec3& world) const noexcept {
    if (density_.empty() || !bounds_.contains(world)) {
        // Soft falloff just outside bounds keeps ray edges continuous.
        if (density_.empty()) return 0.f;
        const glm::vec3 c = glm::clamp(world, bounds_.minimum, bounds_.maximum);
        if (glm::length(world - c) > cellSize_.x) return 0.f;
    }
    int x0, y0, z0, x1, y1, z1;
    float fx, fy, fz;
    sampleLattice(world, x0, y0, z0, x1, y1, z1, fx, fy, fz);
    const auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
    const float c00 = lerp(densityAt(x0, y0, z0), densityAt(x1, y0, z0), fx);
    const float c10 = lerp(densityAt(x0, y1, z0), densityAt(x1, y1, z0), fx);
    const float c01 = lerp(densityAt(x0, y0, z1), densityAt(x1, y0, z1), fx);
    const float c11 = lerp(densityAt(x0, y1, z1), densityAt(x1, y1, z1), fx);
    return lerp(lerp(c00, c10, fy), lerp(c01, c11, fy), fz);
}

glm::vec3 FogDensityField::sampleCurlVelocity(const glm::vec3& world) const noexcept {
    if (curl_.empty()) return {};
    int x0, y0, z0, x1, y1, z1;
    float fx, fy, fz;
    sampleLattice(world, x0, y0, z0, x1, y1, z1, fx, fy, fz);
    const auto lerp3 = [](const glm::vec3& a, const glm::vec3& b, float t) { return a + (b - a) * t; };
    const glm::vec3 c00 = lerp3(curlVelocityAt(x0, y0, z0), curlVelocityAt(x1, y0, z0), fx);
    const glm::vec3 c10 = lerp3(curlVelocityAt(x0, y1, z0), curlVelocityAt(x1, y1, z0), fx);
    const glm::vec3 c01 = lerp3(curlVelocityAt(x0, y0, z1), curlVelocityAt(x1, y0, z1), fx);
    const glm::vec3 c11 = lerp3(curlVelocityAt(x0, y1, z1), curlVelocityAt(x1, y1, z1), fx);
    return lerp3(lerp3(c00, c10, fy), lerp3(c01, c11, fy), fz);
}

glm::vec3 FogDensityField::sampleLightAssist(const glm::vec3& world) const noexcept {
    if (lightAssist_.empty()) return {};
    int x0, y0, z0, x1, y1, z1;
    float fx, fy, fz;
    sampleLattice(world, x0, y0, z0, x1, y1, z1, fx, fy, fz);
    const auto lerp3 = [](const glm::vec3& a, const glm::vec3& b, float t) { return a + (b - a) * t; };
    const glm::vec3 c00 = lerp3(lightAssistAt(x0, y0, z0), lightAssistAt(x1, y0, z0), fx);
    const glm::vec3 c10 = lerp3(lightAssistAt(x0, y1, z0), lightAssistAt(x1, y1, z0), fx);
    const glm::vec3 c01 = lerp3(lightAssistAt(x0, y0, z1), lightAssistAt(x1, y0, z1), fx);
    const glm::vec3 c11 = lerp3(lightAssistAt(x0, y1, z1), lightAssistAt(x1, y1, z1), fx);
    return lerp3(lerp3(c00, c10, fy), lerp3(c01, c11, fy), fz);
}

Result<void> FogDensityField::seedHeightBand(float baseDensity, float baseHeight, float falloff,
                                             float curlScale, std::uint32_t seed) {
    if (density_.empty()) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed,
                                                       "density field is not allocated", "resize", {},
                                                       "graphics.fog"));
    }
    if (!std::isfinite(baseDensity) || baseDensity < 0.f || !std::isfinite(falloff) || falloff < 0.f ||
        !std::isfinite(curlScale)) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "height-band parameters must be finite and non-negative",
            "seedHeightBand", {}, "graphics.fog"));
    }

    for (int z = 0; z < depth_; ++z) {
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                const glm::vec3 p = cellCenter(x, y, z);
                const float h = std::max(p.y - baseHeight, 0.f);
                float d = baseDensity * std::exp(-falloff * h);
                const float n = 0.65f + 0.35f * hash31(x, y, z, seed);
                d *= n;
                setDensity(x, y, z, d);

                // Analytic curl-noise style assist: divergence-free on average.
                const float a = hash31(x + 17, y, z, seed) * 6.2831853f;
                const float b = hash31(x, y + 31, z, seed) * 6.2831853f;
                const glm::vec3 curl(curlScale * std::sin(a) * std::cos(b),
                                    curlScale * 0.35f * std::sin(a + b),
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

std::span<float> FogDensityField::densitySpan() noexcept { return density_; }
std::span<const float> FogDensityField::densitySpan() const noexcept { return density_; }

}  // namespace eve::graphics::fog
