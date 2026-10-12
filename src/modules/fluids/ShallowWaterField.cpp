#include "fluids/ShallowWaterField.h"

#include "common/Diagnostic.h"
#include "fluids/ShallowWaterKernels.h"
#include "gpgpu/ComputeShader.h"
#include "gpgpu/Gpgpu.h"
#include "gpgpu/GpuBuffer.h"
#include "gpgpu/Sequence.h"
#include "image/ImageData.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <glm/glm.hpp>
#include <limits>
#include <string>
#include <utility>

namespace eve::fluids {
namespace {

constexpr int kPushResX          = 0;
constexpr int kPushResZ          = 1;
constexpr int kPushCell          = 2;
constexpr int kPushDt            = 3;
constexpr int kPushGravity       = 4;
constexpr int kPushViscosity     = 5;
constexpr int kPushFriction      = 6;
constexpr int kPushMinDepth      = 7;
constexpr int kPushMaxDepth      = 8;
constexpr int kPushColliderCount = 9;
constexpr int kPushOriginX       = 10;
constexpr int kPushOriginY       = 11;
constexpr int kPushOriginZ       = 12;

[[nodiscard]] bool finite3(const glm::vec3& v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

[[nodiscard]] Result<void> fail(const char* message) {
    return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, message, "fluids.shallowWater"));
}

[[nodiscard]] glm::vec3 fluxX(float h, float hu, float hv, float g, float minH) noexcept {
    if (h <= minH) return {0.f, 0.f, 0.f};
    const float u = hu / h;
    const float v = hv / h;
    return {hu, hu * u + 0.5f * g * h * h, hu * v};
}

[[nodiscard]] glm::vec3 fluxZ(float h, float hu, float hv, float g, float minH) noexcept {
    if (h <= minH) return {0.f, 0.f, 0.f};
    const float u = hu / h;
    const float v = hv / h;
    return {hv, hv * u, hv * v + 0.5f * g * h * h};
}

[[nodiscard]] float colliderFootprintDistance(const ShallowWaterCollider& c, float worldX, float worldZ) noexcept {
    const float dx = worldX - c.center.x;
    const float dz = worldZ - c.center.z;
    if (c.shape == ShallowWaterColliderShape::Box) {
        const float ox = std::abs(dx) - c.halfExtent.x;
        const float oz = std::abs(dz) - c.halfExtent.z;
        const float outside = std::sqrt(std::max(ox, 0.f) * std::max(ox, 0.f) + std::max(oz, 0.f) * std::max(oz, 0.f));
        return outside + std::min(std::max(ox, oz), 0.f);
    }
    return std::sqrt(dx * dx + dz * dz) - c.radius;
}

[[nodiscard]] float colliderImmersion(const ShallowWaterCollider& c, float surfaceY) noexcept {
    if (c.shape == ShallowWaterColliderShape::Sphere) {
        return std::clamp(c.center.y + c.radius - surfaceY, 0.f, 2.f * c.radius);
    }
    if (c.shape == ShallowWaterColliderShape::Capsule) {
        const float top    = c.center.y + c.halfExtent.y + c.radius;
        const float bottom = c.center.y - c.halfExtent.y - c.radius;
        return std::clamp(top - surfaceY, 0.f, top - bottom);
    }
    return std::clamp(c.center.y + c.halfExtent.y - surfaceY, 0.f, 2.f * c.halfExtent.y);
}

}  // namespace

Result<std::unique_ptr<ShallowWaterField>> ShallowWaterField::create(const ShallowWaterSettings& settings) {
    auto field = std::unique_ptr<ShallowWaterField>(new ShallowWaterField(settings));
    if (auto valid = field->validateSettings(settings); !valid) return Result<std::unique_ptr<ShallowWaterField>>::failure(valid.status());
    const int cells = field->cellCount();
    field->heights_.assign(size_t(cells), settings.initialDepth);
    field->momentumX_.assign(size_t(cells), 0.f);
    field->momentumZ_.assign(size_t(cells), 0.f);
    field->beds_.assign(size_t(cells), settings.bedHeight);
    field->srcH_.assign(size_t(cells), 0.f);
    field->srcHu_.assign(size_t(cells), 0.f);
    field->srcHv_.assign(size_t(cells), 0.f);
    field->scratchH_.assign(size_t(cells), 0.f);
    field->scratchHu_.assign(size_t(cells), 0.f);
    field->scratchHv_.assign(size_t(cells), 0.f);
    return Result<std::unique_ptr<ShallowWaterField>>::success(std::move(field));
}

ShallowWaterField::ShallowWaterField(const ShallowWaterSettings& settings)
    : resX_(settings.resolutionX),
      resZ_(settings.resolutionZ),
      cellSize_(settings.cellSize),
      origin_(settings.origin),
      gravity_(settings.gravity),
      viscosity_(settings.viscosity),
      friction_(settings.friction),
      minDepth_(settings.minDepth),
      maxDepth_(settings.maxDepth),
      cfl_(settings.cfl),
      maxSubsteps_(settings.maxSubsteps),
      preferGpu_(settings.preferGpu) {}

ShallowWaterField::~ShallowWaterField() {
    // gpgpu_ comes from Gpgpu::create() (module registry ownership), same as FluidSimulator.
    delete seq_;
    delete stageState_;
    delete bufColliders_;
    delete bufSources_;
    delete bufBed_;
    delete bufStateB_;
    delete bufStateA_;
    delete shIntegrate_;
    delete shRaster_;
    delete shClear_;
}

Result<void> ShallowWaterField::validateSettings(const ShallowWaterSettings& settings) const {
    if (settings.resolutionX < 8 || settings.resolutionX > 512 || settings.resolutionZ < 8 || settings.resolutionZ > 512)
        return fail("resolutionX/Z must be in [8, 512]");
    if (!(settings.cellSize > 0.f) || settings.cellSize > 10.f || !std::isfinite(settings.cellSize))
        return fail("cellSize must be in (0, 10]");
    if (!finite3(settings.origin)) return fail("origin must be finite");
    if (!(settings.gravity > 0.f) || settings.gravity > 100.f || !std::isfinite(settings.gravity))
        return fail("gravity must be in (0, 100]");
    if (!(settings.viscosity >= 0.f) || settings.viscosity > 1.f || !std::isfinite(settings.viscosity))
        return fail("viscosity must be in [0, 1]");
    if (!(settings.friction >= 0.f) || settings.friction > 8.f || !std::isfinite(settings.friction))
        return fail("friction must be in [0, 8]");
    if (!(settings.minDepth > 0.f) || settings.minDepth > 1.f || !std::isfinite(settings.minDepth))
        return fail("minDepth must be in (0, 1]");
    if (!(settings.maxDepth > settings.minDepth) || settings.maxDepth > 100.f || !std::isfinite(settings.maxDepth))
        return fail("maxDepth must be in (minDepth, 100]");
    if (!(settings.cfl > 0.f) || settings.cfl > 1.f || !std::isfinite(settings.cfl)) return fail("cfl must be in (0, 1]");
    if (settings.maxSubsteps < 1 || settings.maxSubsteps > 64) return fail("maxSubsteps must be in [1, 64]");
    if (!(settings.initialDepth >= 0.f) || settings.initialDepth > settings.maxDepth || !std::isfinite(settings.initialDepth))
        return fail("initialDepth must be in [0, maxDepth]");
    if (!std::isfinite(settings.bedHeight)) return fail("bedHeight must be finite");
    const uint64_t cells = uint64_t(settings.resolutionX) * uint64_t(settings.resolutionZ);
    if (cells > 512ull * 512ull) return fail("cell count exceeds 512^2 budget");
    return Result<void>::success();
}

ShallowWaterSettings ShallowWaterField::settings() const noexcept {
    ShallowWaterSettings s;
    s.resolutionX  = resX_;
    s.resolutionZ  = resZ_;
    s.cellSize     = cellSize_;
    s.origin       = origin_;
    s.gravity      = gravity_;
    s.viscosity    = viscosity_;
    s.friction     = friction_;
    s.minDepth     = minDepth_;
    s.maxDepth     = maxDepth_;
    s.cfl          = cfl_;
    s.maxSubsteps  = maxSubsteps_;
    s.preferGpu    = preferGpu_;
    s.initialDepth = heights_.empty() ? 0.f : heights_[0];
    s.bedHeight    = beds_.empty() ? 0.f : beds_[0];
    return s;
}

Result<void> ShallowWaterField::setColliders(std::span<const ShallowWaterCollider> colliders) {
    if (colliders.size() > 64) return fail("at most 64 shallow-water colliders");
    for (size_t i = 0; i < colliders.size(); ++i) {
        const auto& c = colliders[i];
        if (!finite3(c.center) || !finite3(c.halfExtent) || !finite3(c.velocity) || !std::isfinite(c.radius) ||
            !std::isfinite(c.displacementScale) || !std::isfinite(c.dragScale) || c.radius < 0.001f || c.radius > 100.f ||
            c.displacementScale < 0.f || c.displacementScale > 10.f || c.dragScale < 0.f || c.dragScale > 10.f ||
            glm::length(c.velocity) > 100.f ||
            (c.shape != ShallowWaterColliderShape::Sphere && c.shape != ShallowWaterColliderShape::Capsule &&
             c.shape != ShallowWaterColliderShape::Box))
            return fail("invalid shallow-water collider sample");
        if (c.shape == ShallowWaterColliderShape::Box &&
            (c.halfExtent.x <= 0.f || c.halfExtent.y <= 0.f || c.halfExtent.z <= 0.f))
            return fail("box collider halfExtent must be positive");
        if (c.shape == ShallowWaterColliderShape::Capsule && c.halfExtent.y <= 0.f)
            return fail("capsule halfExtent.y must be positive");
        for (size_t j = 0; j < i; ++j)
            if (colliders[j].label == c.label) return fail("duplicate shallow-water collider label");
    }
    colliders_.assign(colliders.begin(), colliders.end());
    return Result<void>::success();
}

Result<void> ShallowWaterField::setBed(std::span<const float> beds) {
    if (int(beds.size()) != cellCount()) return fail("bed buffer size must equal cellCount()");
    for (float v : beds)
        if (!std::isfinite(v)) return fail("bed heights must be finite");
    beds_.assign(beds.begin(), beds.end());
    stateOnGpu_ = false;
    if (gpuOk_ && bufBed_) bufBed_->writeFloat32s(beds_.data(), int(beds_.size()));
    return Result<void>::success();
}

Result<void> ShallowWaterField::fillDepth(float depth) {
    if (!std::isfinite(depth) || depth < 0.f || depth > maxDepth_) return fail("fillDepth out of range");
    std::fill(heights_.begin(), heights_.end(), depth);
    std::fill(momentumX_.begin(), momentumX_.end(), 0.f);
    std::fill(momentumZ_.begin(), momentumZ_.end(), 0.f);
    stateOnGpu_ = false;
    return Result<void>::success();
}

void ShallowWaterField::worldToCell(float worldX, float worldZ, float& fx, float& fz) const noexcept {
    fx = (worldX - origin_.x) / cellSize_ - 0.5f;
    fz = (worldZ - origin_.z) / cellSize_ - 0.5f;
}

Result<void> ShallowWaterField::disturbAt(float worldX, float worldZ, float radius, float heightDelta,
                                          float radialSpeed) {
    if (!std::isfinite(worldX) || !std::isfinite(worldZ) || !std::isfinite(radius) || !std::isfinite(heightDelta) ||
        !std::isfinite(radialSpeed) || radius <= 0.f || radius > 50.f || std::abs(heightDelta) > maxDepth_ ||
        std::abs(radialSpeed) > 50.f)
        return fail("invalid disturbAt arguments");
    stateOnGpu_ = false;
    const float r2 = radius * radius;
    for (int z = 0; z < resZ_; ++z) {
        for (int x = 0; x < resX_; ++x) {
            const float wx = origin_.x + (float(x) + 0.5f) * cellSize_;
            const float wz = origin_.z + (float(z) + 0.5f) * cellSize_;
            const float dx = wx - worldX;
            const float dz = wz - worldZ;
            const float d2 = dx * dx + dz * dz;
            if (d2 > r2) continue;
            const float w   = 1.f - std::sqrt(d2) / radius;
            const int   i   = index(x, z);
            heights_[size_t(i)] = std::clamp(heights_[size_t(i)] + heightDelta * w, 0.f, maxDepth_);
            if (std::abs(radialSpeed) > 1e-6f && d2 > 1e-8f) {
                const float inv = 1.f / std::sqrt(d2);
                const float h   = std::max(heights_[size_t(i)], minDepth_);
                momentumX_[size_t(i)] += dx * inv * radialSpeed * w * h;
                momentumZ_[size_t(i)] += dz * inv * radialSpeed * w * h;
            }
        }
    }
    return Result<void>::success();
}

Result<void> ShallowWaterField::addImpulse(float worldX, float worldZ, float radius, float strength) {
    const float radial = std::abs(strength) * 1.5f;
    return disturbAt(worldX, worldZ, radius, strength, strength >= 0.f ? radial : -radial);
}

float ShallowWaterField::surfaceHeightAt(float worldX, float worldZ) const noexcept {
    float fx = 0.f, fz = 0.f;
    worldToCell(worldX, worldZ, fx, fz);
    const int   x0 = int(std::floor(fx));
    const int   z0 = int(std::floor(fz));
    const float tx = fx - float(x0);
    const float tz = fz - float(z0);
    auto sample = [&](int x, int z) -> float {
        if (!inside(x, z)) return origin_.y;
        const int i = index(x, z);
        return beds_[size_t(i)] + heights_[size_t(i)];
    };
    const float s00 = sample(x0, z0);
    const float s10 = sample(x0 + 1, z0);
    const float s01 = sample(x0, z0 + 1);
    const float s11 = sample(x0 + 1, z0 + 1);
    const float a   = s00 * (1.f - tx) + s10 * tx;
    const float b   = s01 * (1.f - tx) + s11 * tx;
    return a * (1.f - tz) + b * tz;
}

float ShallowWaterField::depthAt(float worldX, float worldZ) const noexcept {
    float fx = 0.f, fz = 0.f;
    worldToCell(worldX, worldZ, fx, fz);
    const int   x0 = int(std::floor(fx));
    const int   z0 = int(std::floor(fz));
    const float tx = fx - float(x0);
    const float tz = fz - float(z0);
    auto sample = [&](int x, int z) -> float {
        if (!inside(x, z)) return 0.f;
        return heights_[size_t(index(x, z))];
    };
    const float a = sample(x0, z0) * (1.f - tx) + sample(x0 + 1, z0) * tx;
    const float b = sample(x0, z0 + 1) * (1.f - tx) + sample(x0 + 1, z0 + 1) * tx;
    return a * (1.f - tz) + b * tz;
}

glm::vec2 ShallowWaterField::velocityAt(float worldX, float worldZ) const noexcept {
    float fx = 0.f, fz = 0.f;
    worldToCell(worldX, worldZ, fx, fz);
    const int   x0 = int(std::floor(fx));
    const int   z0 = int(std::floor(fz));
    const float tx = fx - float(x0);
    const float tz = fz - float(z0);
    auto sample = [&](int x, int z) -> glm::vec2 {
        if (!inside(x, z)) return {0.f, 0.f};
        const int   i = index(x, z);
        const float h = std::max(heights_[size_t(i)], minDepth_);
        return {momentumX_[size_t(i)] / h, momentumZ_[size_t(i)] / h};
    };
    const glm::vec2 a = sample(x0, z0) * (1.f - tx) + sample(x0 + 1, z0) * tx;
    const glm::vec2 b = sample(x0, z0 + 1) * (1.f - tx) + sample(x0 + 1, z0 + 1) * tx;
    return a * (1.f - tz) + b * tz;
}

Result<void> ShallowWaterField::buildSurfaceMesh(std::vector<float>& positionsXYZ, std::vector<float>& normalsXYZ,
                                                 std::vector<float>& uvsST, std::vector<uint32_t>& indices) const {
    const int vx = resX_;
    const int vz = resZ_;
    positionsXYZ.resize(size_t(vx * vz) * 3u);
    normalsXYZ.resize(size_t(vx * vz) * 3u);
    uvsST.resize(size_t(vx * vz) * 2u);
    indices.clear();
    indices.reserve(size_t((vx - 1) * (vz - 1) * 6));

    for (int z = 0; z < vz; ++z) {
        for (int x = 0; x < vx; ++x) {
            const int   i  = index(x, z);
            const size_t p = size_t(i) * 3u;
            const size_t u = size_t(i) * 2u;
            positionsXYZ[p]     = origin_.x + (float(x) + 0.5f) * cellSize_;
            positionsXYZ[p + 1] = beds_[size_t(i)] + heights_[size_t(i)];
            positionsXYZ[p + 2] = origin_.z + (float(z) + 0.5f) * cellSize_;
            uvsST[u]     = float(x) / float(std::max(vx - 1, 1));
            uvsST[u + 1] = float(z) / float(std::max(vz - 1, 1));
        }
    }
    for (int z = 0; z < vz; ++z) {
        for (int x = 0; x < vx; ++x) {
            const auto pos = [&](int cx, int cz) -> glm::vec3 {
                cx = std::clamp(cx, 0, vx - 1);
                cz = std::clamp(cz, 0, vz - 1);
                const size_t p = size_t(index(cx, cz)) * 3u;
                return {positionsXYZ[p], positionsXYZ[p + 1], positionsXYZ[p + 2]};
            };
            const glm::vec3 dx = pos(x + 1, z) - pos(x - 1, z);
            const glm::vec3 dz = pos(x, z + 1) - pos(x, z - 1);
            glm::vec3       n  = glm::normalize(glm::cross(dz, dx));
            if (!std::isfinite(n.x) || !std::isfinite(n.y) || !std::isfinite(n.z)) n = {0.f, 1.f, 0.f};
            const size_t p      = size_t(index(x, z)) * 3u;
            normalsXYZ[p]       = n.x;
            normalsXYZ[p + 1]   = n.y;
            normalsXYZ[p + 2]   = n.z;
        }
    }
    for (int z = 0; z < vz - 1; ++z) {
        for (int x = 0; x < vx - 1; ++x) {
            const uint32_t i00 = uint32_t(index(x, z));
            const uint32_t i10 = uint32_t(index(x + 1, z));
            const uint32_t i01 = uint32_t(index(x, z + 1));
            const uint32_t i11 = uint32_t(index(x + 1, z + 1));
            indices.push_back(i00);
            indices.push_back(i01);
            indices.push_back(i10);
            indices.push_back(i10);
            indices.push_back(i01);
            indices.push_back(i11);
        }
    }
    return Result<void>::success();
}

namespace {

[[nodiscard]] uint8_t toU8(float v) noexcept {
    return uint8_t(std::lround(std::clamp(v, 0.f, 1.f) * 255.f));
}

[[nodiscard]] Result<void> requireRgbaGrid(const image::ImageData& image, int resX, int resZ) {
    if (image.getFormat() != "RGBA8" || image.getWidth() != resX || image.getHeight() != resZ || !image.getData())
        return fail("field image must be RGBA8 with width=resolutionX and height=resolutionZ");
    return Result<void>::success();
}

}  // namespace

Result<void> ShallowWaterField::copyFieldToImage(image::ImageData& image, float velocityScale) const {
    if (auto shape = requireRgbaGrid(image, resX_, resZ_); !shape) return shape;
    if (!(velocityScale > 0.f) || velocityScale > 50.f || !std::isfinite(velocityScale))
        return fail("velocityScale must be in (0, 50]");

    auto* pixels = static_cast<uint8_t*>(image.getData());
    for (int z = 0; z < resZ_; ++z) {
        for (int x = 0; x < resX_; ++x) {
            const int   i     = index(x, z);
            const float h     = heights_[size_t(i)];
            const float invH  = 1.f / std::max(h, minDepth_);
            const float vx    = momentumX_[size_t(i)] * invH;
            const float vz    = momentumZ_[size_t(i)] * invH;
            const float speed = std::sqrt(vx * vx + vz * vz);
            const size_t p    = size_t(z * resX_ + x) * 4u;
            pixels[p + 0]     = toU8(h / std::max(maxDepth_, 1e-4f));
            pixels[p + 1]     = toU8(0.5f + 0.5f * std::clamp(vx / velocityScale, -1.f, 1.f));
            pixels[p + 2]     = toU8(0.5f + 0.5f * std::clamp(vz / velocityScale, -1.f, 1.f));
            pixels[p + 3]     = toU8(std::clamp(speed / velocityScale, 0.f, 1.f));
        }
    }
    return Result<void>::success();
}

Result<void> ShallowWaterField::copyShadedAlbedoToImage(image::ImageData& image, float restDepth,
                                                        float velocityScale) const {
    if (auto shape = requireRgbaGrid(image, resX_, resZ_); !shape) return shape;
    if (!(restDepth > 0.f) || restDepth > maxDepth_ || !std::isfinite(restDepth))
        return fail("restDepth must be in (0, maxDepth]");
    if (!(velocityScale > 0.f) || velocityScale > 50.f || !std::isfinite(velocityScale))
        return fail("velocityScale must be in (0, 50]");

    auto*       pixels   = static_cast<uint8_t*>(image.getData());
    const float invRest  = 1.f / restDepth;
    for (int z = 0; z < resZ_; ++z) {
        for (int x = 0; x < resX_; ++x) {
            const int   i       = index(x, z);
            const float h       = heights_[size_t(i)];
            const float invH    = 1.f / std::max(h, minDepth_);
            const float vx      = momentumX_[size_t(i)] * invH;
            const float vz      = momentumZ_[size_t(i)] * invH;
            const float speed   = std::sqrt(vx * vx + vz * vz);
            // Exaggerate small height anomalies so crests/troughs read clearly in albedo.
            const float anomaly = std::clamp((h - restDepth) * invRest * 4.f, -1.f, 1.f);
            const float foam    = std::clamp(speed / velocityScale, 0.f, 1.f);
            const float flowX   = std::clamp(vx / velocityScale, -1.f, 1.f);
            const float flowZ   = std::clamp(vz / velocityScale, -1.f, 1.f);

            float r = 0.05f + 0.10f * (0.5f + 0.5f * anomaly) + 0.08f * flowX;
            float g = 0.28f + 0.35f * (0.5f + 0.5f * anomaly) + 0.12f * flowZ;
            float b = 0.42f + 0.45f * (0.5f + 0.5f * anomaly);
            r       = r * (1.f - foam) + foam;
            g       = g * (1.f - foam) + foam;
            b       = b * (1.f - 0.85f * foam) + 0.85f * foam;

            const size_t p = size_t(z * resX_ + x) * 4u;
            pixels[p + 0]  = toU8(r);
            pixels[p + 1]  = toU8(g);
            pixels[p + 2]  = toU8(b);
            pixels[p + 3]  = toU8(0.72f + 0.28f * foam);
        }
    }
    return Result<void>::success();
}

Result<void> ShallowWaterField::copyNormalToImage(image::ImageData& image, float normalStrength) const {
    if (auto shape = requireRgbaGrid(image, resX_, resZ_); !shape) return shape;
    if (!(normalStrength > 0.f) || normalStrength > 64.f || !std::isfinite(normalStrength))
        return fail("normalStrength must be in (0, 64]");

    auto* pixels = static_cast<uint8_t*>(image.getData());
    for (int z = 0; z < resZ_; ++z) {
        for (int x = 0; x < resX_; ++x) {
            const auto sampleH = [&](int cx, int cz) -> float {
                cx = std::clamp(cx, 0, resX_ - 1);
                cz = std::clamp(cz, 0, resZ_ - 1);
                const int i = index(cx, cz);
                return beds_[size_t(i)] + heights_[size_t(i)];
            };
            const float dHx = (sampleH(x + 1, z) - sampleH(x - 1, z)) / (2.f * cellSize_);
            const float dHz = (sampleH(x, z + 1) - sampleH(x, z - 1)) / (2.f * cellSize_);
            // Tangent-space normal with Z-up (OpenGL-style normal maps).
            glm::vec3 n{-dHx * normalStrength, -dHz * normalStrength, 1.f};
            n = glm::normalize(n);
            if (!std::isfinite(n.x) || !std::isfinite(n.y) || !std::isfinite(n.z)) n = {0.f, 0.f, 1.f};

            const size_t p = size_t(z * resX_ + x) * 4u;
            pixels[p + 0]  = toU8(0.5f + 0.5f * n.x);
            pixels[p + 1]  = toU8(0.5f + 0.5f * n.y);
            pixels[p + 2]  = toU8(0.5f + 0.5f * n.z);
            pixels[p + 3]  = 255;
        }
    }
    return Result<void>::success();
}

void ShallowWaterField::rasterizeCollidersCpu(float dt) {
    std::fill(srcH_.begin(), srcH_.end(), 0.f);
    std::fill(srcHu_.begin(), srcHu_.end(), 0.f);
    std::fill(srcHv_.begin(), srcHv_.end(), 0.f);
    if (colliders_.empty() || !(dt > 0.f)) return;

    const float coverRadius = cellSize_ * 0.75f;
    const float maxSrcH     = maxDepth_ * 0.15f;
    for (int z = 0; z < resZ_; ++z) {
        for (int x = 0; x < resX_; ++x) {
            const int   i  = index(x, z);
            const float wx = origin_.x + (float(x) + 0.5f) * cellSize_;
            const float wz = origin_.z + (float(z) + 0.5f) * cellSize_;
            const float surfaceY = beds_[size_t(i)] + heights_[size_t(i)];
            for (const auto& c : colliders_) {
                const float dist = colliderFootprintDistance(c, wx, wz);
                if (dist >= coverRadius) continue;
                const float immersion = colliderImmersion(c, surfaceY);
                if (immersion <= 1e-4f) continue;
                const float cover = std::clamp(1.f - dist / coverRadius, 0.f, 1.f);
                // Rates × substep dt — absolute sources stacked with CFL substeps
                // previously grew into multi-meter height spikes.
                const float push = immersion * cover * c.displacementScale * dt;
                float       dx   = wx - c.center.x;
                float       dz   = wz - c.center.z;
                const float olen = std::sqrt(dx * dx + dz * dz);
                if (olen > 1e-5f) {
                    dx /= olen;
                    dz /= olen;
                } else {
                    dx = 0.f;
                    dz = 1.f;
                }
                const float radial = push * 1.f;
                const float h      = std::max(heights_[size_t(i)], minDepth_);
                srcH_[size_t(i)] -= push * 0.35f;
                srcHu_[size_t(i)] += (dx * radial + c.velocity.x * c.dragScale * dt) * h;
                srcHv_[size_t(i)] += (dz * radial + c.velocity.z * c.dragScale * dt) * h;
                srcH_[size_t(i)] += std::max(0.f, -c.velocity.y) * cover * 0.15f * c.displacementScale * dt;
            }
            srcH_[size_t(i)] = std::clamp(srcH_[size_t(i)], -maxSrcH, maxSrcH);
        }
    }
}

void ShallowWaterField::integrateCpu(float dt) {
    // Local Lax-Friedrichs wave-speed bound (gravity waves + horizontal flow).
    float maxWave = std::sqrt(gravity_ * minDepth_) + 1.f;
    for (int i = 0; i < cellCount(); ++i) {
        const float h = std::max(heights_[size_t(i)], minDepth_);
        const float u = momentumX_[size_t(i)] / h;
        const float v = momentumZ_[size_t(i)] / h;
        maxWave       = std::max(maxWave, std::abs(u) + std::sqrt(gravity_ * h));
        maxWave       = std::max(maxWave, std::abs(v) + std::sqrt(gravity_ * h));
    }
    const float alpha = maxWave;
    auto sample = [&](int x, int z, float& h, float& hu, float& hv) {
        x  = std::clamp(x, 0, resX_ - 1);
        z  = std::clamp(z, 0, resZ_ - 1);
        const int i = index(x, z);
        h  = std::max(heights_[size_t(i)] + srcH_[size_t(i)], 0.f);
        hu = momentumX_[size_t(i)] + srcHu_[size_t(i)];
        hv = momentumZ_[size_t(i)] + srcHv_[size_t(i)];
        if (!std::isfinite(h)) h = 0.f;
        if (!std::isfinite(hu)) hu = 0.f;
        if (!std::isfinite(hv)) hv = 0.f;
    };
    auto sampleBed = [&](int x, int z) {
        x = std::clamp(x, 0, resX_ - 1);
        z = std::clamp(z, 0, resZ_ - 1);
        return beds_[size_t(index(x, z))];
    };

    for (int z = 0; z < resZ_; ++z) {
        for (int x = 0; x < resX_; ++x) {
            float h = 0, hu = 0, hv = 0;
            float hL = 0, huL = 0, hvL = 0;
            float hR = 0, huR = 0, hvR = 0;
            float hD = 0, huD = 0, hvD = 0;
            float hU = 0, huU = 0, hvU = 0;
            sample(x, z, h, hu, hv);
            sample(x - 1, z, hL, huL, hvL);
            sample(x + 1, z, hR, huR, hvR);
            sample(x, z - 1, hD, huD, hvD);
            sample(x, z + 1, hU, huU, hvU);

            const glm::vec3 q{h, hu, hv};
            const glm::vec3 qL{hL, huL, hvL};
            const glm::vec3 qR{hR, huR, hvR};
            const glm::vec3 qD{hD, huD, hvD};
            const glm::vec3 qU{hU, huU, hvU};

            const glm::vec3 fR = 0.5f * (fluxX(h, hu, hv, gravity_, minDepth_) + fluxX(hR, huR, hvR, gravity_, minDepth_)) -
                                 0.5f * alpha * (qR - q);
            const glm::vec3 fL = 0.5f * (fluxX(hL, huL, hvL, gravity_, minDepth_) + fluxX(h, hu, hv, gravity_, minDepth_)) -
                                 0.5f * alpha * (q - qL);
            const glm::vec3 gUflux =
                0.5f * (fluxZ(h, hu, hv, gravity_, minDepth_) + fluxZ(hU, huU, hvU, gravity_, minDepth_)) -
                0.5f * alpha * (qU - q);
            const glm::vec3 gDflux =
                0.5f * (fluxZ(hD, huD, hvD, gravity_, minDepth_) + fluxZ(h, hu, hv, gravity_, minDepth_)) -
                0.5f * alpha * (q - qD);

            const float b  = sampleBed(x, z);
            const float bL = sampleBed(x - 1, z);
            const float bR = sampleBed(x + 1, z);
            const float bD = sampleBed(x, z - 1);
            const float bU = sampleBed(x, z + 1);
            const float hSafe = std::max(h, minDepth_);
            const glm::vec3 source{0.f, -gravity_ * hSafe * (bR - bL) / (2.f * cellSize_),
                                   -gravity_ * hSafe * (bU - bD) / (2.f * cellSize_)};

            glm::vec3 qn = q - (dt / cellSize_) * (fR - fL + gUflux - gDflux) + dt * source;
            // dt-scaled Laplacian viscosity (was previously unscaled and blew up).
            qn.y += viscosity_ * dt * (huL + huR + huD + huU - 4.f * hu);
            qn.z += viscosity_ * dt * (hvL + hvR + hvD + hvU - 4.f * hv);
            qn.y *= std::max(0.f, 1.f - friction_ * dt);
            qn.z *= std::max(0.f, 1.f - friction_ * dt);
            qn.x = std::clamp(qn.x, 0.f, maxDepth_);
            if (!std::isfinite(qn.x)) qn.x = 0.f;
            if (!std::isfinite(qn.y)) qn.y = 0.f;
            if (!std::isfinite(qn.z)) qn.z = 0.f;
            if (qn.x <= minDepth_) {
                qn.y = 0.f;
                qn.z = 0.f;
            } else {
                // Cap horizontal speed so a single splash cannot break CFL next step.
                const float speed = std::sqrt(qn.y * qn.y + qn.z * qn.z) / std::max(qn.x, minDepth_);
                const float maxSpeed = 8.f;
                if (speed > maxSpeed) {
                    const float s = maxSpeed / speed;
                    qn.y *= s;
                    qn.z *= s;
                }
            }
            if (x == 0 || x == resX_ - 1) qn.y = 0.f;
            if (z == 0 || z == resZ_ - 1) qn.z = 0.f;

            const int i           = index(x, z);
            scratchH_[size_t(i)]  = qn.x;
            scratchHu_[size_t(i)] = qn.y;
            scratchHv_[size_t(i)] = qn.z;
        }
    }
    heights_.swap(scratchH_);
    momentumX_.swap(scratchHu_);
    momentumZ_.swap(scratchHv_);
}

void ShallowWaterField::stepCpu(float dt) {
    rasterizeCollidersCpu(dt);
    integrateCpu(dt);
}

Result<void> ShallowWaterField::step(float dt) {
    if (!std::isfinite(dt) || dt < 0.f) return fail("dt must be finite and >= 0");
    if (dt == 0.f) return Result<void>::success();
    // Long first frames after engine startup commonly exceed 1s; advance in
    // capped chunks instead of rejecting the call.
    float remaining = std::min(dt, 5.f);
    while (remaining > 1e-6f) {
        const float chunk = std::min(remaining, 1.f / 20.f);  // <= 50 ms chunks
        float       maxH  = minDepth_;
        float       maxVel = 0.f;
        for (int i = 0; i < cellCount(); ++i) {
            maxH = std::max(maxH, heights_[size_t(i)]);
            const float h = std::max(heights_[size_t(i)], minDepth_);
            const float u = momentumX_[size_t(i)] / h;
            const float v = momentumZ_[size_t(i)] / h;
            maxVel        = std::max(maxVel, std::sqrt(u * u + v * v));
        }
        const float waveSpeed = std::sqrt(gravity_ * maxH) + maxVel + 1.f;
        const float safeDt    = cfl_ * cellSize_ / std::max(waveSpeed, 1e-3f);
        int         substeps  = std::clamp(int(std::ceil(chunk / safeDt)), 1, maxSubsteps_);
        const float subDt     = chunk / float(substeps);

        bool advanced = false;
        if (preferGpu_) {
            if (ensureGpu()) {
                auto gpu = stepGpu(chunk);
                if (gpu) {
                    advanced = true;
                } else {
                    backendFallback_ = true;
                    gpuOk_           = false;
                }
            } else {
                backendFallback_ = true;
            }
        }
        if (!advanced) {
            for (int i = 0; i < substeps; ++i) stepCpu(subDt);
            stateOnGpu_ = false;
        }
        remaining -= chunk;
    }
    return Result<void>::success();
}

bool ShallowWaterField::ensureGpu() {
    if (gpuOk_) return true;
    gpgpu_ = eve::gpgpu::Gpgpu::create();
    if (!gpgpu_ || !gpgpu_->isAvailable()) return false;
    const int cells = cellCount();
    try {
        shClear_     = gpgpu_->newShader(kShallowWaterClearSources);
        shRaster_    = gpgpu_->newShader(kShallowWaterRasterizeColliders);
        shIntegrate_ = gpgpu_->newShader(kShallowWaterIntegrate);
        bufStateA_   = gpgpu_->newBuffer(cells * 4 * int(sizeof(float)), "storage");
        bufStateB_   = gpgpu_->newBuffer(cells * 4 * int(sizeof(float)), "storage");
        bufBed_      = gpgpu_->newBuffer(cells * int(sizeof(float)), "storage");
        bufSources_  = gpgpu_->newBuffer(cells * 4 * int(sizeof(float)), "storage");
        bufColliders_ = gpgpu_->newBuffer(64 * 16 * int(sizeof(float)), "storage");
        stageState_  = gpgpu_->newBuffer(cells * 4 * int(sizeof(float)), "staging");
        seq_         = gpgpu_->newSequence();
    } catch (...) {
        return false;
    }
    if (!seq_ || !seq_->isAvailable() || !shClear_ || !shRaster_ || !shIntegrate_) return false;

    shClear_->bindBuffer(3, bufSources_);
    shRaster_->bindBuffer(0, bufStateA_);
    shRaster_->bindBuffer(2, bufBed_);
    shRaster_->bindBuffer(3, bufSources_);
    shRaster_->bindBuffer(4, bufColliders_);
    shIntegrate_->bindBuffer(0, bufStateA_);
    shIntegrate_->bindBuffer(1, bufStateB_);
    shIntegrate_->bindBuffer(2, bufBed_);
    shIntegrate_->bindBuffer(3, bufSources_);
    bufBed_->writeFloat32s(beds_.data(), int(beds_.size()));
    gpuOk_      = true;
    stateOnGpu_ = false;
    pingA_      = true;
    return true;
}

void ShallowWaterField::setPushConstants(gpgpu::ComputeShader* shader, float dt) const {
    if (!shader) return;
    shader->setFloat(kPushResX, float(resX_));
    shader->setFloat(kPushResZ, float(resZ_));
    shader->setFloat(kPushCell, cellSize_);
    shader->setFloat(kPushDt, dt);
    shader->setFloat(kPushGravity, gravity_);
    shader->setFloat(kPushViscosity, viscosity_);
    shader->setFloat(kPushFriction, friction_);
    shader->setFloat(kPushMinDepth, minDepth_);
    shader->setFloat(kPushMaxDepth, maxDepth_);
    shader->setFloat(kPushColliderCount, float(colliders_.size()));
    shader->setFloat(kPushOriginX, origin_.x);
    shader->setFloat(kPushOriginY, origin_.y);
    shader->setFloat(kPushOriginZ, origin_.z);
}

void ShallowWaterField::uploadState() {
    std::vector<float> packed(size_t(cellCount()) * 4u, 0.f);
    for (int i = 0; i < cellCount(); ++i) {
        const size_t b = size_t(i) * 4u;
        packed[b]      = heights_[size_t(i)];
        packed[b + 1]  = momentumX_[size_t(i)];
        packed[b + 2]  = momentumZ_[size_t(i)];
    }
    auto* dst = pingA_ ? bufStateA_ : bufStateB_;
    seq_->recordUpload(dst, packed.data(), uint64_t(packed.size()) * sizeof(float));
}

void ShallowWaterField::downloadState() {
    auto* src = pingA_ ? bufStateA_ : bufStateB_;
    seq_->recordDownload(src, stageState_, uint64_t(cellCount()) * 4u * sizeof(float));
}

void ShallowWaterField::uploadColliders() {
    std::vector<float> packed(64u * 16u, 0.f);
    for (size_t i = 0; i < colliders_.size(); ++i) {
        const auto& c = colliders_[i];
        float*      d = packed.data() + i * 16u;
        d[0]          = c.center.x;
        d[1]          = c.center.y;
        d[2]          = c.center.z;
        d[3]          = c.radius;
        d[4]          = c.halfExtent.x;
        d[5]          = c.halfExtent.y;
        d[6]          = c.halfExtent.z;
        d[7]          = float(c.shape);
        d[8]          = c.velocity.x;
        d[9]          = c.velocity.y;
        d[10]         = c.velocity.z;
        d[11]         = c.displacementScale;
        d[12]         = c.dragScale;
    }
    seq_->recordUpload(bufColliders_, packed.data(), uint64_t(packed.size()) * sizeof(float));
}

Result<void> ShallowWaterField::stepGpu(float dt) {
    float maxH = minDepth_;
    for (float h : heights_) maxH = std::max(maxH, h);
    const float waveSpeed = std::sqrt(gravity_ * maxH) + 1.f;
    const float safeDt    = cfl_ * cellSize_ / std::max(waveSpeed, 1e-3f);
    const int   substeps  = std::clamp(int(std::ceil(dt / safeDt)), 1, maxSubsteps_);
    const float subDt     = dt / float(substeps);
    const int   groupsX   = (resX_ + 7) / 8;
    const int   groupsZ   = (resZ_ + 7) / 8;

    try {
        seq_->begin();
        if (!stateOnGpu_) uploadState();
        uploadColliders();
        for (int s = 0; s < substeps; ++s) {
            auto* src = pingA_ ? bufStateA_ : bufStateB_;
            auto* dst = pingA_ ? bufStateB_ : bufStateA_;
            shClear_->bindBuffer(3, bufSources_);
            setPushConstants(shClear_, subDt);
            seq_->recordDispatch(shClear_, groupsX, groupsZ);

            shRaster_->bindBuffer(0, src);
            shRaster_->bindBuffer(2, bufBed_);
            shRaster_->bindBuffer(3, bufSources_);
            shRaster_->bindBuffer(4, bufColliders_);
            setPushConstants(shRaster_, subDt);
            seq_->recordDispatch(shRaster_, groupsX, groupsZ);

            shIntegrate_->bindBuffer(0, src);
            shIntegrate_->bindBuffer(1, dst);
            shIntegrate_->bindBuffer(2, bufBed_);
            shIntegrate_->bindBuffer(3, bufSources_);
            setPushConstants(shIntegrate_, subDt);
            seq_->recordDispatch(shIntegrate_, groupsX, groupsZ);
            pingA_ = !pingA_;
        }
        downloadState();
        seq_->submit();
        std::vector<float> packed(size_t(cellCount()) * 4u, 0.f);
        stageState_->downloadBytes(packed.data(), uint64_t(packed.size()) * sizeof(float));
        for (int i = 0; i < cellCount(); ++i) {
            const size_t b          = size_t(i) * 4u;
            heights_[size_t(i)]     = packed[b];
            momentumX_[size_t(i)]   = packed[b + 1];
            momentumZ_[size_t(i)]   = packed[b + 2];
        }
        stateOnGpu_ = true;
        return Result<void>::success();
    } catch (const std::exception& error) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed,
                                                       std::string("Shallow-water GPU step failed: ") + error.what(),
                                                       "fluids.shallowWater.gpu"));
    } catch (...) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "Shallow-water GPU step failed", "fluids.shallowWater.gpu"));
    }
}

}  // namespace eve::fluids
