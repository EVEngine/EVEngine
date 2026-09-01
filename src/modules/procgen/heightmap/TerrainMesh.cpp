#include "procgen/heightmap/TerrainMesh.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <queue>

namespace eve::procgen {
namespace {
float smoothstep(float a, float b, float v) {
    const float t = std::clamp((v - a) / (b - a), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

std::array<float, 3> normalAt(const Heightmap &hm, int x, int y, float cell, float heightScale) {
    const int x0 = std::max(0, x - 1), x1 = std::min(hm.getWidth() - 1, x + 1);
    const int y0 = std::max(0, y - 1), y1 = std::min(hm.getHeight() - 1, y + 1);
    const float dx = (hm.height(x1, y) - hm.height(x0, y)) * heightScale /
                     (float(std::max(1, x1 - x0)) * cell);
    const float dz = (hm.height(x, y1) - hm.height(x, y0)) * heightScale /
                     (float(std::max(1, y1 - y0)) * cell);
    const float inv = 1.f / std::sqrt(dx * dx + 1.f + dz * dz);
    return {-dx * inv, inv, -dz * inv};
}

std::array<float, 4> weightsAt(const Heightmap &hm, const TerrainLayers *layers, int x, int y,
                               const std::array<float, 3> &normal, float minH, float rangeH) {
    std::array<float, 4> w{0.05f, 0.05f, 0.05f, 0.05f};
    auto addBiome = [](std::array<float, 4> &target, Biome biome, float scale) {
        switch (biome) {
        case Biome::Ocean: case Biome::Beach: case Biome::Desert:
            target[0] += 0.9f * scale; break;
        case Biome::River:
            // Exposed mountain reaches are wet rock/riverbank rather than a
            // bright sand stripe. Calm reaches receive a separate water mesh.
            target[0] += 0.10f * scale;
            target[1] += 0.34f * scale;
            target[2] += 0.46f * scale;
            break;
        case Biome::Lake: target[0] += 0.75f * scale; target[1] += 0.15f * scale; break;
        case Biome::Wetland: target[1] += 0.68f * scale; target[0] += 0.22f * scale; break;
        case Biome::Grassland: case Biome::Forest: case Biome::Rainforest:
            target[1] += 0.9f * scale; break;
        case Biome::Tundra: case Biome::Taiga:
            target[3] += 0.75f * scale; target[1] += 0.15f * scale; break;
        case Biome::Alpine: target[2] += 0.85f * scale; target[3] += 0.1f * scale; break;
        }
    };
    if (layers) {
        // Biomes remain categorical for gameplay, but their material response
        // is a compact Gaussian blend. This removes chessboard boundaries at
        // climate thresholds without washing out slope-driven rock or snow.
        float kernelSum = 0.f;
        for (int oy = -1; oy <= 1; ++oy) for (int ox = -1; ox <= 1; ++ox) {
            const int sx = std::clamp(x + ox, 0, hm.getWidth() - 1);
            const int sy = std::clamp(y + oy, 0, hm.getHeight() - 1);
            const float kernel = float((ox == 0 ? 2 : 1) * (oy == 0 ? 2 : 1));
            const int biome = std::clamp(layers->getBiome(sx, sy), 0, int(Biome::Wetland));
            addBiome(w, Biome(biome), kernel);
            kernelSum += kernel;
        }
        for (float &value : w) value /= kernelSum;
    } else addBiome(w, Biome::Grassland, 1.f);
    const float slope = 1.f - normal[1];
    w[2] += smoothstep(0.08f, 0.42f, slope) * 1.2f;
    if (layers) {
        const float drainage = std::log1p(std::max(0.f, layers->getFlowAccumulation(x, y))) /
            std::log1p(float(std::max(2, hm.getWidth() * hm.getHeight())));
        const float lowGradient = 1.f - smoothstep(0.025f, 0.16f, slope);
        const float alluvium = smoothstep(0.42f, 0.72f, drainage) * lowGradient;
        // Mature, low-gradient drainage corridors expose silt, gravel and
        // point-bar sediment even when the surrounding biome is humid. Without
        // this counter-signal the widened floodplain is painted as one uniform
        // green carpet and its geomorphic break in slope becomes invisible.
        w[0] += alluvium * 0.72f;
        w[2] += alluvium * 0.10f;
    }
    const float elevation = rangeH > 0.f ? (hm.height(x, y) - minH) / rangeH : 0.f;
    w[2] += smoothstep(0.62f, 0.82f, elevation) * 0.35f;
    w[3] += smoothstep(0.78f, 0.96f, elevation) * 0.65f;
    if (layers) w[1] += layers->getMoisture(x, y) * (1.f - smoothstep(0.05f, 0.3f, slope)) * 0.25f;
    const float sum = w[0] + w[1] + w[2] + w[3];
    for (float &value : w) value /= sum;
    return w;
}

std::vector<int> sampleAxis(int begin, int cells, int step) {
    std::vector<int> axis;
    for (int v = begin; v < begin + cells; v += step) axis.push_back(v);
    axis.push_back(begin + cells);
    return axis;
}
}  // namespace

float TerrainMeshChunk::getMaterialWeight(int vertex, int channel) const {
    if (vertex < 0 || vertex >= getVertexCount() || channel < 0 || channel >= 4) return 0.f;
    return weights_[size_t(vertex) * 4u + size_t(channel)];
}

int TerrainMeshChunk::getBiome(int vertex) const {
    if (vertex < 0 || size_t(vertex) >= biomes_.size()) return -1;
    return int(biomes_[size_t(vertex)]);
}

bool TerrainMeshBuilder::build(const Heightmap &hm, const TerrainLayers *layers,
                               const TerrainMeshSettings &s, TerrainMeshChunk &out,
                               std::string *error) {
    if (s.cellsX <= 0 || s.cellsY <= 0 || s.originX < 0 || s.originY < 0 ||
        s.originX + s.cellsX >= hm.getWidth() || s.originY + s.cellsY >= hm.getHeight() ||
        s.lod < 0 || s.lod > 20 || !std::isfinite(s.cellSize) || s.cellSize <= 0.f ||
        !std::isfinite(s.heightScale) || !std::isfinite(s.skirtDepth) || s.skirtDepth < 0.f) {
        if (error) *error = "terrain mesh: invalid bounds or settings";
        return false;
    }
    if (layers && (layers->getWidth() != hm.getWidth() || layers->getHeight() != hm.getHeight())) {
        if (error) *error = "terrain mesh: layer dimensions do not match heightmap";
        return false;
    }
    const int step = 1 << s.lod;
    const std::vector<int> xs = sampleAxis(s.originX, s.cellsX, step);
    const std::vector<int> ys = sampleAxis(s.originY, s.cellsY, step);
    const auto [minIt, maxIt] = std::minmax_element(hm.data().begin(), hm.data().end());
    const float minH = *minIt, rangeH = *maxIt - *minIt;
    out = {}; out.lodStep_ = step; out.originX_ = s.originX; out.originY_ = s.originY;
    out.geometricError_ = estimateGeometricError(hm, s);
    const int cols = int(xs.size()), rows = int(ys.size());
    out.splatWidth_ = cols; out.splatHeight_ = rows;
    out.mesh_.reserve(cols * rows + (cols + rows) * 4, (cols - 1) * (rows - 1) * 6);
    auto addVertex = [&](int gx, int gy, float yOffset, const std::array<float, 3> &n) {
        out.mesh_.addVertex(float(gx - s.originX) * s.cellSize,
                            hm.height(gx, gy) * s.heightScale + yOffset,
                            float(gy - s.originY) * s.cellSize, n[0], n[1], n[2],
                            float(gx - s.originX) / float(s.cellsX),
                            float(gy - s.originY) / float(s.cellsY));
        const auto weights = weightsAt(hm, layers, gx, gy, normalAt(hm, gx, gy, s.cellSize, s.heightScale), minH, rangeH);
        out.weights_.insert(out.weights_.end(), weights.begin(), weights.end());
        out.biomes_.push_back(uint8_t(layers ? std::clamp(layers->getBiome(gx, gy), 0, int(Biome::Wetland))
                                             : int(Biome::Grassland)));
    };
    for (int gy : ys) for (int gx : xs) addVertex(gx, gy, 0.f, normalAt(hm, gx, gy, s.cellSize, s.heightScale));
    out.baseVertexCount_ = out.mesh_.getVertexCount();
    for (int y = 0; y + 1 < rows; ++y) for (int x = 0; x + 1 < cols; ++x) {
        const uint32_t a = uint32_t(y * cols + x), b = a + 1, c = a + uint32_t(cols), d = c + 1;
        out.mesh_.addTriangle(a, c, b); out.mesh_.addTriangle(b, c, d);
    }

    if (s.skirtDepth > 0.f) {
        struct Edge { std::vector<std::pair<int, int>> points; std::array<float, 3> normal; };
        std::array<Edge, 4> edges;
        for (int gx : xs) edges[0].points.emplace_back(gx, ys.front());
        for (int gy : ys) edges[1].points.emplace_back(xs.back(), gy);
        for (auto it = xs.rbegin(); it != xs.rend(); ++it) edges[2].points.emplace_back(*it, ys.back());
        for (auto it = ys.rbegin(); it != ys.rend(); ++it) edges[3].points.emplace_back(xs.front(), *it);
        edges[0].normal = {0.f, 0.f, -1.f}; edges[1].normal = {1.f, 0.f, 0.f};
        edges[2].normal = {0.f, 0.f, 1.f}; edges[3].normal = {-1.f, 0.f, 0.f};
        for (const Edge &edge : edges) {
            const uint32_t start = uint32_t(out.mesh_.getVertexCount());
            for (const auto &[gx, gy] : edge.points) {
                addVertex(gx, gy, 0.f, edge.normal); addVertex(gx, gy, -s.skirtDepth, edge.normal);
            }
            for (uint32_t i = 0; i + 1 < edge.points.size(); ++i) {
                const uint32_t topA = start + i * 2, bottomA = topA + 1, topB = topA + 2, bottomB = topB + 1;
                out.mesh_.addTriangle(topA, topB, bottomA); out.mesh_.addTriangle(topB, bottomB, bottomA);
            }
        }
    }
    out.mesh_.setMeta("kind", "terrain.chunk");
    out.mesh_.setMeta("lod", std::to_string(s.lod));
    out.mesh_.setMeta("originX", std::to_string(s.originX));
    out.mesh_.setMeta("originY", std::to_string(s.originY));
    return true;
}

float TerrainMeshBuilder::estimateGeometricError(const Heightmap &hm,
                                                  const TerrainMeshSettings &s) {
    if (s.lod <= 0 || s.originX < 0 || s.originY < 0 || s.cellsX <= 0 || s.cellsY <= 0 ||
        s.originX + s.cellsX >= hm.getWidth() || s.originY + s.cellsY >= hm.getHeight() ||
        !std::isfinite(s.heightScale)) return 0.f;
    const int step = 1 << std::min(s.lod, 20);
    float maxError = 0.f;
    for (int by = 0; by < s.cellsY; by += step) {
        const int y0 = s.originY + by;
        const int y1 = s.originY + std::min(by + step, s.cellsY);
        for (int bx = 0; bx < s.cellsX; bx += step) {
            const int x0 = s.originX + bx;
            const int x1 = s.originX + std::min(bx + step, s.cellsX);
            const float h00 = hm.height(x0, y0), h10 = hm.height(x1, y0);
            const float h01 = hm.height(x0, y1), h11 = hm.height(x1, y1);
            for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) {
                const float tx = x1 > x0 ? float(x - x0) / float(x1 - x0) : 0.f;
                const float ty = y1 > y0 ? float(y - y0) / float(y1 - y0) : 0.f;
                const float approximation = std::lerp(std::lerp(h00, h10, tx),
                                                       std::lerp(h01, h11, tx), ty);
                maxError = std::max(maxError,
                    std::abs(hm.height(x, y) - approximation) * std::abs(s.heightScale));
            }
        }
    }
    return maxError;
}

int TerrainLodSelector::select(const Heightmap &hm, TerrainMeshSettings s, int maxLod,
                               float distanceToCamera, float viewportHeight,
                               float verticalFovDegrees, float targetPixelError) {
    if (maxLod < 0 || maxLod > 20 || !std::isfinite(distanceToCamera) || distanceToCamera <= 0.f ||
        !std::isfinite(viewportHeight) || viewportHeight <= 0.f ||
        !std::isfinite(verticalFovDegrees) || verticalFovDegrees <= 1.f || verticalFovDegrees >= 179.f ||
        !std::isfinite(targetPixelError) || targetPixelError <= 0.f ||
        s.originX < 0 || s.originY < 0 || s.cellsX <= 0 || s.cellsY <= 0 ||
        s.originX + s.cellsX >= hm.getWidth() || s.originY + s.cellsY >= hm.getHeight() ||
        !std::isfinite(s.heightScale)) return -1;
    const float radians = verticalFovDegrees * 0.01745329251994329577f;
    const float projectionScale = viewportHeight / (2.f * std::tan(radians * 0.5f));
    for (int lod = maxLod; lod >= 0; --lod) {
        s.lod = lod;
        const float projected = TerrainMeshBuilder::estimateGeometricError(hm, s) *
                                projectionScale / distanceToCamera;
        if (projected <= targetPixelError) return lod;
    }
    return 0;
}

bool TerrainRiverMeshBuilder::build(const Heightmap &hm, const TerrainLayers &layers,
                                    const TerrainRiverMeshSettings &s, MeshBuild &out,
                                    std::string *error) {
    if (s.cellsX <= 0 || s.cellsY <= 0 || s.originX < 0 || s.originY < 0 ||
        s.originX + s.cellsX >= hm.getWidth() || s.originY + s.cellsY >= hm.getHeight() ||
        layers.getWidth() != hm.getWidth() || layers.getHeight() != hm.getHeight() ||
        !std::isfinite(s.cellSize) || s.cellSize <= 0.f ||
        !std::isfinite(s.heightScale) || !std::isfinite(s.minWidth) ||
        !std::isfinite(s.maxWidth) || s.minWidth <= 0.f || s.maxWidth < s.minWidth ||
        !std::isfinite(s.heightOffset) || !std::isfinite(s.minSurfaceSlope) ||
        !std::isfinite(s.maxSurfaceSlope) || s.minSurfaceSlope < 0.f ||
        s.maxSurfaceSlope <= s.minSurfaceSlope) {
        if (error) *error = "terrain river mesh: invalid bounds or settings";
        return false;
    }
    out = {};
    static constexpr std::array<int, 8> flowDx{-1, 0, 1, -1, 1, -1, 0, 1};
    static constexpr std::array<int, 8> flowDy{-1, -1, -1, 0, 0, 1, 1, 1};
    const size_t layerCells = size_t(layers.getWidth()) * size_t(layers.getHeight());
    std::vector<float> channelFlow(layerCells, 0.f);
    std::vector<uint16_t> riverIndegree(layerCells, 0);
    for (int y = 0; y < layers.getHeight(); ++y) for (int x = 0; x < layers.getWidth(); ++x) {
        const size_t i = size_t(y * layers.getWidth() + x);
        if (!layers.isRiver(x, y)) continue;
        channelFlow[i] = std::max(0.f, layers.getFlowAccumulation(x, y));
        const int direction = layers.getFlowDirection(x, y);
        if (direction < 0 || direction >= 8) continue;
        const int rx = x + flowDx[size_t(direction)], ry = y + flowDy[size_t(direction)];
        if (rx >= 0 && ry >= 0 && rx < layers.getWidth() && ry < layers.getHeight() &&
            layers.isRiver(rx, ry)) ++riverIndegree[size_t(ry * layers.getWidth() + rx)];
    }
    std::queue<size_t> riverQueue;
    for (size_t i = 0; i < layerCells; ++i)
        if (channelFlow[i] > 0.f && riverIndegree[i] == 0) riverQueue.push(i);
    while (!riverQueue.empty()) {
        const size_t i = riverQueue.front(); riverQueue.pop();
        const int x = int(i % size_t(layers.getWidth()));
        const int y = int(i / size_t(layers.getWidth()));
        const int direction = layers.getFlowDirection(x, y);
        if (direction < 0 || direction >= 8) continue;
        const int rx = x + flowDx[size_t(direction)], ry = y + flowDy[size_t(direction)];
        if (rx < 0 || ry < 0 || rx >= layers.getWidth() || ry >= layers.getHeight() ||
            !layers.isRiver(rx, ry)) continue;
        const size_t receiver = size_t(ry * layers.getWidth() + rx);
        channelFlow[receiver] = std::max(channelFlow[receiver], channelFlow[i]);
        if (riverIndegree[receiver] > 0 && --riverIndegree[receiver] == 0)
            riverQueue.push(receiver);
    }
    float maxFlow = 1.f;
    for (float flow : channelFlow) maxFlow = std::max(maxFlow, flow);
    std::vector<uint8_t> calmReach(layerCells, 0);
    for (int y = 0; y < layers.getHeight(); ++y) for (int x = 0; x < layers.getWidth(); ++x) {
        const int d = layers.getFlowDirection(x, y);
        if (!layers.isRiver(x, y) || d < 0 || d >= 8) continue;
        const int rx = x + flowDx[size_t(d)], ry = y + flowDy[size_t(d)];
        if (rx < 0 || ry < 0 || rx >= hm.getWidth() || ry >= hm.getHeight()) continue;
        const float run = s.cellSize * std::sqrt(float(flowDx[size_t(d)] * flowDx[size_t(d)] +
                                                       flowDy[size_t(d)] * flowDy[size_t(d)]));
        const float rise = std::abs(hm.height(rx, ry) - hm.height(x, y)) * s.heightScale;
        const float grade = rise / run;
        calmReach[size_t(y * layers.getWidth() + x)] =
            uint8_t(grade >= s.minSurfaceSlope && grade < s.maxSurfaceSlope);
    }
    // Close isolated one-cell grade spikes only when a calm upstream segment
    // and calm downstream segment both exist. Long cascades remain suppressed.
    std::vector<uint8_t> closedReach = calmReach;
    for (int y = 1; y + 1 < layers.getHeight(); ++y) for (int x = 1; x + 1 < layers.getWidth(); ++x) {
        const size_t i = size_t(y * layers.getWidth() + x);
        const int d = layers.getFlowDirection(x, y);
        if (calmReach[i] || !layers.isRiver(x, y) || d < 0 || d >= 8) continue;
        const int rx = x + flowDx[size_t(d)], ry = y + flowDy[size_t(d)];
        const size_t receiver = size_t(ry * layers.getWidth() + rx);
        if (!calmReach[receiver]) continue;
        bool calmDonor = false;
        for (int incoming = 0; incoming < 8 && !calmDonor; ++incoming) {
            const int nx = x + flowDx[size_t(incoming)], ny = y + flowDy[size_t(incoming)];
            const int nd = layers.getFlowDirection(nx, ny);
            if (nd < 0 || nd >= 8) continue;
            const size_t n = size_t(ny * layers.getWidth() + nx);
            calmDonor = calmReach[n] && nx + flowDx[size_t(nd)] == x &&
                         ny + flowDy[size_t(nd)] == y;
        }
        if (calmDonor) closedReach[i] = 1;
    }
    auto strongestDonor = [&](int sx, int sy) {
        float donorFlow = -1.f;
        int donorX = sx, donorY = sy;
        for (int candidate = 0; candidate < 8; ++candidate) {
            const int dxCell = sx + flowDx[size_t(candidate)];
            const int dyCell = sy + flowDy[size_t(candidate)];
            if (dxCell < 0 || dyCell < 0 || dxCell >= layers.getWidth() ||
                dyCell >= layers.getHeight()) continue;
            const int donorDirection = layers.getFlowDirection(dxCell, dyCell);
            if (donorDirection < 0 || donorDirection >= 8 ||
                dxCell + flowDx[size_t(donorDirection)] != sx ||
                dyCell + flowDy[size_t(donorDirection)] != sy) continue;
            const float flow = layers.getFlowAccumulation(dxCell, dyCell);
            if (flow > donorFlow) {
                donorFlow = flow; donorX = dxCell; donorY = dyCell;
            }
        }
        return std::array<int, 2>{donorX, donorY};
    };
    auto receiver = [&](int sx, int sy) {
        const int direction = layers.getFlowDirection(sx, sy);
        if (direction < 0 || direction >= 8) return std::array<int, 2>{sx, sy};
        const int receiverX = sx + flowDx[size_t(direction)];
        const int receiverY = sy + flowDy[size_t(direction)];
        if (receiverX < 0 || receiverY < 0 || receiverX >= layers.getWidth() ||
            receiverY >= layers.getHeight()) return std::array<int, 2>{sx, sy};
        return std::array<int, 2>{receiverX, receiverY};
    };
    auto smoothCenter = [&](int sx, int sy) {
        // Binomial five-point filtering follows the highest-discharge donor
        // through the cell and two receivers downstream. It suppresses the
        // one-cell D8 staircase while retaining confluences and is deterministic
        // across independently generated chunks. The weights sum to one and
        // preserve straight reaches exactly.
        const auto pMinus1 = strongestDonor(sx, sy);
        const auto pMinus2 = strongestDonor(pMinus1[0], pMinus1[1]);
        const auto pPlus1 = receiver(sx, sy);
        const auto pPlus2 = receiver(pPlus1[0], pPlus1[1]);
        return std::array<float, 2>{
            (float(pMinus2[0]) + 4.f * float(pMinus1[0]) + 6.f * float(sx) +
             4.f * float(pPlus1[0]) + float(pPlus2[0])) / 16.f,
            (float(pMinus2[1]) + 4.f * float(pMinus1[1]) + 6.f * float(sy) +
             4.f * float(pPlus1[1]) + float(pPlus2[1])) / 16.f};
    };
    auto sampleHeight = [&](float sx, float sy) {
        sx = std::clamp(sx, 0.f, float(hm.getWidth() - 1));
        sy = std::clamp(sy, 0.f, float(hm.getHeight() - 1));
        const int x0 = int(std::floor(sx)), y0 = int(std::floor(sy));
        const int x1 = std::min(x0 + 1, hm.getWidth() - 1);
        const int y1 = std::min(y0 + 1, hm.getHeight() - 1);
        const float tx = sx - float(x0), ty = sy - float(y0);
        return std::lerp(std::lerp(hm.height(x0, y0), hm.height(x1, y0), tx),
                         std::lerp(hm.height(x0, y1), hm.height(x1, y1), tx), ty);
    };
    for (int y = s.originY; y <= s.originY + s.cellsY; ++y) {
        for (int x = s.originX; x <= s.originX + s.cellsX; ++x) {
            const int direction = layers.getFlowDirection(x, y);
            if (!layers.isRiver(x, y) || direction < 0 || direction >= 8) continue;
            if (!closedReach[size_t(y * layers.getWidth() + x)]) continue;
            const int receiverX = x + flowDx[size_t(direction)];
            const int receiverY = y + flowDy[size_t(direction)];
            const auto startCenter = smoothCenter(x, y);
            const auto endCenter = smoothCenter(receiverX, receiverY);
            const float tx0 = endCenter[0] - startCenter[0];
            const float tz0 = endCenter[1] - startCenter[1];
            const float tangentLengthSquared = tx0 * tx0 + tz0 * tz0;
            if (tangentLengthSquared <= 1e-8f) continue;
            const float invLength = 1.f / std::sqrt(tangentLengthSquared);
            const float tx = tx0 * invLength, tz = tz0 * invLength;
            auto widthAt = [&](int sampleX, int sampleY) {
                // Downstream hydraulic geometry is commonly approximated by a
                // discharge power law. The 0.36 exponent retains readable head
                // streams while producing gradual main-stem widening.
                const float discharge = channelFlow[size_t(sampleY * layers.getWidth() + sampleX)];
                const float areaScale = maxFlow > 1.f
                    ? std::pow(std::clamp(discharge / maxFlow, 0.f, 1.f), 0.36f)
                    : 0.f;
                const float orderScale = std::clamp(
                    (float(layers.getStreamOrder(sampleX, sampleY)) - 1.f) / 4.f, 0.f, 1.f);
                const float flowScale = std::max(areaScale, orderScale * 0.82f);
                return 0.5f * std::lerp(s.minWidth, s.maxWidth, flowScale);
            };
            const float startWidth = widthAt(x, y);
            const float endWidth = layers.isRiver(receiverX, receiverY)
                ? widthAt(receiverX, receiverY) : startWidth;
            float startFlowX = layers.getFlowVectorX(x, y);
            float startFlowY = layers.getFlowVectorY(x, y);
            float endFlowX = layers.getFlowVectorX(receiverX, receiverY);
            float endFlowY = layers.getFlowVectorY(receiverX, receiverY);
            if (std::hypot(startFlowX, startFlowY) < 0.1f) {
                startFlowX = tx; startFlowY = tz;
            }
            if (std::hypot(endFlowX, endFlowY) < 0.1f) {
                endFlowX = tx; endFlowY = tz;
            }
            const float chordCells = std::sqrt(tangentLengthSquared);
            auto curvePoint = [&](float t) {
                const float t2 = t * t, t3 = t2 * t;
                const float h00 = 2.f * t3 - 3.f * t2 + 1.f;
                const float h10 = t3 - 2.f * t2 + t;
                const float h01 = -2.f * t3 + 3.f * t2;
                const float h11 = t3 - t2;
                return std::array<float, 2>{
                    h00 * startCenter[0] + h10 * startFlowX * chordCells +
                        h01 * endCenter[0] + h11 * endFlowX * chordCells,
                    h00 * startCenter[1] + h10 * startFlowY * chordCells +
                        h01 * endCenter[1] + h11 * endFlowY * chordCells};
            };
            constexpr int curveSegments = 5;
            std::array<std::array<float, 2>, curveSegments + 1> curve{};
            for (int point = 0; point <= curveSegments; ++point)
                curve[size_t(point)] = curvePoint(float(point) / float(curveSegments));
            const uint32_t stripStart = uint32_t(out.getVertexCount());
            for (int point = 0; point <= curveSegments; ++point) {
                const int previous = std::max(0, point - 1);
                const int following = std::min(curveSegments, point + 1);
                const float qdx = curve[size_t(following)][0] - curve[size_t(previous)][0];
                const float qdy = curve[size_t(following)][1] - curve[size_t(previous)][1];
                const float t = float(point) / float(curveSegments);
                // Every independently emitted reach that meets at a drainage
                // cell must produce the same bank pair at that cell. Using the
                // finite-difference tangent of each Hermite span made the two
                // reaches disagree at their shared endpoint, leaving the
                // scalloped/saw-tooth silhouette visible in rendered rivers.
                // The stored continuous flow vector is cell-local and therefore
                // identical on both sides of the join. Blend it inside the span,
                // but snap endpoint frames exactly to the corresponding cell.
                float frameX = std::lerp(startFlowX, endFlowX, t);
                float frameY = std::lerp(startFlowY, endFlowY, t);
                if (point == 0) { frameX = startFlowX; frameY = startFlowY; }
                if (point == curveSegments) { frameX = endFlowX; frameY = endFlowY; }
                float frameLength = std::hypot(frameX, frameY);
                if (frameLength < 1e-5f) {
                    frameX = qdx; frameY = qdy;
                    frameLength = std::max(1e-5f, std::hypot(frameX, frameY));
                }
                const float qpx = -frameY / frameLength;
                const float qpz = frameX / frameLength;
                const float width = std::lerp(startWidth, endWidth, t);
                const auto &q = curve[size_t(point)];
                const float qx = (q[0] - float(s.originX)) * s.cellSize;
                const float qz = (q[1] - float(s.originY)) * s.cellSize;
                const float widthCells = width / s.cellSize;
                const float centerBed = sampleHeight(q[0], q[1]);
                const float leftBed = sampleHeight(q[0] - qpx * widthCells,
                                                   q[1] - qpz * widthCells);
                const float rightBed = sampleHeight(q[0] + qpx * widthCells,
                                                    q[1] + qpz * widthCells);
                // One transverse water level must clear both banks. Sampling
                // only the center buried the uphill half of ribbons whenever
                // a smoothed centerline crossed a sloping raster triangle,
                // which appeared as broken rectangular river segments.
                const float qy = std::max({centerBed, leftBed, rightBed}) *
                                 s.heightScale + s.heightOffset;
                out.addVertex(qx - qpx * width, qy, qz - qpz * width,
                              0.f, 1.f, 0.f, 0.f, t);
                out.addVertex(qx + qpx * width, qy, qz + qpz * width,
                              0.f, 1.f, 0.f, 1.f, t);
            }
            for (int segment = 0; segment < curveSegments; ++segment) {
                const uint32_t a = stripStart + uint32_t(segment * 2);
                const uint32_t b = a + 1u, c = a + 2u, dVertex = a + 3u;
                out.addTriangle(a, c, b);
                out.addTriangle(b, c, dVertex);
            }
        }
    }
    out.setMeta("kind", "terrain.rivers");
    return true;
}

bool TerrainLakeMeshBuilder::build(const Heightmap &hm, const TerrainLayers &layers,
                                   const TerrainLakeMeshSettings &s, MeshBuild &out,
                                   std::string *error) {
    if (s.cellsX <= 0 || s.cellsY <= 0 || s.originX < 0 || s.originY < 0 ||
        s.originX + s.cellsX >= hm.getWidth() || s.originY + s.cellsY >= hm.getHeight() ||
        layers.getWidth() != hm.getWidth() || layers.getHeight() != hm.getHeight() ||
        !std::isfinite(s.cellSize) || s.cellSize <= 0.f ||
        !std::isfinite(s.heightScale) || !std::isfinite(s.minimumDepth) ||
        s.minimumDepth < 0.f || !std::isfinite(s.heightOffset)) {
        if (error) *error = "terrain lake mesh: invalid bounds or settings";
        return false;
    }
    out = {};
    struct ShoreVertex { float x, y, z, depth; };
    for (int y = s.originY; y < s.originY + s.cellsY; ++y) {
        for (int x = s.originX; x < s.originX + s.cellsX; ++x) {
            const float d00 = layers.getLakeDepth(x, y);
            const float d10 = layers.getLakeDepth(x + 1, y);
            const float d01 = layers.getLakeDepth(x, y + 1);
            const float d11 = layers.getLakeDepth(x + 1, y + 1);
            if (std::max({d00, d10, d01, d11}) < s.minimumDepth) continue;
            auto surface = [&](int sx, int sy, float depth) {
                return (hm.height(sx, sy) + depth) * s.heightScale + s.heightOffset;
            };
            const float x0 = float(x - s.originX) * s.cellSize;
            const float x1 = float(x + 1 - s.originX) * s.cellSize;
            const float z0 = float(y - s.originY) * s.cellSize;
            const float z1 = float(y + 1 - s.originY) * s.cellSize;
            // Clip the cell polygon against the requested depth contour. This
            // is the filled equivalent of marching squares and places shoreline
            // vertices inside cells instead of producing staircase coastlines.
            std::vector<ShoreVertex> polygon{
                {x0, surface(x, y, d00), z0, d00},
                {x1, surface(x + 1, y, d10), z0, d10},
                {x1, surface(x + 1, y + 1, d11), z1, d11},
                {x0, surface(x, y + 1, d01), z1, d01},
            };
            std::vector<ShoreVertex> clipped;
            for (size_t edge = 0; edge < polygon.size(); ++edge) {
                const ShoreVertex &a = polygon[edge];
                const ShoreVertex &b = polygon[(edge + 1) % polygon.size()];
                const bool aInside = a.depth >= s.minimumDepth;
                const bool bInside = b.depth >= s.minimumDepth;
                if (aInside) clipped.push_back(a);
                if (aInside == bInside) continue;
                const float t = std::clamp((s.minimumDepth - a.depth) /
                                           (b.depth - a.depth), 0.f, 1.f);
                clipped.push_back({std::lerp(a.x, b.x, t), std::lerp(a.y, b.y, t),
                                   std::lerp(a.z, b.z, t), s.minimumDepth});
            }
            if (clipped.size() < 3) continue;
            const uint32_t first = uint32_t(out.getVertexCount());
            for (const ShoreVertex &v : clipped) {
                const float u = (v.x / (float(s.cellsX) * s.cellSize));
                const float texV = (v.z / (float(s.cellsY) * s.cellSize));
                out.addVertex(v.x, v.y, v.z, 0.f, 1.f, 0.f, u, texV);
            }
            for (uint32_t vertex = 1; vertex + 1 < clipped.size(); ++vertex)
                out.addTriangle(first, first + vertex + 1, first + vertex);
        }
    }
    out.setMeta("kind", "terrain.lakes");
    return true;
}

}  // namespace eve::procgen
