#include "procgen/heightmap/TerrainPipeline.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <queue>
#include <utility>

namespace eve::procgen {
namespace {
constexpr std::array<int, 8> dx{-1, 0, 1, -1, 1, -1, 0, 1};
constexpr std::array<int, 8> dy{-1, -1, -1, 0, 0, 1, 1, 1};
constexpr std::array<float, 8> distance{1.41421356f, 1.f, 1.41421356f, 1.f, 1.f, 1.41421356f, 1.f, 1.41421356f};
size_t at(int x, int y, int width) { return size_t(y) * size_t(width) + size_t(x); }
float saturate(float value) { return std::clamp(value, 0.f, 1.f); }
float routeHash(int x, int y) {
    uint32_t n = uint32_t(x) * 1597334677u ^ uint32_t(y) * 3812015801u;
    n = (n ^ (n >> 15u)) * 2246822519u;
    return float(n & 0x00ffffffu) / float(0x00ffffffu);
}
float routeNoise(float x, float y) {
    const int ix = int(std::floor(x)), iy = int(std::floor(y));
    float fx = x - float(ix), fy = y - float(iy);
    fx = fx * fx * (3.f - 2.f * fx);
    fy = fy * fy * (3.f - 2.f * fy);
    const float a = routeHash(ix, iy), b = routeHash(ix + 1, iy);
    const float c = routeHash(ix, iy + 1), d = routeHash(ix + 1, iy + 1);
    return std::lerp(std::lerp(a, b, fx), std::lerp(c, d, fx), fy);
}

std::vector<uint8_t> protectedLakeBasins(const HydrologyMap &hydro, int w, int h,
                                         float maxBreachDepth) {
    const size_t count = size_t(w) * size_t(h);
    std::vector<uint8_t> protectedCells(count, 0), visited(count, 0);
    std::vector<size_t> component;
    std::queue<size_t> frontier;
    for (size_t seed = 0; seed < count; ++seed) {
        if (visited[seed] || hydro.lakeDepth[seed] <= 0.f) continue;
        visited[seed] = 1;
        frontier.push(seed);
        component.clear();
        float basinDepth = 0.f;
        while (!frontier.empty()) {
            const size_t i = frontier.front(); frontier.pop();
            component.push_back(i);
            basinDepth = std::max(basinDepth, hydro.lakeDepth[i]);
            const int x = int(i % size_t(w)), y = int(i / size_t(w));
            for (int d = 0; d < 8; ++d) {
                const int nx = x + dx[d], ny = y + dy[d];
                if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                const size_t n = at(nx, ny, w);
                if (!visited[n] && hydro.lakeDepth[n] > 0.f) {
                    visited[n] = 1;
                    frontier.push(n);
                }
            }
        }
        if (basinDepth > std::max(0.f, maxBreachDepth))
            for (size_t i : component) protectedCells[i] = 1;
    }
    return protectedCells;
}
}  // namespace

TerrainLayers::TerrainLayers(HydrologyMap hydrology, ClimateMap climate)
    : hydrology_(std::move(hydrology)), climate_(std::move(climate)) {}

namespace {
float erosionSample(const std::vector<float> &values, int width, int height, int x, int y) {
    if (x < 0 || y < 0 || x >= width || y >= height) return 0.f;
    const size_t i = size_t(y) * size_t(width) + size_t(x);
    return i < values.size() ? values[i] : 0.f;
}
}  // namespace

float TerrainErosionMap::getWear(int x, int y) const {
    return erosionSample(wear, width, height, x, y);
}
float TerrainErosionMap::getDeposition(int x, int y) const {
    return erosionSample(deposition, width, height, x, y);
}
float TerrainErosionMap::getHeightDelta(int x, int y) const {
    return erosionSample(heightDelta, width, height, x, y);
}

int TerrainLayers::getWidth() const { return hydrology_.width; }
int TerrainLayers::getHeight() const { return hydrology_.height; }

size_t TerrainLayers::index(int x, int y) const {
    if (x < 0 || y < 0 || x >= getWidth() || y >= getHeight()) return size_t(-1);
    return size_t(y) * size_t(getWidth()) + size_t(x);
}

float TerrainLayers::getFlowAccumulation(int x, int y) const {
    const size_t i = index(x, y); return i < hydrology_.flowAccumulation.size() ? hydrology_.flowAccumulation[i] : 0.f;
}
int TerrainLayers::getFlowDirection(int x, int y) const {
    const size_t i = index(x, y);
    return i < hydrology_.flowDirection.size() ? int(hydrology_.flowDirection[i]) : -1;
}
float TerrainLayers::getFlowVectorX(int x, int y) const {
    const size_t i = index(x, y);
    if (i < hydrology_.flowVectorX.size()) return hydrology_.flowVectorX[i];
    const int d = getFlowDirection(x, y);
    return d >= 0 && d < 8 ? float(dx[d]) / distance[d] : 0.f;
}
float TerrainLayers::getFlowVectorY(int x, int y) const {
    const size_t i = index(x, y);
    if (i < hydrology_.flowVectorY.size()) return hydrology_.flowVectorY[i];
    const int d = getFlowDirection(x, y);
    return d >= 0 && d < 8 ? float(dy[d]) / distance[d] : 0.f;
}
bool TerrainLayers::isRiver(int x, int y) const {
    const size_t i = index(x, y); return i < hydrology_.rivers.size() && hydrology_.rivers[i] != 0;
}
int TerrainLayers::getStreamOrder(int x, int y) const {
    const size_t i = index(x, y);
    return i < hydrology_.streamOrder.size() ? int(hydrology_.streamOrder[i]) : 0;
}
float TerrainLayers::getLakeDepth(int x, int y) const {
    const size_t i = index(x, y);
    return i < hydrology_.lakeDepth.size() ? hydrology_.lakeDepth[i] : 0.f;
}
bool TerrainLayers::isLake(int x, int y, float minimumDepth) const {
    return getLakeDepth(x, y) >= std::max(0.f, minimumDepth);
}
float TerrainLayers::getTemperature(int x, int y) const {
    const size_t i = index(x, y); return i < climate_.temperature.size() ? climate_.temperature[i] : 0.f;
}
float TerrainLayers::getMoisture(int x, int y) const {
    const size_t i = index(x, y); return i < climate_.moisture.size() ? climate_.moisture[i] : 0.f;
}
int TerrainLayers::getBiome(int x, int y) const {
    const size_t i = index(x, y); return i < climate_.biomes.size() ? int(climate_.biomes[i]) : -1;
}
std::string TerrainLayers::getBiomeName(int x, int y) const {
    static constexpr const char *names[] = {"ocean", "beach", "desert", "grassland", "forest",
                                             "rainforest", "tundra", "taiga", "alpine", "river",
                                             "lake", "wetland"};
    const int biome = getBiome(x, y);
    return biome >= 0 && biome < int(sizeof(names) / sizeof(names[0])) ? names[biome] : std::string{};
}

void TerrainPipeline::erodeThermal(Heightmap &hm, const ThermalErosionSettings &s) {
    const int w = hm.getWidth(), h = hm.getHeight();
    if (w < 2 || h < 2 || s.iterations <= 0 || s.strength <= 0.f) return;
    auto &height = hm.data();
    std::vector<float> delta(height.size());
    for (int iteration = 0; iteration < s.iterations; ++iteration) {
        std::fill(delta.begin(), delta.end(), 0.f);
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
            const size_t from = at(x, y, w);
            float total = 0.f;
            std::array<float, 8> excess{};
            for (int d = 0; d < 8; ++d) {
                const int nx = x + dx[d], ny = y + dy[d];
                if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                excess[d] = std::max(0.f, height[from] - height[at(nx, ny, w)] - s.talus * distance[d]);
                total += excess[d];
            }
            if (total <= 0.f) continue;
            // Multiple downhill neighbours must not each spend the same height
            // difference.  Cap the aggregate transfer below half of the steepest
            // excess so one Jacobi step cannot invert a slope and amplify it on
            // the following iteration.
            const float maxExcess = *std::max_element(excess.begin(), excess.end());
            const float moved = std::min(total * std::clamp(s.strength, 0.f, 0.5f),
                                         maxExcess * 0.5f);
            delta[from] -= moved;
            for (int d = 0; d < 8; ++d)
                if (excess[d] > 0.f) delta[at(x + dx[d], y + dy[d], w)] += moved * excess[d] / total;
        }
        for (size_t i = 0; i < height.size(); ++i) height[i] += delta[i];
    }
}

void TerrainPipeline::erodeHydraulic(Heightmap &hm, const HydraulicErosionSettings &s) {
    const int w = hm.getWidth(), h = hm.getHeight();
    if (w < 2 || h < 2 || s.iterations <= 0) return;
    auto &terrain = hm.data();
    std::vector<float> water(terrain.size()), sediment(terrain.size()), nextWater, nextSediment;
    for (int iteration = 0; iteration < s.iterations; ++iteration) {
        for (float &v : water) v += std::max(0.f, s.rainfall);
        nextWater.assign(terrain.size(), 0.f); nextSediment.assign(terrain.size(), 0.f);
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
            const size_t i = at(x, y, w);
            int best = -1; float bestDrop = 0.f;
            for (int d = 0; d < 8; ++d) {
                const int nx = x + dx[d], ny = y + dy[d];
                if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                const size_t n = at(nx, ny, w);
                const float drop = (terrain[i] + water[i] - terrain[n] - water[n]) / distance[d];
                if (drop > bestDrop) { bestDrop = drop; best = int(n); }
            }
            const float capacity = bestDrop * water[i] * std::max(0.f, s.capacity);
            if (sediment[i] < capacity) {
                const float amount = std::min(std::max(0.f, terrain[i]), (capacity - sediment[i]) * std::max(0.f, s.erosion));
                terrain[i] -= amount; sediment[i] += amount;
            } else {
                const float amount = (sediment[i] - capacity) * std::clamp(s.deposition, 0.f, 1.f);
                terrain[i] += amount; sediment[i] -= amount;
            }
            const float outflow = best >= 0 ? water[i] * 0.5f : 0.f;
            const float ratio = water[i] > 0.f ? outflow / water[i] : 0.f;
            nextWater[i] += water[i] - outflow; nextSediment[i] += sediment[i] * (1.f - ratio);
            if (best >= 0) { nextWater[size_t(best)] += outflow; nextSediment[size_t(best)] += sediment[i] * ratio; }
        }
        const float keep = 1.f - std::clamp(s.evaporation, 0.f, 1.f);
        for (float &v : nextWater) v *= keep;
        water.swap(nextWater); sediment.swap(nextSediment);
    }
    // Sediment still suspended when the simulation ends represents material
    // exported through the open drainage boundary. Dumping it into its final
    // cells creates needle-like sink deposits and is numerically unstable.
}

void TerrainPipeline::erodeFluvial(Heightmap &hm, const FluvialErosionSettings &s) {
    (void)erodeFluvialDetailed(hm, s);
}

TerrainErosionMap TerrainPipeline::erodeFluvialDetailed(
    Heightmap &hm, const FluvialErosionSettings &s) {
    TerrainErosionMap diagnostics;
    const int w = hm.getWidth(), h = hm.getHeight();
    if (w < 3 || h < 3 || s.iterations <= 0 || s.incision <= 0.f ||
        s.maxDepth <= 0.f || s.bankWidth < 0.f || !std::isfinite(s.coordinateScale) ||
        s.coordinateScale <= 0.f) return diagnostics;
    auto &terrain = hm.data();
    const std::vector<float> original = terrain;
    const size_t count = terrain.size();
    diagnostics.width = w; diagnostics.height = h;
    diagnostics.wear.assign(count, 0.f);
    diagnostics.deposition.assign(count, 0.f);
    diagnostics.heightDelta.assign(count, 0.f);
    std::vector<float> next(count), relaxed(count);
    std::vector<float> valleyTarget(count), valleyWeight(count);
    std::vector<float> floodplainTarget(count), floodplainWeight(count);
    std::vector<int> channelDistance(count);
    std::vector<size_t> order(count);
    for (int iteration = 0; iteration < s.iterations; ++iteration) {
        const HydrologyMap hydro = buildHydrology(hm, s.riverThreshold,
                                                   -std::numeric_limits<float>::infinity(),
                                                   s.coordinateScale, false);
        const float cutoff = s.riverThreshold <= 1.f
                                 ? std::max(2.f, s.riverThreshold * float(terrain.size()))
                                 : s.riverThreshold;
        std::iota(order.begin(), order.end(), size_t(0));
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            return hydro.flowAccumulation[a] > hydro.flowAccumulation[b];
        });

        // Channel heads must be allowed to form before they satisfy the final
        // mapped-river cutoff.  Using the display cutoff as the erosion cutoff
        // leaves broad convex slopes forever smooth: there is no initial groove
        // to capture adjacent runoff.  A smaller initiation area, combined with
        // coherent erodibility, seeds a few persistent rills. Recomputing the
        // drainage field each iteration then lets successful rills capture flow
        // and grow into a dendritic network while the others die out.
        // Channel-initiation area scales with the requested drainage density.
        // sqrt(cutoff) is much too permissive on medium/large maps and lets
        // nearly every raster column become a permanent rill on smooth slopes.
        const float initiationCutoff = std::max(4.f, cutoff * 0.30f);
        const float wideningCutoff = std::lerp(initiationCutoff, cutoff, 0.42f);
        // Priority-Flood supplies a receiver graph through both shallow sills
        // and genuinely endorheic basins.  Only depressions that can plausibly
        // be opened within this erosion job's depth budget may become channels.
        // Otherwise the virtual routing tree would be excavated into radial
        // spokes across a lake floor.
        const std::vector<uint8_t> protectedLake =
            protectedLakeBasins(hydro, w, h, s.maxBreachDepth);

        // Breach spill sills on the depression-free receiver graph. Priority
        // Flood can route across a closed basin, but the corresponding receiver
        // may still be uphill on the real surface. Merely skipping that link
        // strands channels in the basin. Propagating a shallow descending grade
        // through the sill opens a physical outlet, bounded by maxDepth so a
        // deep endorheic basin is not flattened wholesale.
        next = terrain;
        const float breachGrade = 0.00035f / std::max(0.001f, s.coordinateScale);
        for (size_t i : order) {
            const int d = hydro.flowDirection[i];
            if (d < 0 || hydro.flowAccumulation[i] < initiationCutoff) continue;
            if (protectedLake[i]) continue;
            const int x = int(i % size_t(w)), y = int(i / size_t(w));
            const size_t receiver = at(x + dx[d], y + dy[d], w);
            const float outletTarget = next[i] - breachGrade * distance[d];
            if (next[receiver] > outletTarget) {
                const int rx = x + dx[d], ry = y + dy[d];
                const float boundaryDistance = float(std::min({rx, ry, w - 1 - rx, h - 1 - ry}));
                const float boundaryT = saturate(boundaryDistance /
                    std::max(1.f, s.bankWidth + 1.f));
                const float boundaryFade = boundaryT * boundaryT * (3.f - 2.f * boundaryT);
                const float target = std::max(original[receiver] - s.maxDepth, outletTarget);
                next[receiver] += (target - next[receiver]) * boundaryFade;
            }
        }

        // Braun-Willett implicit update for n=1. Receivers have no smaller
        // contributing area, so this order establishes their new height first.
        for (size_t i : order) {
            const int d = hydro.flowDirection[i];
            if (d < 0 || hydro.flowAccumulation[i] < initiationCutoff) continue;
            if (protectedLake[i]) continue;
            const int x = int(i % size_t(w)), y = int(i / size_t(w));
            const size_t receiver = at(x + dx[d], y + dy[d], w);
            if (next[receiver] >= terrain[i]) continue;
            const float normalizedArea = hydro.flowAccumulation[i] / cutoff;
            const float maturity = saturate((hydro.flowAccumulation[i] - initiationCutoff) /
                                            std::max(1.f, cutoff - initiationCutoff));
            const float erodibility = 0.72f + 0.56f *
                routeNoise(float(x) * 0.085f + 31.7f, float(y) * 0.085f - 19.3f);
            const float boundaryDistance = float(std::min({x, y, w - 1 - x, h - 1 - y}));
            const float boundaryT = saturate(boundaryDistance /
                std::max(1.f, s.bankWidth + 1.f));
            const float boundaryFade = boundaryT * boundaryT * (3.f - 2.f * boundaryT);
            const float c = s.incision * erodibility * boundaryFade *
                std::pow(std::max(0.04f, normalizedArea), 0.45f) / distance[d];
            const float implicitHeight = (terrain[i] + c * next[receiver]) / (1.f + c);
            // A small detachment term supplies the positive feedback missing
            // from pure slope relaxation. It is weak at a newly initiated head
            // and approaches the configured incision rate only in mature flow.
            // `incision` is a per-pass terrain-height scale.  The old
            // 0.00045--0.0018 multiplier made even an aggressively configured
            // gallery river cut less than one screen pixel after many passes:
            // hydrology was visible only because a water ribbon was drawn on
            // top of an essentially unchanged slope.  Mature channels need a
            // meaningful detachment term so their beds establish a longitudinal
            // profile and the bank pass below has a real valley floor to widen.
            // Channel heads remain deliberately weak to avoid turning every
            // drainage pixel into a deep raster trench.
            const float detachment = s.incision * erodibility * boundaryFade *
                (0.0030f + 0.0090f * maturity) *
                std::pow(std::max(0.04f, normalizedArea), 0.30f);
            next[i] = std::max(original[i] - s.maxDepth,
                               std::min(terrain[i], implicitHeight - detachment));
        }

        // Limited hillslope diffusion near persistent channels turns the
        // one-cell bed into a V-shaped valley without excavating circular pits.
        const int radius = std::max(0, int(std::ceil(s.bankWidth)));
        std::fill(channelDistance.begin(), channelDistance.end(), radius + 1);
        std::queue<size_t> queue;
        for (size_t i = 0; i < count; ++i)
            if (hydro.flowAccumulation[i] >= wideningCutoff &&
                !protectedLake[i]) {
            channelDistance[i] = 0; queue.push(i);
        }
        constexpr std::array<int, 4> cardinal{1, 3, 4, 6};
        while (!queue.empty()) {
            const size_t i = queue.front(); queue.pop();
            if (channelDistance[i] >= radius) continue;
            const int x = int(i % size_t(w)), y = int(i / size_t(w));
            for (int d : cardinal) {
                const int nx = x + dx[d], ny = y + dy[d];
                if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                const size_t n = at(nx, ny, w);
                if (channelDistance[n] > channelDistance[i] + 1) {
                    channelDistance[n] = channelDistance[i] + 1; queue.push(n);
                }
            }
        }
        relaxed = next;
        for (int y = 1; y + 1 < h; ++y) for (int x = 1; x + 1 < w; ++x) {
            const size_t i = at(x, y, w);
            if (channelDistance[i] == 0 || channelDistance[i] > radius) continue;
            const float average = (next[at(x - 1, y, w)] + next[at(x + 1, y, w)] +
                                   next[at(x, y - 1, w)] + next[at(x, y + 1, w)]) * 0.25f;
            const float alpha = 0.12f * (1.f - float(channelDistance[i]) / float(radius + 1));
            relaxed[i] = std::clamp(next[i] + (average - next[i]) * alpha,
                                    original[i] - s.maxDepth, original[i]);
        }
        terrain.swap(relaxed);
    }

    // Widen the final, stable drainage network once. Reapplying this operation
    // inside the incision loop over-deepens banks and turns valleys into trenches.
    const HydrologyMap hydro = buildHydrology(hm, s.riverThreshold,
                                               -std::numeric_limits<float>::infinity(),
                                               s.coordinateScale, false);
    const float cutoff = s.riverThreshold <= 1.f
                             ? std::max(2.f, s.riverThreshold * float(count))
                             : s.riverThreshold;
    const std::vector<uint8_t> protectedLake =
        protectedLakeBasins(hydro, w, h, s.maxBreachDepth);
    std::fill(valleyTarget.begin(), valleyTarget.end(),
              std::numeric_limits<float>::infinity());
    std::fill(valleyWeight.begin(), valleyWeight.end(), 0.f);
    std::fill(floodplainTarget.begin(), floodplainTarget.end(), 0.f);
    std::fill(floodplainWeight.begin(), floodplainWeight.end(), 0.f);
    for (size_t i = 0; i < count; ++i) {
        const float flow = hydro.flowAccumulation[i];
        const int d = hydro.flowDirection[i];
        if (flow < cutoff || d < 0 || protectedLake[i]) continue;
        const int x = int(i % size_t(w)), y = int(i / size_t(w));
        const size_t receiver = at(x + dx[d], y + dy[d], w);
        // Estimate a reach tangent from the strongest upstream donor through
        // this cell to its receiver.  Expanding circular stamps around D8
        // pixels produces a chain of pits and swollen confluences; a local
        // tangent lets the valley grow across the channel while adjacent
        // stamps overlap smoothly along it.
        int donorX = x, donorY = y;
        float donorHeight = terrain[i];
        float donorFlow = -1.f;
        for (int upstreamDirection = 0; upstreamDirection < 8; ++upstreamDirection) {
            const int ux = x + dx[upstreamDirection], uy = y + dy[upstreamDirection];
            if (ux < 0 || uy < 0 || ux >= w || uy >= h) continue;
            const size_t upstream = at(ux, uy, w);
            const int upstreamReceiver = hydro.flowDirection[upstream];
            if (upstreamReceiver < 0 ||
                ux + dx[upstreamReceiver] != x || uy + dy[upstreamReceiver] != y)
                continue;
            if (hydro.flowAccumulation[upstream] > donorFlow) {
                donorFlow = hydro.flowAccumulation[upstream];
                donorX = ux; donorY = uy; donorHeight = terrain[upstream];
            }
        }
        // Use the continuous MFD gradient for the corridor frame. The D8 donor
        // and receiver remain the authoritative longitudinal graph, but their
        // eight discrete headings leave a chain of overlapping oval pits when
        // they are also used as the valley-carving orientation.
        float tangentX = hydro.flowVectorX[i];
        float tangentY = hydro.flowVectorY[i];
        float tangentLength = std::sqrt(tangentX * tangentX + tangentY * tangentY);
        if (tangentLength < 0.001f) {
            tangentX = float(x + dx[d] - donorX);
            tangentY = float(y + dy[d] - donorY);
            tangentLength = std::hypot(tangentX, tangentY);
        }
        if (tangentLength < 0.001f) {
            tangentX = float(dx[d]); tangentY = float(dy[d]); tangentLength = distance[d];
        }
        tangentX /= tangentLength; tangentY /= tangentLength;
        const float normalX = -tangentY, normalY = tangentX;
        const float areaRatio = std::max(1.f, flow / cutoff);
        // Hydraulic geometry is controlled by contributing area, not by the
        // largest river present in this particular tile.  Normalising every
        // reach against maxFlow made a trunk river on one seed as narrow as a
        // tributary on another.  A bounded Hack-style power law preserves
        // visible stream hierarchy while remaining stable across map sizes.
        const float areaScale = std::min(3.4f, std::pow(areaRatio, 0.32f));
        const float riverMaturity = saturate(std::log2(areaRatio) / 6.f);
        const float bedSlope = std::max(0.f, terrain[i] - terrain[receiver]) / distance[d];
        // Mature low-gradient reaches exchange sediment laterally and produce
        // a recognisable valley floor.  Treating every reach as bedrock incision
        // leaves a narrow V-notch all the way to the outlet.
        const bool alluvial = areaRatio > 3.5f && bedSlope < 0.045f;
        const float width = std::max(0.75f, s.bankWidth *
            (0.18f + 0.50f * areaScale) * (alluvial ? 1.28f : 1.f));
        // A short longitudinal footprint bridges diagonal D8 steps without
        // allowing one reach to excavate a circular area around itself.
        // A longer overlap turns cell stamps into one continuous geomorphic
        // corridor. It is still much shorter than a bend wavelength, so it
        // cannot cut across separate neighbouring reaches.
        const float alongReach = 2.75f;
        const int radius = int(std::ceil(width + alongReach));
        // bankWidth is expressed in raster cells. Normalize the per-cell rise
        // so doubling resolution (and therefore bankWidth) preserves the same
        // approximate world-space cross-section instead of doubling relief.
        const float bankResolutionScale = 3.f / std::max(1.f, s.bankWidth);
        const float bankRise = (alluvial ? 0.0032f
                                         : 0.010f + 0.020f * (1.f - riverMaturity)) *
                               bankResolutionScale;
        // Establish a scale-separated trunk bed after the iterative stream
        // power passes.  This is deliberately tied to maxDepth so it cannot
        // bypass the caller's erosion budget.  Tributaries get only a shallow
        // notch; high-order reaches acquire enough relief for a readable V
        // valley or alluvial floor.
        const float bedOffset = s.maxDepth * (0.025f + 0.19f * riverMaturity);
        for (int oy = -radius; oy <= radius; ++oy) for (int ox = -radius; ox <= radius; ++ox) {
            const int nx = x + ox, ny = y + oy;
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
            const float lateral = std::abs(float(ox) * normalX + float(oy) * normalY);
            const float along = float(ox) * tangentX + float(oy) * tangentY;
            if (lateral > width || std::abs(along) > alongReach) continue;
            const size_t n = at(nx, ny, w);
            // Continue the channel's longitudinal grade through this stamp.
            // Upstream and downstream samples are taken from the actual
            // receiver graph so overlapping stamps agree on bed elevation.
            float bed = terrain[i] - bedOffset;
            if (along >= 0.f)
                bed += along * (terrain[receiver] - terrain[i]) / distance[d];
            else if (donorFlow >= 0.f) {
                const float donorDistance = std::hypot(float(x - donorX), float(y - donorY));
                bed += along * (terrain[i] - donorHeight) /
                       std::max(1.f, donorDistance);
            }
            // Four-part geomorphic cross-section:
            //   bed -> floor -> valley wall -> rounded shoulder.
            // A single linear ramp reads as a raster trench and has no break
            // in slope for lighting to reveal.  Bedrock reaches retain a small
            // channel floor and a concave V-wall; mature reaches get a broad,
            // almost-level alluvial floor and a steeper outer valley wall.
            const float channelHalfWidth = std::max(0.42f,
                width * (0.045f + 0.025f * riverMaturity));
            const float floorHalfWidth = alluvial
                ? width * (0.25f + 0.25f * riverMaturity)
                : channelHalfWidth;
            const float shoulderStart = std::max(floorHalfWidth + 0.01f, width * 0.78f);
            float profileRise = 0.f;
            if (lateral <= channelHalfWidth) {
                profileRise = 0.0007f * lateral;
            } else if (lateral <= floorHalfWidth) {
                profileRise = 0.0007f * channelHalfWidth +
                              0.0012f * (lateral - channelHalfWidth);
            } else if (lateral <= shoulderStart) {
                const float wallT = (lateral - floorHalfWidth) /
                    std::max(0.001f, shoulderStart - floorHalfWidth);
                const float wallShape = std::pow(wallT, alluvial ? 1.32f : 0.78f);
                profileRise = 0.0007f * channelHalfWidth +
                              bankRise * (shoulderStart - floorHalfWidth) * wallShape;
            } else {
                const float shoulderRise = bankRise * (shoulderStart - floorHalfWidth);
                const float shoulderT = (lateral - shoulderStart) /
                    std::max(0.001f, width - shoulderStart);
                // Smoothly flatten the outer wall into the untouched hillslope.
                const float rounded = shoulderT * shoulderT * (3.f - 2.f * shoulderT);
                profileRise = 0.0007f * channelHalfWidth + shoulderRise +
                              bankRise * (width - shoulderStart) * 0.22f * rounded;
            }
            const float target = bed + profileRise;
            const float crossFade = 1.f - lateral / width;
            const float alongFade = 1.f - std::abs(along) / alongReach;
            const float smoothCrossFade = crossFade * crossFade * (3.f - 2.f * crossFade);
            const float domainDistance = float(std::min({nx, ny, w - 1 - nx, h - 1 - ny}));
            const float domainT = saturate(domainDistance /
                std::max(1.f, s.bankWidth + 1.f));
            const float domainFade = domainT * domainT * (3.f - 2.f * domainT);
            // Priority-Flood deliberately drains to the finite map boundary.
            // Widening every one of those numerical outlets stamps a bright
            // rectangular moat into wear maps. A production tiled build uses
            // a halo and crops it; this fade provides the equivalent behaviour
            // for standalone heightfields while leaving interior valleys intact.
            const float edgeFade = smoothCrossFade * (0.68f + 0.32f * alongFade) *
                                   domainFade;
            if (target < valleyTarget[n]) valleyTarget[n] = target;
            valleyWeight[n] = std::max(valleyWeight[n], edgeFade);
            if (alluvial && lateral <= floorHalfWidth) {
                const float floorFade = 1.f - lateral / std::max(0.001f, floorHalfWidth);
                // A weak cross-valley grade avoids an unnaturally perfect
                // tabletop while still filling D8-scale ruts and point pits.
                const float floor = bed + 0.0015f * lateral;
                const float weight = floorFade * floorFade * (0.5f + 0.5f * alongFade) *
                                     domainFade;
                floodplainTarget[n] += floor * weight;
                floodplainWeight[n] += weight;
            }
        }
    }
    for (size_t i = 0; i < count; ++i) if (std::isfinite(valleyTarget[i])) {
        const float target = std::max(original[i] - s.maxDepth, valleyTarget[i]);
        if (target < terrain[i])
            terrain[i] += (target - terrain[i]) * (0.82f * valleyWeight[i]);
    }
    // Deposit exported channel sediment back into mature, low-slope reaches.
    // The pre-fluvial surface is an upper bound, so this cannot inflate hills or
    // violate maxDepth; it only fills local over-incision and establishes a
    // continuous alluvial floor inside the wider carved valley.
    for (size_t i = 0; i < count; ++i) if (floodplainWeight[i] > 0.f) {
        const float desired = std::clamp(floodplainTarget[i] / floodplainWeight[i],
                                         original[i] - s.maxDepth, original[i]);
        const float alpha = 0.34f * saturate(floodplainWeight[i]);
        const float before = terrain[i];
        terrain[i] += (desired - terrain[i]) * alpha;
        diagnostics.deposition[i] += std::max(0.f, terrain[i] - before);
    }

    // Lake-inlet alluvial aprons. A channel entering standing water rapidly
    // loses transport capacity; without this pass the incised V-groove simply
    // terminates at the shoreline. Build a widening, low-gradient fan on the
    // landward side of each high-flow inlet. Deposition may only restore
    // material removed by this erosion job, never raise the original terrain
    // or fill a protected lake basin wholesale.
    std::vector<float> deltaTarget(count, -std::numeric_limits<float>::infinity());
    std::vector<float> deltaWeight(count, 0.f);
    for (size_t i = 0; i < count; ++i) {
        const int d = hydro.flowDirection[i];
        if (d < 0 || protectedLake[i] || hydro.flowAccumulation[i] < cutoff) continue;
        const int x = int(i % size_t(w)), y = int(i / size_t(w));
        const int rx = x + dx[d], ry = y + dy[d];
        if (rx < 0 || ry < 0 || rx >= w || ry >= h) continue;
        const size_t receiver = at(rx, ry, w);
        if (!protectedLake[receiver]) continue;
        const float areaRatio = std::max(1.f, hydro.flowAccumulation[i] / cutoff);
        const float fanLength = s.bankWidth * (0.85f + 0.30f *
            std::min(3.f, std::pow(areaRatio, 0.28f)));
        const float fanWidth = fanLength * 0.72f;
        const int radius = int(std::ceil(std::max(fanLength, fanWidth)));
        const float tangentX = float(dx[d]) / distance[d];
        const float tangentY = float(dy[d]) / distance[d];
        const float normalX = -tangentY, normalY = tangentX;
        const float lakeSurface = terrain[receiver] + hydro.lakeDepth[receiver];
        for (int oy = -radius; oy <= radius; ++oy) for (int ox = -radius; ox <= radius; ++ox) {
            const int nx = x + ox, ny = y + oy;
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
            const size_t n = at(nx, ny, w);
            if (protectedLake[n]) continue;
            const float along = float(ox) * tangentX + float(oy) * tangentY;
            if (along > 0.35f || along < -fanLength) continue;
            const float upstreamT = std::clamp(-along / std::max(0.001f, fanLength), 0.f, 1.f);
            const float localHalfWidth = std::lerp(fanWidth, std::max(0.7f, s.bankWidth * 0.22f),
                                                   upstreamT);
            const float lateral = std::abs(float(ox) * normalX + float(oy) * normalY);
            if (lateral > localHalfWidth) continue;
            const float crossFade = 1.f - lateral / localHalfWidth;
            const float lengthFade = 1.f - upstreamT;
            const float target = lakeSurface + (-along) *
                (0.0018f / std::max(0.001f, s.coordinateScale));
            deltaTarget[n] = std::max(deltaTarget[n], target);
            deltaWeight[n] = std::max(deltaWeight[n], crossFade * crossFade *
                                                      (0.35f + 0.65f * lengthFade));
        }
    }
    for (size_t i = 0; i < count; ++i) if (deltaWeight[i] > 0.f) {
        const float target = std::clamp(deltaTarget[i], terrain[i], original[i]);
        const float before = terrain[i];
        terrain[i] += (target - terrain[i]) * (0.58f * deltaWeight[i]);
        diagnostics.deposition[i] += std::max(0.f, terrain[i] - before);
    }

    // Bank-failure relaxation. Stream-power incision establishes the bed but
    // does not by itself enforce a stable hillslope angle, so a deeply lowered
    // raster cell can leave an implausible near-vertical wall. Move material
    // downslope only around the final valley footprint. This behaves like a
    // compact talus/debris pass without blurring unaffected mountain ridges.
    std::vector<uint8_t> valleyRegion(count, 0);
    for (size_t i = 0; i < count; ++i)
        valleyRegion[i] = uint8_t(valleyWeight[i] > 1e-4f || floodplainWeight[i] > 1e-4f ||
                                  deltaWeight[i] > 0.f);
    std::vector<float> bankDelta(count, 0.f);
    constexpr std::array<int, 4> bankDx{1, 0, 1, -1};
    constexpr std::array<int, 4> bankDy{0, 1, 1, 1};
    constexpr std::array<float, 4> bankDistance{1.f, 1.f, 1.41421356f, 1.41421356f};
    const float stableBankStep = 0.014f / std::max(0.001f, s.coordinateScale);
    for (int pass = 0; pass < 4; ++pass) {
        std::fill(bankDelta.begin(), bankDelta.end(), 0.f);
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
            const size_t a = at(x, y, w);
            for (size_t edge = 0; edge < bankDx.size(); ++edge) {
                const int nx = x + bankDx[edge], ny = y + bankDy[edge];
                if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                const size_t b = at(nx, ny, w);
                if (!valleyRegion[a] && !valleyRegion[b]) continue;
                const float difference = terrain[a] - terrain[b];
                const float excess = std::abs(difference) - stableBankStep * bankDistance[edge];
                if (excess <= 0.f) continue;
                const float moved = excess * 0.11f;
                if (difference > 0.f) { bankDelta[a] -= moved; bankDelta[b] += moved; }
                else { bankDelta[a] += moved; bankDelta[b] -= moved; }
            }
        }
        for (size_t i = 0; i < count; ++i) {
            const float before = terrain[i];
            terrain[i] = std::clamp(terrain[i] + bankDelta[i],
                                    original[i] - s.maxDepth, original[i]);
            diagnostics.deposition[i] += std::max(0.f, terrain[i] - before);
        }
    }
    for (size_t i = 0; i < count; ++i) {
        diagnostics.heightDelta[i] = terrain[i] - original[i];
        // Gross wear equals the remaining net lowering plus material that was
        // subsequently returned to this cell. This makes wear/deposit useful
        // independently while preserving wear - deposit == net lowering.
        diagnostics.wear[i] = std::max(0.f, original[i] - terrain[i]) +
                              diagnostics.deposition[i];
    }
    return diagnostics;
}

HydrologyMap TerrainPipeline::buildHydrology(const Heightmap &hm, float threshold, float seaLevel,
                                             float coordinateScale, bool classifyLakes) {
    HydrologyMap out;
    out.width = hm.getWidth(); out.height = hm.getHeight();
    const int w = out.width, h = out.height; const size_t count = size_t(w) * size_t(h);
    out.flowDirection.assign(count, -1); out.flowAccumulation.assign(count, 1.f);
    out.flowVectorX.assign(count, 0.f); out.flowVectorY.assign(count, 0.f);
    out.lakeDepth.assign(count, 0.f); out.rivers.assign(count, 0);
    out.streamOrder.assign(count, 0);
    if (w <= 0 || h <= 0) return out;
    const auto &z = hm.data();
    // Priority-flood produces a minimally raised routing surface. Every inland
    // cell then has a path to the map edge instead of terminating in a noise pit.
    struct FloodCell { float elevation; size_t index; };
    auto greater = [](const FloodCell &a, const FloodCell &b) {
        return a.elevation > b.elevation || (a.elevation == b.elevation && a.index > b.index);
    };
    std::priority_queue<FloodCell, std::vector<FloodCell>, decltype(greater)> frontier(greater);
    std::vector<float> routed = z;
    std::vector<uint8_t> visited(count, 0);
    auto seed = [&](int x, int y) {
        const size_t i = at(x, y, w);
        if (!visited[i]) { visited[i] = 1; frontier.push({routed[i], i}); }
    };
    for (int x = 0; x < w; ++x) { seed(x, 0); seed(x, h - 1); }
    for (int y = 1; y + 1 < h; ++y) { seed(0, y); seed(w - 1, y); }
    constexpr float epsilon = 1e-6f;
    while (!frontier.empty()) {
        const FloodCell cell = frontier.top(); frontier.pop();
        const int x = int(cell.index % size_t(w)), y = int(cell.index / size_t(w));
        for (int d = 0; d < 8; ++d) {
            const int nx = x + dx[d], ny = y + dy[d];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
            const size_t n = at(nx, ny, w);
            if (visited[n]) continue;
            visited[n] = 1;
            routed[n] = std::max(routed[n], cell.elevation + epsilon);
            for (int back = 0; back < 8; ++back)
                if (nx + dx[back] == x && ny + dy[back] == y) { out.flowDirection[n] = int8_t(back); break; }
            frontier.push({routed[n], n});
        }
    }
    // Preserve the physical meaning of depression filling instead of silently
    // drawing rivers across the dry basin floor. Sub-epsilon depths are routing
    // gradients, not visible water.
    for (size_t i = 0; i < count; ++i) {
        const float depth = routed[i] - z[i];
        out.lakeDepth[i] = depth > 1e-4f ? depth : 0.f;
    }
    // Priority-Flood defines the depression-free routing surface, but its
    // visitation parent is not a physical flow direction: using that tree
    // directly imprints queue-order spokes into smooth mountains. Route each
    // cell to its locally steepest downslope neighbour on the filled surface.
    for (int y = 1; y + 1 < h; ++y) for (int x = 1; x + 1 < w; ++x) {
        const size_t i = at(x, y, w);
        int bestDirection = -1;
        float gradientX = 0.f, gradientY = 0.f, totalGradientWeight = 0.f;
        for (int d = 0; d < 8; ++d) {
            const size_t n = at(x + dx[d], y + dy[d], w);
            const float slope = (routed[i] - routed[n]) / distance[d];
            if (slope <= 0.f) continue;
            const float weight = std::pow(slope, 1.1f);
            gradientX += weight * float(dx[d]) / distance[d];
            gradientY += weight * float(dy[d]) / distance[d];
            totalGradientWeight += weight;
        }
        float bestScore = -std::numeric_limits<float>::infinity();
        const float gradientLength = std::hypot(gradientX, gradientY);
        if (gradientLength > 1e-12f) {
            out.flowVectorX[i] = gradientX / gradientLength;
            out.flowVectorY[i] = gradientY / gradientLength;
        }
        for (int d = 0; d < 8; ++d) {
            const int nx = x + dx[d], ny = y + dy[d];
            const size_t n = at(nx, ny, w);
            const float slope = (routed[i] - routed[n]) / distance[d];
            if (slope <= 0.f) continue;
            // Quantise the continuous multi-neighbour gradient only for the
            // single-channel receiver graph. The contributing area below is
            // still distributed to every downslope neighbour.
            const float alignment = totalGradientWeight > 0.f
                ? (gradientX * float(dx[d]) + gradientY * float(dy[d])) /
                  (totalGradientWeight * distance[d]) : 0.f;
            const float score = alignment + slope * 0.08f;
            if (score > bestScore) { bestScore = score; bestDirection = d; }
        }
        if (bestDirection >= 0) out.flowDirection[i] = int8_t(bestDirection);
    }
    std::vector<size_t> order(count); std::iota(order.begin(), order.end(), size_t(0));
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return routed[a] > routed[b]; });
    // Freeman multiple-flow-direction accumulation. Sheet flow can converge
    // continuously before a channel is selected, avoiding D8's artificial
    // capture of an entire raster column by one early diagonal decision.
    for (size_t i : order) {
        const int x = int(i % size_t(w)), y = int(i / size_t(w));
        std::array<float, 8> weights{};
        float weightSum = 0.f;
        for (int d = 0; d < 8; ++d) {
            const int nx = x + dx[d], ny = y + dy[d];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
            const float slope = (routed[i] - routed[at(nx, ny, w)]) / distance[d];
            if (slope <= 0.f) continue;
            weights[size_t(d)] = std::pow(slope, 1.1f);
            weightSum += weights[size_t(d)];
        }
        if (weightSum <= 0.f) continue;
        for (int d = 0; d < 8; ++d) if (weights[size_t(d)] > 0.f) {
            const int nx = x + dx[d], ny = y + dy[d];
            out.flowAccumulation[at(nx, ny, w)] +=
                out.flowAccumulation[i] * weights[size_t(d)] / weightSum;
        }
    }
    const float cutoff = threshold <= 1.f ? std::max(2.f, threshold * float(count)) : threshold;
    std::vector<uint8_t> suppressedBoundaryBasin(count, 0);
    std::vector<uint8_t> boundaryOutletChannel(count, 0);
    std::vector<size_t> boundaryBasinMouths;

    // Priority-Flood reports every numerical depression, but most tiny noise
    // pits are seasonal wet ground rather than perennial lakes. Classify whole
    // connected basins using physical area, depth/volume and contributing
    // catchment. Filtering individual pixels by depth leaves dotted puddles and
    // can cut holes through one coherent lake shoreline.
    if (classifyLakes) {
    std::fill(visited.begin(), visited.end(), uint8_t(0));
    std::vector<size_t> basin;
    std::queue<size_t> basinQueue;
    const float samplesPerReferenceArea = coordinateScale * coordinateScale;
    for (size_t seedIndex = 0; seedIndex < count; ++seedIndex) {
        if (visited[seedIndex] || out.lakeDepth[seedIndex] <= 0.f) continue;
        basin.clear();
        visited[seedIndex] = 1;
        basinQueue.push(seedIndex);
        float maximumDepth = 0.f, depthVolume = 0.f, maximumCatchment = 0.f;
        size_t maximumCatchmentCell = seedIndex;
        int minimumBoundaryDistance = std::min(w, h);
        while (!basinQueue.empty()) {
            const size_t i = basinQueue.front(); basinQueue.pop();
            basin.push_back(i);
            maximumDepth = std::max(maximumDepth, out.lakeDepth[i]);
            depthVolume += out.lakeDepth[i];
            if (out.flowAccumulation[i] > maximumCatchment) {
                maximumCatchment = out.flowAccumulation[i];
                maximumCatchmentCell = i;
            }
            const int x = int(i % size_t(w)), y = int(i / size_t(w));
            minimumBoundaryDistance = std::min(minimumBoundaryDistance,
                std::min({x, y, w - 1 - x, h - 1 - y}));
            for (int d = 0; d < 8; ++d) {
                const int nx = x + dx[d], ny = y + dy[d];
                if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                const size_t n = at(nx, ny, w);
                if (!visited[n] && out.lakeDepth[n] > 0.f) {
                    visited[n] = 1; basinQueue.push(n);
                }
            }
        }
        const float referenceArea = float(basin.size()) /
            std::max(0.001f, samplesPerReferenceArea);
        const float meanDepth = depthVolume / float(basin.size());
        const float catchmentRatio = maximumCatchment / std::max(1.f, cutoff);
        const int boundaryHalo = std::max(1, int(std::ceil(2.f * coordinateScale)));
        const bool perennial = minimumBoundaryDistance > boundaryHalo &&
            referenceArea >= 6.f && maximumDepth >= 0.004f &&
            (meanDepth >= 0.0015f || maximumDepth >= 0.020f) &&
            (catchmentRatio >= 0.35f || referenceArea >= 20.f);
        if (!perennial) {
            if (minimumBoundaryDistance <= boundaryHalo) {
                for (size_t i : basin) suppressedBoundaryBasin[i] = 1;
                boundaryBasinMouths.push_back(maximumCatchmentCell);
            }
            for (size_t i : basin) out.lakeDepth[i] = 0.f;
        }
    }
    }
    // A boundary-touching depression lacks enough off-map context for lake
    // classification. Suppress its Priority-Flood routing tree, then retain
    // only the highest-discharge trunk from the basin mouth to the open edge.
    // This is a deterministic single-outlet fallback until a neighbouring halo
    // is available, and avoids both a fake lake and radial blue spokes.
    for (size_t mouth : boundaryBasinMouths) {
        size_t current = mouth;
        for (size_t steps = 0; steps < count && suppressedBoundaryBasin[current]; ++steps) {
            boundaryOutletChannel[current] = 1;
            const int direction = out.flowDirection[current];
            if (direction < 0 || direction >= 8) break;
            const int x = int(current % size_t(w)), y = int(current / size_t(w));
            const int nx = x + dx[size_t(direction)], ny = y + dy[size_t(direction)];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) break;
            const size_t nextCell = at(nx, ny, w);
            if (nextCell == current) break;
            current = nextCell;
        }
        current = mouth;
        for (size_t steps = 0; steps < count && suppressedBoundaryBasin[current]; ++steps) {
            boundaryOutletChannel[current] = 1;
            const int x = int(current % size_t(w)), y = int(current / size_t(w));
            size_t strongestDonor = current;
            float strongestFlow = -1.f;
            for (int neighbour = 0; neighbour < 8; ++neighbour) {
                const int nx = x + dx[size_t(neighbour)], ny = y + dy[size_t(neighbour)];
                if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                const size_t donor = at(nx, ny, w);
                if (!suppressedBoundaryBasin[donor]) continue;
                const int donorDirection = out.flowDirection[donor];
                if (donorDirection < 0 || donorDirection >= 8 ||
                    nx + dx[size_t(donorDirection)] != x ||
                    ny + dy[size_t(donorDirection)] != y) continue;
                if (out.flowAccumulation[donor] > strongestFlow) {
                    strongestFlow = out.flowAccumulation[donor];
                    strongestDonor = donor;
                }
            }
            if (strongestDonor == current) break;
            current = strongestDonor;
        }
    }
    for (size_t i = 0; i < count; ++i)
        out.rivers[i] = uint8_t(z[i] > seaLevel && out.lakeDepth[i] <= 0.f &&
                                (!suppressedBoundaryBasin[i] || boundaryOutletChannel[i]) &&
                                (out.flowAccumulation[i] >= cutoff || boundaryOutletChannel[i]));
    // MFD accumulation is physically smoother than single-receiver D8, but a
    // portion of the discharge can leave the selected main receiver and make
    // one intermediate cell fall just below the display threshold. A river
    // mask made from the threshold alone then contains one-cell holes even
    // though both reaches belong to the same drainage path. Close the semantic
    // network downstream without changing the MFD accumulation values.
    const std::vector<uint8_t> thresholdRivers = out.rivers;
    for (size_t seed = 0; seed < count; ++seed) {
        if (!thresholdRivers[seed]) continue;
        size_t current = seed;
        for (size_t steps = 0; steps < count; ++steps) {
            const int direction = out.flowDirection[current];
            if (direction < 0 || direction >= 8) break;
            const int x = int(current % size_t(w)), y = int(current / size_t(w));
            const int nx = x + dx[size_t(direction)], ny = y + dy[size_t(direction)];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) break;
            const size_t nextCell = at(nx, ny, w);
            if (z[nextCell] <= seaLevel || out.lakeDepth[nextCell] > 0.f ||
                (suppressedBoundaryBasin[nextCell] && !boundaryOutletChannel[nextCell])) break;
            out.rivers[nextCell] = 1;
            if (nextCell == current) break;
            current = nextCell;
        }
    }
    // Strahler ordering converts the binary river mask into a stable hierarchy:
    // headwaters are order 1 and the order increases only when two tributaries
    // of the same order meet. This is less sensitive to local MFD discharge
    // fluctuations than deriving every geomorphic decision from area alone.
    std::vector<uint16_t> riverIndegree(count, 0);
    for (size_t i = 0; i < count; ++i) {
        if (!out.rivers[i]) continue;
        const int direction = out.flowDirection[i];
        if (direction < 0 || direction >= 8) continue;
        const int x = int(i % size_t(w)), y = int(i / size_t(w));
        const int nx = x + dx[size_t(direction)], ny = y + dy[size_t(direction)];
        if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
        const size_t receiver = at(nx, ny, w);
        if (out.rivers[receiver]) ++riverIndegree[receiver];
    }
    std::vector<uint8_t> maximumIncomingOrder(count, 0), equalMaximumDonors(count, 0);
    std::queue<size_t> orderQueue;
    for (size_t i = 0; i < count; ++i) if (out.rivers[i] && riverIndegree[i] == 0) {
        out.streamOrder[i] = 1; orderQueue.push(i);
    }
    while (!orderQueue.empty()) {
        const size_t i = orderQueue.front(); orderQueue.pop();
        const int direction = out.flowDirection[i];
        if (direction < 0 || direction >= 8) continue;
        const int x = int(i % size_t(w)), y = int(i / size_t(w));
        const int nx = x + dx[size_t(direction)], ny = y + dy[size_t(direction)];
        if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
        const size_t receiver = at(nx, ny, w);
        if (!out.rivers[receiver]) continue;
        const uint8_t donorOrder = out.streamOrder[i];
        if (donorOrder > maximumIncomingOrder[receiver]) {
            maximumIncomingOrder[receiver] = donorOrder;
            equalMaximumDonors[receiver] = 1;
        } else if (donorOrder == maximumIncomingOrder[receiver]) {
            equalMaximumDonors[receiver] = uint8_t(std::min(255,
                int(equalMaximumDonors[receiver]) + 1));
        }
        if (riverIndegree[receiver] > 0 && --riverIndegree[receiver] == 0) {
            out.streamOrder[receiver] = uint8_t(std::min(255,
                int(maximumIncomingOrder[receiver]) +
                (equalMaximumDonors[receiver] >= 2 ? 1 : 0)));
            orderQueue.push(receiver);
        }
    }
    return out;
}

ClimateMap TerrainPipeline::buildClimate(const Heightmap &hm, const HydrologyMap &hydro,
                                         float seaLevel, float latitude, float coordinateScale) {
    ClimateMap out;
    out.width = hm.getWidth(); out.height = hm.getHeight();
    const int w = out.width, h = out.height; const size_t count = size_t(w) * size_t(h);
    out.temperature.resize(count); out.moisture.resize(count); out.biomes.resize(count);
    if (w <= 0 || h <= 0) return out;
    const auto &z = hm.data();

    // World-scale freshwater distance is a more stable ecological signal than
    // repeatedly blurring a one-cell river mask. It creates a continuous
    // riparian corridor across chunk boundaries and a broad lake-shore ecotone
    // while preserving the categorical channel/lake cells themselves.
    const int waterInfluenceRadius = std::max(1, int(std::ceil(4.f * coordinateScale)));
    std::vector<int> freshWaterDistance(count, waterInfluenceRadius + 1);
    std::queue<size_t> waterQueue;
    for (size_t i = 0; i < count; ++i) {
        const bool freshWater = (i < hydro.rivers.size() && hydro.rivers[i]) ||
            (i < hydro.lakeDepth.size() && hydro.lakeDepth[i] > 0.001f);
        if (freshWater) { freshWaterDistance[i] = 0; waterQueue.push(i); }
    }
    constexpr std::array<int, 4> climateDx{-1, 1, 0, 0};
    constexpr std::array<int, 4> climateDy{0, 0, -1, 1};
    while (!waterQueue.empty()) {
        const size_t i = waterQueue.front(); waterQueue.pop();
        if (freshWaterDistance[i] >= waterInfluenceRadius) continue;
        const int x = int(i % size_t(w)), y = int(i / size_t(w));
        for (size_t d = 0; d < climateDx.size(); ++d) {
            const int nx = x + climateDx[d], ny = y + climateDy[d];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
            const size_t n = at(nx, ny, w);
            if (freshWaterDistance[n] > freshWaterDistance[i] + 1) {
                freshWaterDistance[n] = freshWaterDistance[i] + 1;
                waterQueue.push(n);
            }
        }
    }
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const size_t i = at(x, y, w); const float lat = std::abs((float(y) + 0.5f) / float(h) * 2.f - 1.f);
        out.temperature[i] = saturate(1.f - lat * saturate(latitude) - std::max(0.f, z[i] - seaLevel) * 0.7f);
        const float river = i < hydro.rivers.size() && hydro.rivers[i] ? 1.f : 0.f;
        const float lake = i < hydro.lakeDepth.size() && hydro.lakeDepth[i] > 0.001f ? 1.f : 0.f;
        const float drainage = i < hydro.flowAccumulation.size() ? std::log1p(hydro.flowAccumulation[i]) / std::log1p(float(count)) : 0.f;
        const float waterProximity = freshWaterDistance[i] <= waterInfluenceRadius
            ? 1.f - float(freshWaterDistance[i]) / float(waterInfluenceRadius + 1) : 0.f;
        out.moisture[i] = saturate(0.12f + 0.55f * drainage + 0.45f * river +
                                   0.70f * lake + 0.34f * waterProximity * waterProximity +
                                   (z[i] <= seaLevel ? 1.f : 0.f));
    }
    // Drainage is a one-cell D8 field. Diffusing its climatic contribution
    // produces coherent riparian and regional biomes instead of pixel-wide
    // vegetation stripes, while semantic river/ocean cells remain explicit.
    std::vector<float> smoothed = out.moisture;
    for (int iteration = 0; iteration < 5; ++iteration) {
        for (int y = 1; y + 1 < h; ++y) for (int x = 1; x + 1 < w; ++x) {
            const size_t i = at(x, y, w);
            if (z[i] <= seaLevel || (i < hydro.rivers.size() && hydro.rivers[i])) {
                smoothed[i] = out.moisture[i];
                continue;
            }
            const float neighbours = (out.moisture[at(x - 1, y, w)] +
                                      out.moisture[at(x + 1, y, w)] +
                                      out.moisture[at(x, y - 1, w)] +
                                      out.moisture[at(x, y + 1, w)]) * 0.25f;
            smoothed[i] = out.moisture[i] * 0.45f + neighbours * 0.55f;
        }
        out.moisture.swap(smoothed);
    }
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const size_t i = at(x, y, w);
        const float river = i < hydro.rivers.size() && hydro.rivers[i] ? 1.f : 0.f;
        const bool lake = i < hydro.lakeDepth.size() && hydro.lakeDepth[i] > 0.001f;
        const bool nearFreshWater = freshWaterDistance[i] <=
            std::max(1, int(std::ceil(2.5f * coordinateScale)));
        const float localSlope = std::max(
            std::abs(z[at(std::max(0, x - 1), y, w)] - z[at(std::min(w - 1, x + 1), y, w)]),
            std::abs(z[at(x, std::max(0, y - 1), w)] - z[at(x, std::min(h - 1, y + 1), w)]));
        if (z[i] <= seaLevel) out.biomes[i] = Biome::Ocean;
        else if (lake && hydro.lakeDepth[i] <= 0.006f && localSlope < 0.045f)
            out.biomes[i] = Biome::Wetland;
        else if (lake) out.biomes[i] = Biome::Lake;
        else if (river > 0.f) out.biomes[i] = Biome::River;
        else if (z[i] < seaLevel + 0.035f) out.biomes[i] = Biome::Beach;
        else if (nearFreshWater && out.moisture[i] > 0.62f && localSlope < 0.045f)
            out.biomes[i] = Biome::Wetland;
        else if (z[i] > 0.86f) out.biomes[i] = Biome::Alpine;
        else if (out.temperature[i] < 0.22f) out.biomes[i] = out.moisture[i] > 0.38f ? Biome::Taiga : Biome::Tundra;
        else if (out.moisture[i] < 0.22f) out.biomes[i] = Biome::Desert;
        else if (out.moisture[i] < 0.48f) out.biomes[i] = Biome::Grassland;
        else if (out.moisture[i] < 0.75f) out.biomes[i] = Biome::Forest;
        else out.biomes[i] = Biome::Rainforest;
    }
    return out;
}

}  // namespace eve::procgen
