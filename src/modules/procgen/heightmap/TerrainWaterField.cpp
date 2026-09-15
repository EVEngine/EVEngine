#include "procgen/heightmap/TerrainWaterField.h"
#include <array>
#include "procgen/heightmap/TerrainErosion.h"
#include "procgen/heightmap/TerrainRasterInternal.h"

namespace eve::procgen {

Result<int> generateTerrainWaterFlowMap(Heightmap& target, const Heightmap& source,
                                        const TerrainWaterFlowMapSettings& settings) {
    using namespace raster_detail;
    if (&target == &source || !validRaster(source) || !validRaster(target) ||
        target.getWidth() != source.getHeight() || target.getHeight() != source.getWidth() ||
        !std::isfinite(settings.dropletVolume) || settings.dropletVolume < 0.f ||
        !std::isfinite(settings.absorptionRate) || settings.absorptionRate <= 0.f ||
        settings.smoothIterations < 0)
        return invalid("terrain.waterFlow: transposed finite rasters and valid droplet controls required");

    const int sourceWidth = source.getWidth(), sourceHeight = source.getHeight();
    std::vector<float> working = source.data();
    std::vector<float> flow(size_t(sourceWidth) * sourceHeight, 0.f);
    auto sourceIndex = [sourceWidth](int x, int z) { return size_t(z) * sourceWidth + x; };
    for (int x0 = 1; x0 < sourceWidth - 1; ++x0) {
        for (int z0 = 1; z0 < sourceHeight - 1; ++z0) {
            float volume = settings.dropletVolume;
            int x = x0, z = z0;
            while (volume > 0.f) {
                const size_t index = sourceIndex(x, z);
                const double deposited = double(flow[index]) + settings.absorptionRate;
                if (!isRepresentable(deposited)) return invalid("terrain.waterFlow: accumulation exceeds float range");
                flow[index] = float(deposited);
                const float nextVolume = volume - settings.absorptionRate;
                if (nextVolume == volume)
                    return invalid("terrain.waterFlow: absorption is too small to advance finite float volume");
                volume = nextVolume;

                const float currentHeight = working[index];
                float nextHeight = currentHeight;
                int nextX = x, nextZ = z;
                for (int nx = -1; nx < 2; ++nx)
                    for (int nz = -1; nz < 2; ++nz) {
                        const int testX = x + nx, testZ = z + nz;
                        if (testX < 0 || testX >= sourceWidth || testZ < 0 || testZ >= sourceHeight) continue;
                        const float candidate = working[sourceIndex(testX, testZ)];
                        if (candidate < nextHeight) {
                            nextX = testX; nextZ = testZ; nextHeight = candidate;
                        }
                    }
                if (currentHeight == nextHeight) {
                    const double pooled = double(working[index]) + settings.absorptionRate;
                    if (!isRepresentable(pooled)) return invalid("terrain.waterFlow: pooled height exceeds float range");
                    working[index] = float(pooled);
                } else {
                    x = nextX; z = nextZ;
                }
            }
        }
    }

    std::vector<float> output(flow.size());
    const int outWidth = target.getWidth(), outHeight = target.getHeight();
    for (int x = 0; x < sourceWidth; ++x)
        for (int z = 0; z < sourceHeight; ++z)
            output[size_t(x) * outWidth + z] = flow[sourceIndex(x, z)];
    auto safe = [&](int x, int z) -> float {
        x = std::clamp(x, 0, outWidth - 1); z = std::clamp(z, 0, outHeight - 1);
        return output[size_t(z) * outWidth + x];
    };
    for (int iteration = 0; iteration < settings.smoothIterations; ++iteration)
        for (int x = 0; x < outWidth; ++x)
            for (int z = 0; z < outHeight; ++z)
                output[size_t(z) * outWidth + x] =
                    std::clamp((safe(x - 1, z) + safe(x + 1, z) + safe(x, z - 1) + safe(x, z + 1)) * 0.25F,
                               0.f, 1.f);
    return publish(target, std::move(output));
}

Result<int> generateTerrainVelocityFlowMap(Heightmap& target, const Heightmap& source, int iterations) {
    using namespace raster_detail;
    if (!validRaster(target) || !validRaster(source) || target.getWidth() != source.getWidth() ||
        target.getHeight() != source.getHeight() || iterations < 0)
        return invalid("terrain.velocityFlow: matching finite rasters and nonnegative iterations required");
    constexpr float time = 0.2F;
    constexpr int left = 0, right = 1, bottom = 2, top = 3;
    const int width = source.getWidth(), height = source.getHeight();
    const auto terrain = source.data();
    std::vector<float> water(terrain.size(), 0.0001F);
    std::vector<std::array<float, 4>> flow(terrain.size());
    auto index = [width](int x, int y) { return size_t(y) * width + x; };
    for (int iteration = 0; iteration < iterations; ++iteration) {
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
                const int xm = std::max(x - 1, 0), xp = std::min(x + 1, width - 1);
                const int ym = std::max(y - 1, 0), yp = std::min(y + 1, height - 1);
                const size_t i = index(x, y);
                const std::array<size_t, 4> neighbor = {index(xm, y), index(xp, y), index(x, ym), index(x, yp)};
                std::array<float, 4> next{};
                double sum = 0;
                for (int direction = 0; direction < 4; ++direction) {
                    const double difference = double(water[i]) + terrain[i] - water[neighbor[direction]] -
                                              terrain[neighbor[direction]];
                    const double value = std::max(0.0, double(flow[i][direction]) + difference);
                    if (!isRepresentable(value)) return invalid("terrain.velocityFlow: outflow exceeds float range");
                    next[direction] = float(value); sum += value;
                }
                if (sum > 0) {
                    const float scale = std::clamp(float(double(water[i]) / (sum * time)), 0.F, 1.F);
                    for (int direction = 0; direction < 4; ++direction) flow[i][direction] = next[direction] * scale;
                } else {
                    flow[i] = {};
                }
            }
        auto nextWater = water;
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
                const size_t i = index(x, y);
                const double outgoing = double(flow[i][left]) + flow[i][right] + flow[i][bottom] + flow[i][top];
                double incoming = 0;
                if (x > 0) incoming += flow[index(x - 1, y)][right];
                if (x + 1 < width) incoming += flow[index(x + 1, y)][left];
                if (y > 0) incoming += flow[index(x, y - 1)][top];
                if (y + 1 < height) incoming += flow[index(x, y + 1)][bottom];
                const double value = std::max(0.0, double(water[i]) + (incoming - outgoing) * time);
                if (!isRepresentable(value)) return invalid("terrain.velocityFlow: water value exceeds float range");
                nextWater[i] = float(value);
            }
        water.swap(nextWater);
    }
    std::vector<float> output(terrain.size());
    float minimum = std::numeric_limits<float>::infinity();
    float maximum = -std::numeric_limits<float>::infinity();
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const size_t i = index(x, y);
            const float dl = x == 0 ? 0.F : flow[index(x - 1, y)][right] - flow[i][left];
            const float dr = x + 1 == width ? 0.F : flow[i][right] - flow[index(x + 1, y)][left];
            const float dt = y + 1 == height ? 0.F : flow[index(x, y + 1)][bottom] - flow[i][top];
            const float db = y == 0 ? 0.F : flow[i][bottom] - flow[index(x, y - 1)][top];
            const double vx = (double(dl) + dr) * 0.5, vy = (double(db) + dt) * 0.5;
            const double magnitude = std::sqrt(vx * vx + vy * vy);
            if (!isRepresentable(magnitude)) return invalid("terrain.velocityFlow: velocity exceeds float range");
            output[i] = float(magnitude); minimum = std::min(minimum, output[i]); maximum = std::max(maximum, output[i]);
        }
    const float range = maximum - minimum;
    if (range < 1e-12F) std::fill(output.begin(), output.end(), 0.F);
    else for (float& value : output) value = (value - minimum) / range;
    return publish(target, std::move(output));
}

struct TerrainWaterField::Impl {
    int                             width = 0, height = 0;
    std::vector<TerrainWaterSample> cells;
};
TerrainWaterField::TerrainWaterField()                                        = default;
TerrainWaterField::~TerrainWaterField()                                       = default;
TerrainWaterField::TerrainWaterField(TerrainWaterField&&) noexcept            = default;
TerrainWaterField& TerrainWaterField::operator=(TerrainWaterField&&) noexcept = default;
int                TerrainWaterField::getWidth() const noexcept { return impl_ ? impl_->width : 0; }
int                TerrainWaterField::getHeight() const noexcept { return impl_ ? impl_->height : 0; }

Result<int> TerrainWaterField::reset(int width, int height, float depth) {
    using namespace raster_detail;
    if (width <= 0 || height <= 0 || width > std::numeric_limits<int>::max() / height || !std::isfinite(depth) ||
        depth < 0)
        return invalid(
            "terrain.water.reset: positive dimensions with int-sized cell count and finite nonnegative depth required");
    auto candidate    = std::make_unique<Impl>();
    candidate->width  = width;
    candidate->height = height;
    candidate->cells.resize(size_t(width) * height);
    for (auto& cell : candidate->cells) cell.depth = depth;
    impl_.swap(candidate);
    return Result<int>::success(width * height);
}

Result<TerrainWaterSample> TerrainWaterField::sample(int x, int z) const {
    if (!impl_ || x < 0 || z < 0 || x >= impl_->width || z >= impl_->height)
        return Result<TerrainWaterSample>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "terrain.water.sample: coordinate outside initialized field"));
    return Result<TerrainWaterSample>::success(impl_->cells[size_t(z) * impl_->width + x]);
}

Result<int> TerrainWaterField::exportChannel(Heightmap& target, TerrainWaterChannel channel) const {
    using namespace raster_detail;
    if (!impl_ || !validRaster(target) || target.getWidth() != impl_->width || target.getHeight() != impl_->height)
        return invalid("terrain.water.exportChannel: matching finite target and initialized field required");
    float TerrainWaterSample::*member = nullptr;
    switch (channel) {
        case TerrainWaterChannel::Depth: member = &TerrainWaterSample::depth; break;
        case TerrainWaterChannel::VelocityX: member = &TerrainWaterSample::velocityX; break;
        case TerrainWaterChannel::VelocityZ: member = &TerrainWaterSample::velocityZ; break;
        case TerrainWaterChannel::FluxRight: member = &TerrainWaterSample::fluxRight; break;
        case TerrainWaterChannel::FluxLeft: member = &TerrainWaterSample::fluxLeft; break;
        case TerrainWaterChannel::FluxBottom: member = &TerrainWaterSample::fluxBottom; break;
        case TerrainWaterChannel::FluxTop: member = &TerrainWaterSample::fluxTop; break;
        default: return invalid("terrain.water.exportChannel: unknown channel");
    }
    std::vector<float> output(impl_->cells.size());
    for (size_t i = 0; i < output.size(); ++i) output[i] = impl_->cells[i].*member;
    return publish(target, std::move(output));
}

Result<int> TerrainWaterField::advance(const Heightmap& terrain, const TerrainWaterSettings& s) {
    using namespace raster_detail;
    auto positive    = [](float v) { return std::isfinite(v) && v > 0; };
    auto nonnegative = [](float v) { return std::isfinite(v) && v >= 0; };
    if (!impl_ || !validRaster(terrain) || terrain.getWidth() != impl_->width || terrain.getHeight() != impl_->height ||
        !positive(s.spacingX) || !positive(s.spacingZ) || !positive(s.heightScale) || !positive(s.waterScale) ||
        !nonnegative(s.dt) || !nonnegative(s.precipitation) || !nonnegative(s.evaporation) ||
        !std::isfinite(s.flowAcceleration))
        return invalid("terrain.water.advance: matching finite terrain and finite simulation controls required");
    if (s.dt == 0) return Result<int>::success(0);
    const int    width = impl_->width, height = impl_->height;
    const double area        = double(s.spacingX) * s.spacingZ;
    const double capacitance = double(s.dt) * s.flowAcceleration / area;
    auto         next        = impl_->cells;
    auto         neighbors   = [width, height](int x, int z) {
        return std::array<size_t, 4>{
            size_t(z) * width + std::min(x + 1, width - 1), size_t(z) * width + std::max(x - 1, 0),
            size_t(std::min(z + 1, height - 1)) * width + x, size_t(std::max(z - 1, 0)) * width + x};
    };
    // Complete the entire flux field before any neighbor reads from it.
    for (int z = 0; z < height; ++z)
        for (int x = 0; x < width; ++x) {
            const size_t               index   = size_t(z) * width + x;
            const auto                 n       = neighbors(x, z);
            const auto&                old     = impl_->cells[index];
            const double               surface = double(terrain.data()[index]) + old.depth;
            const std::array<float, 4> prior   = {old.fluxRight, old.fluxLeft, old.fluxBottom, old.fluxTop};
            std::array<float, 4>       flux;
            for (int direction = 0; direction < 4; ++direction) {
                const double delta =
                    double(s.heightScale) *
                    (surface - (double(terrain.data()[n[direction]]) + impl_->cells[n[direction]].depth));
                const double value = std::max(0.0, prior[direction] + delta * capacitance);
                if (!isRepresentable(value)) return invalid("terrain.water.advance: flux exceeds finite float range");
                flux[direction] = float(value);
            }
            next[index].fluxRight  = flux[0];
            next[index].fluxLeft   = flux[1];
            next[index].fluxBottom = flux[2];
            next[index].fluxTop    = flux[3];
        }
    int changed = 0;
    for (int z = 0; z < height; ++z)
        for (int x = 0; x < width; ++x) {
            const size_t index       = size_t(z) * width + x;
            const auto   n           = neighbors(x, z);
            const auto&  right       = next[n[0]];
            const auto&  left        = next[n[1]];
            const auto&  bottom      = next[n[2]];
            const auto&  top         = next[n[3]];
            const auto&  old         = impl_->cells[index];
            auto&        cell        = next[index];
            const double inFlow      = double(left.fluxRight) + right.fluxLeft + bottom.fluxTop + top.fluxBottom;
            const double outFlow     = double(cell.fluxLeft) + cell.fluxRight + cell.fluxTop + cell.fluxBottom;
            const double waterOld    = double(old.depth) / s.waterScale;
            const double transported = waterOld + double(s.dt) * (inFlow - outFlow) / area;
            const double average     = 0.5 * (waterOld + transported);
            const double vx          = average == 0
                                           ? 0
                                           : 0.5 * (double(left.fluxLeft) - cell.fluxRight - right.fluxRight + cell.fluxLeft) /
                                        s.spacingX / average;
            const double vz          = average == 0
                                           ? 0
                                           : 0.5 * (double(top.fluxBottom) - cell.fluxTop - bottom.fluxTop + cell.fluxBottom) /
                                        s.spacingZ / average;
            // Retain the final assignment in SimulateWaterFlow, which overwrites transported water.
            const double depth = double(s.waterScale) *
                                 std::max(waterOld + double(s.dt) * (double(s.precipitation) - s.evaporation), 0.0);
            if (!isRepresentable(vx) || !isRepresentable(vz) || !isRepresentable(depth))
                return invalid("terrain.water.advance: velocity or depth exceeds finite float range");
            cell.velocityX = float(vx);
            cell.velocityZ = float(vz);
            cell.depth     = float(depth);
            changed += cell.depth != old.depth || cell.velocityX != old.velocityX || cell.velocityZ != old.velocityZ ||
                       cell.fluxRight != old.fluxRight || cell.fluxLeft != old.fluxLeft ||
                       cell.fluxBottom != old.fluxBottom || cell.fluxTop != old.fluxTop;
        }
    impl_->cells.swap(next);
    return Result<int>::success(changed);
}

Result<int> TerrainWaterField::advanceHydraulic(Heightmap& heights, Heightmap& sediment,
                                                const TerrainWaterSettings&    water,
                                                const TerrainSedimentSettings& reaction,
                                                const TerrainThermalSettings& thermal, int iterations) {
    using namespace raster_detail;
    if (!impl_ || &heights == &sediment || !validRaster(heights) || !validRaster(sediment) ||
        heights.getWidth() != impl_->width || heights.getHeight() != impl_->height ||
        sediment.getWidth() != impl_->width || sediment.getHeight() != impl_->height || iterations < 0 ||
        std::any_of(heights.data().begin(), heights.data().end(), [](float h) { return h < 0; }))
        return invalid(
            "terrain.hydraulic: initialized field, distinct matching rasters, nonnegative heights/iterations required");
    if (iterations == 0) return Result<int>::success(0);
    TerrainWaterField candidate;
    candidate.impl_      = std::make_unique<Impl>(*impl_);
    Heightmap nextHeight = heights, nextSediment = sediment;
    Heightmap vx(impl_->width, impl_->height), vz(impl_->width, impl_->height);
    for (int iteration = 0; iteration < iterations; ++iteration) {
        auto flowed = candidate.advance(nextHeight, water);
        if (!flowed.ok()) return flowed;
        for (size_t i = 0; i < candidate.impl_->cells.size(); ++i) {
            vx.data()[i] = candidate.impl_->cells[i].velocityX;
            vz.data()[i] = candidate.impl_->cells[i].velocityZ;
        }
        auto reacted = applyTerrainSediment(nextHeight, nextSediment, vx, vz, water, reaction);
        if (!reacted.ok()) return reacted;
        auto settled = applyTerrainThermal(nextHeight, nextSediment, thermal);
        if (!settled.ok()) return settled;
    }
    int changed = 0;
    for (size_t i = 0; i < impl_->cells.size(); ++i) {
        const auto& before = impl_->cells[i];
        const auto& after  = candidate.impl_->cells[i];
        changed += heights.data()[i] != nextHeight.data()[i] || sediment.data()[i] != nextSediment.data()[i] ||
                   before.depth != after.depth || before.velocityX != after.velocityX ||
                   before.velocityZ != after.velocityZ || before.fluxRight != after.fluxRight ||
                   before.fluxLeft != after.fluxLeft || before.fluxBottom != after.fluxBottom ||
                   before.fluxTop != after.fluxTop;
    }
    heights.data().swap(nextHeight.data());
    sediment.data().swap(nextSediment.data());
    impl_.swap(candidate.impl_);
    return Result<int>::success(changed);
}
}  // namespace eve::procgen
