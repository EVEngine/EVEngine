#include "building/HeightfieldSurface.h"

#include "building/PlacementWorld.h"
#include "grid/GridConfig.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace eve::building {
namespace {

template <class T>
eve::Result<T> heightfieldFailure(eve::DiagnosticCode code, const std::string &message,
                                  const std::string &subject = {}) {
    return eve::Result<T>::failure(eve::Diagnostic::error(
        code, message, subject, {}, "building.heightfield-surface"));
}

}  // namespace

HeightfieldSurface::HeightfieldSurface(Config config, std::vector<float> samples)
    : config_(std::move(config)), samples_(std::move(samples)) {}

eve::Result<std::shared_ptr<const HeightfieldSurface>> HeightfieldSurface::create(
    Config config, std::vector<float> samples) {
    if (config.width < 2 || config.height < 2) {
        return heightfieldFailure<std::shared_ptr<const HeightfieldSurface>>(
            eve::DiagnosticCode::InvalidArgument,
            "heightfield dimensions must both be at least two", config.surfaceId);
    }
    if (!std::isfinite(config.originX) || !std::isfinite(config.originY) ||
        !std::isfinite(config.spacingX) || !std::isfinite(config.spacingY) ||
        !std::isfinite(config.heightScale) || !std::isfinite(config.heightOffset) ||
        config.spacingX <= 0.f || config.spacingY <= 0.f) {
        return heightfieldFailure<std::shared_ptr<const HeightfieldSurface>>(
            eve::DiagnosticCode::InvalidArgument,
            "heightfield transform requires finite values and positive spacing",
            config.surfaceId);
    }
    const size_t width = static_cast<size_t>(config.width);
    const size_t height = static_cast<size_t>(config.height);
    if (width > std::numeric_limits<size_t>::max() / height || samples.size() != width * height) {
        return heightfieldFailure<std::shared_ptr<const HeightfieldSurface>>(
            eve::DiagnosticCode::InvalidArgument,
            "heightfield sample count must equal width times height", config.surfaceId);
    }
    for (float sample : samples) {
        if (!std::isfinite(sample)) {
            return heightfieldFailure<std::shared_ptr<const HeightfieldSurface>>(
                eve::DiagnosticCode::InvalidArgument,
                "heightfield samples must all be finite", config.surfaceId);
        }
    }
    return eve::Result<std::shared_ptr<const HeightfieldSurface>>::success(
        std::shared_ptr<const HeightfieldSurface>(
            new HeightfieldSurface(std::move(config), std::move(samples))));
}

eve::Result<PlacementSystem::PlacementHit> HeightfieldSurface::sample(
    const PlacementWorld &world, float planeX, float planeY) const {
    if (!std::isfinite(planeX) || !std::isfinite(planeY)) {
        return heightfieldFailure<PlacementSystem::PlacementHit>(
            eve::DiagnosticCode::InvalidArgument,
            "heightfield sample coordinates must be finite", config_.surfaceId);
    }
    const float u = (planeX - config_.originX) / config_.spacingX;
    const float v = (planeY - config_.originY) / config_.spacingY;
    const float maxU = static_cast<float>(config_.width - 1);
    const float maxV = static_cast<float>(config_.height - 1);
    if (u < 0.f || v < 0.f || u > maxU || v > maxV) {
        return heightfieldFailure<PlacementSystem::PlacementHit>(
            eve::DiagnosticCode::NotFound,
            "heightfield sample coordinate is outside its finite extent", config_.surfaceId);
    }

    const int x0 = std::min(static_cast<int>(std::floor(u)), config_.width - 2);
    const int y0 = std::min(static_cast<int>(std::floor(v)), config_.height - 2);
    const float tx = u - static_cast<float>(x0);
    const float ty = v - static_cast<float>(y0);
    const auto at = [&](int x, int y) {
        return samples_[static_cast<size_t>(y) * static_cast<size_t>(config_.width) +
                        static_cast<size_t>(x)];
    };
    const float h00 = at(x0, y0);
    const float h10 = at(x0 + 1, y0);
    const float h01 = at(x0, y0 + 1);
    const float h11 = at(x0 + 1, y0 + 1);
    const float h0 = h00 + (h10 - h00) * tx;
    const float h1 = h01 + (h11 - h01) * tx;
    const float sampledHeight = (h0 + (h1 - h0) * ty) * config_.heightScale +
                                config_.heightOffset;
    const auto gradientX = [&](int x, int y) {
        if (x == 0) return (at(1, y) - at(0, y)) / config_.spacingX;
        if (x == config_.width - 1)
            return (at(x, y) - at(x - 1, y)) / config_.spacingX;
        return (at(x + 1, y) - at(x - 1, y)) / (2.f * config_.spacingX);
    };
    const auto gradientY = [&](int x, int y) {
        if (y == 0) return (at(x, 1) - at(x, 0)) / config_.spacingY;
        if (y == config_.height - 1)
            return (at(x, y) - at(x, y - 1)) / config_.spacingY;
        return (at(x, y + 1) - at(x, y - 1)) / (2.f * config_.spacingY);
    };
    const auto bilinear = [&](float g00, float g10, float g01, float g11) {
        const float g0 = g00 + (g10 - g00) * tx;
        const float g1 = g01 + (g11 - g01) * tx;
        return (g0 + (g1 - g0) * ty) * config_.heightScale;
    };
    const float dx = bilinear(gradientX(x0, y0), gradientX(x0 + 1, y0),
                              gradientX(x0, y0 + 1), gradientX(x0 + 1, y0 + 1));
    const float dy = bilinear(gradientY(x0, y0), gradientY(x0 + 1, y0),
                              gradientY(x0, y0 + 1), gradientY(x0 + 1, y0 + 1));

    PlacementSystem::PlacementHit hit;
    if (world.getGrid().plane == grid::GridPlane::XZ) {
        hit.worldX = planeX;
        hit.worldY = sampledHeight;
        hit.worldZ = planeY;
        hit.normalX = -dx;
        hit.normalY = 1.f;
        hit.normalZ = -dy;
        hit.tangentX = 1.f;
        hit.tangentY = dx;
        hit.tangentZ = 0.f;
    } else {
        hit.worldX = planeX;
        hit.worldY = planeY;
        hit.worldZ = sampledHeight;
        hit.normalX = -dx;
        hit.normalY = -dy;
        hit.normalZ = 1.f;
        hit.tangentX = 1.f;
        hit.tangentY = 0.f;
        hit.tangentZ = dx;
    }
    hit.surfaceId = config_.surfaceId;
    hit.surfaceRevision = config_.surfaceRevision;
    hit.primitiveId = static_cast<uint64_t>(y0) * static_cast<uint64_t>(config_.width - 1) +
                      static_cast<uint64_t>(x0);
    hit.tags = config_.tags;
    return eve::Result<PlacementSystem::PlacementHit>::success(std::move(hit));
}

}  // namespace eve::building
