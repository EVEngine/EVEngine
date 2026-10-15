// CPU terrain surface generation, streaming and terrace analysis.
// Kept outside the grass/script adapter so native logic tests need no renderer.
#include <algorithm>
#include <array>
#include <cmath>
#include <queue>
#include <tuple>
#include <vector>
#include "procgen/heightmap/TerrainMesh.h"
#include "procgen/heightmap/TerrainRasterInternal.h"
#include "procgen/heightmap/TerrainStreaming.h"
#include "procgen/heightmap/TerrainTerraceRemover.h"
#include "procgen/texture/NoiseField.h"

// TerrainMesh.cpp
namespace eve::procgen {
namespace {
float smoothstep(float a, float b, float v) {
    const float t = std::clamp((v - a) / (b - a), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

std::array<float, 3> normalAt(const Heightmap &hm, int x, int y, float cell, float heightScale) {
    const int   x0 = std::max(0, x - 1), x1 = std::min(hm.getWidth() - 1, x + 1);
    const int   y0 = std::max(0, y - 1), y1 = std::min(hm.getHeight() - 1, y + 1);
    const float dx  = (hm.height(x1, y) - hm.height(x0, y)) * heightScale / (float(std::max(1, x1 - x0)) * cell);
    const float dz  = (hm.height(x, y1) - hm.height(x, y0)) * heightScale / (float(std::max(1, y1 - y0)) * cell);
    const float inv = 1.f / std::sqrt(dx * dx + 1.f + dz * dz);
    return {-dx * inv, inv, -dz * inv};
}

std::array<float, 4> weightsAt(const Heightmap &hm, const TerrainLayers *layers, int x, int y,
                               const std::array<float, 3> &normal, float minH, float rangeH) {
    std::array<float, 4> w{0.05f, 0.05f, 0.05f, 0.05f};
    auto                 addBiome = [](std::array<float, 4> &target, Biome biome, float scale) {
        switch (biome) {
            case Biome::Ocean:
            case Biome::Beach:
            case Biome::Desert: target[0] += 0.9f * scale; break;
            case Biome::River:
                // Exposed mountain reaches are wet rock/riverbank rather than a
                // bright sand stripe. Calm reaches receive a separate water mesh.
                target[0] += 0.10f * scale;
                target[1] += 0.34f * scale;
                target[2] += 0.46f * scale;
                break;
            case Biome::Lake:
                target[0] += 0.75f * scale;
                target[1] += 0.15f * scale;
                break;
            case Biome::Wetland:
                target[1] += 0.68f * scale;
                target[0] += 0.22f * scale;
                break;
            case Biome::Grassland:
            case Biome::Forest:
            case Biome::Rainforest: target[1] += 0.9f * scale; break;
            case Biome::Tundra:
            case Biome::Taiga:
                target[3] += 0.75f * scale;
                target[1] += 0.15f * scale;
                break;
            case Biome::Alpine:
                target[2] += 0.85f * scale;
                target[3] += 0.1f * scale;
                break;
        }
    };
    if (layers) {
        // Biomes remain categorical for gameplay, but their material response
        // is a compact Gaussian blend. This removes chessboard boundaries at
        // climate thresholds without washing out slope-driven rock or snow.
        float kernelSum = 0.f;
        for (int oy = -1; oy <= 1; ++oy)
            for (int ox = -1; ox <= 1; ++ox) {
                const int   sx     = std::clamp(x + ox, 0, hm.getWidth() - 1);
                const int   sy     = std::clamp(y + oy, 0, hm.getHeight() - 1);
                const float kernel = float((ox == 0 ? 2 : 1) * (oy == 0 ? 2 : 1));
                const int   biome  = std::clamp(layers->getBiome(sx, sy), 0, int(Biome::Wetland));
                addBiome(w, Biome(biome), kernel);
                kernelSum += kernel;
            }
        for (float &value : w) value /= kernelSum;
    } else
        addBiome(w, Biome::Grassland, 1.f);
    const float slope = 1.f - normal[1];
    w[2] += smoothstep(0.08f, 0.42f, slope) * 1.2f;
    if (layers) {
        const float drainage = std::log1p(std::max(0.f, layers->getFlowAccumulation(x, y))) /
                               std::log1p(float(std::max(2, hm.getWidth() * hm.getHeight())));
        const float lowGradient = 1.f - smoothstep(0.025f, 0.16f, slope);
        const float alluvium    = smoothstep(0.42f, 0.72f, drainage) * lowGradient;
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

bool TerrainMeshBuilder::build(const Heightmap &hm, const TerrainLayers *layers, const TerrainMeshSettings &s,
                               TerrainMeshChunk &out, std::string *error) {
    if (s.cellsX <= 0 || s.cellsY <= 0 || s.originX < 0 || s.originY < 0 || s.originX + s.cellsX >= hm.getWidth() ||
        s.originY + s.cellsY >= hm.getHeight() || s.lod < 0 || s.lod > 20 || !std::isfinite(s.cellSize) ||
        s.cellSize <= 0.f || !std::isfinite(s.heightScale) || !std::isfinite(s.skirtDepth) || s.skirtDepth < 0.f) {
        if (error) *error = "terrain mesh: invalid bounds or settings";
        return false;
    }
    if (layers && (layers->getWidth() != hm.getWidth() || layers->getHeight() != hm.getHeight())) {
        if (error) *error = "terrain mesh: layer dimensions do not match heightmap";
        return false;
    }
    const int              step = 1 << s.lod;
    const std::vector<int> xs   = sampleAxis(s.originX, s.cellsX, step);
    const std::vector<int> ys   = sampleAxis(s.originY, s.cellsY, step);
    const auto [minIt, maxIt]   = std::minmax_element(hm.data().begin(), hm.data().end());
    const float minH = *minIt, rangeH = *maxIt - *minIt;
    out                 = {};
    out.lodStep_        = step;
    out.originX_        = s.originX;
    out.originY_        = s.originY;
    out.geometricError_ = estimateGeometricError(hm, s);
    const int cols = int(xs.size()), rows = int(ys.size());
    out.splatWidth_  = cols;
    out.splatHeight_ = rows;
    out.mesh_.reserve(cols * rows + (cols + rows) * 4, (cols - 1) * (rows - 1) * 6);
    auto addVertex = [&](int gx, int gy, float yOffset, const std::array<float, 3> &n) {
        out.mesh_.addVertex(float(gx - s.originX) * s.cellSize, hm.height(gx, gy) * s.heightScale + yOffset,
                            float(gy - s.originY) * s.cellSize, n[0], n[1], n[2],
                            float(gx - s.originX) / float(s.cellsX), float(gy - s.originY) / float(s.cellsY));
        const auto weights =
            weightsAt(hm, layers, gx, gy, normalAt(hm, gx, gy, s.cellSize, s.heightScale), minH, rangeH);
        out.weights_.insert(out.weights_.end(), weights.begin(), weights.end());
        out.biomes_.push_back(
            uint8_t(layers ? std::clamp(layers->getBiome(gx, gy), 0, int(Biome::Wetland)) : int(Biome::Grassland)));
    };
    for (int gy : ys)
        for (int gx : xs) addVertex(gx, gy, 0.f, normalAt(hm, gx, gy, s.cellSize, s.heightScale));
    out.baseVertexCount_ = out.mesh_.getVertexCount();
    for (int y = 0; y + 1 < rows; ++y)
        for (int x = 0; x + 1 < cols; ++x) {
            const uint32_t a = uint32_t(y * cols + x), b = a + 1, c = a + uint32_t(cols), d = c + 1;
            out.mesh_.addTriangle(a, c, b);
            out.mesh_.addTriangle(b, c, d);
        }

    if (s.skirtDepth > 0.f) {
        struct Edge {
            std::vector<std::pair<int, int>> points;
            std::array<float, 3>             normal;
        };
        std::array<Edge, 4> edges;
        for (int gx : xs) edges[0].points.emplace_back(gx, ys.front());
        for (int gy : ys) edges[1].points.emplace_back(xs.back(), gy);
        for (auto it = xs.rbegin(); it != xs.rend(); ++it) edges[2].points.emplace_back(*it, ys.back());
        for (auto it = ys.rbegin(); it != ys.rend(); ++it) edges[3].points.emplace_back(xs.front(), *it);
        edges[0].normal = {0.f, 0.f, -1.f};
        edges[1].normal = {1.f, 0.f, 0.f};
        edges[2].normal = {0.f, 0.f, 1.f};
        edges[3].normal = {-1.f, 0.f, 0.f};
        for (const Edge &edge : edges) {
            const uint32_t start = uint32_t(out.mesh_.getVertexCount());
            for (const auto &[gx, gy] : edge.points) {
                addVertex(gx, gy, 0.f, edge.normal);
                addVertex(gx, gy, -s.skirtDepth, edge.normal);
            }
            for (uint32_t i = 0; i + 1 < edge.points.size(); ++i) {
                const uint32_t topA = start + i * 2, bottomA = topA + 1, topB = topA + 2, bottomB = topB + 1;
                out.mesh_.addTriangle(topA, topB, bottomA);
                out.mesh_.addTriangle(topB, bottomB, bottomA);
            }
        }
    }
    out.mesh_.setMeta("kind", "terrain.chunk");
    out.mesh_.setMeta("lod", std::to_string(s.lod));
    out.mesh_.setMeta("originX", std::to_string(s.originX));
    out.mesh_.setMeta("originY", std::to_string(s.originY));
    return true;
}

float TerrainMeshBuilder::estimateGeometricError(const Heightmap &hm, const TerrainMeshSettings &s) {
    if (s.lod <= 0 || s.originX < 0 || s.originY < 0 || s.cellsX <= 0 || s.cellsY <= 0 ||
        s.originX + s.cellsX >= hm.getWidth() || s.originY + s.cellsY >= hm.getHeight() ||
        !std::isfinite(s.heightScale))
        return 0.f;
    const int step     = 1 << std::min(s.lod, 20);
    float     maxError = 0.f;
    for (int by = 0; by < s.cellsY; by += step) {
        const int y0 = s.originY + by;
        const int y1 = s.originY + std::min(by + step, s.cellsY);
        for (int bx = 0; bx < s.cellsX; bx += step) {
            const int   x0  = s.originX + bx;
            const int   x1  = s.originX + std::min(bx + step, s.cellsX);
            const float h00 = hm.height(x0, y0), h10 = hm.height(x1, y0);
            const float h01 = hm.height(x0, y1), h11 = hm.height(x1, y1);
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x) {
                    const float tx            = x1 > x0 ? float(x - x0) / float(x1 - x0) : 0.f;
                    const float ty            = y1 > y0 ? float(y - y0) / float(y1 - y0) : 0.f;
                    const float approximation = std::lerp(std::lerp(h00, h10, tx), std::lerp(h01, h11, tx), ty);
                    maxError = std::max(maxError, std::abs(hm.height(x, y) - approximation) * std::abs(s.heightScale));
                }
        }
    }
    return maxError;
}

int TerrainLodSelector::select(const Heightmap &hm, TerrainMeshSettings s, int maxLod, float distanceToCamera,
                               float viewportHeight, float verticalFovDegrees, float targetPixelError) {
    if (maxLod < 0 || maxLod > 20 || !std::isfinite(distanceToCamera) || distanceToCamera <= 0.f ||
        !std::isfinite(viewportHeight) || viewportHeight <= 0.f || !std::isfinite(verticalFovDegrees) ||
        verticalFovDegrees <= 1.f || verticalFovDegrees >= 179.f || !std::isfinite(targetPixelError) ||
        targetPixelError <= 0.f || s.originX < 0 || s.originY < 0 || s.cellsX <= 0 || s.cellsY <= 0 ||
        s.originX + s.cellsX >= hm.getWidth() || s.originY + s.cellsY >= hm.getHeight() ||
        !std::isfinite(s.heightScale))
        return -1;
    const float radians         = verticalFovDegrees * 0.01745329251994329577f;
    const float projectionScale = viewportHeight / (2.f * std::tan(radians * 0.5f));
    for (int lod = maxLod; lod >= 0; --lod) {
        s.lod                 = lod;
        const float projected = TerrainMeshBuilder::estimateGeometricError(hm, s) * projectionScale / distanceToCamera;
        if (projected <= targetPixelError) return lod;
    }
    return 0;
}

bool TerrainRiverMeshBuilder::build(const Heightmap &hm, const TerrainLayers &layers, const TerrainRiverMeshSettings &s,
                                    MeshBuild &out, std::string *error) {
    if (s.cellsX <= 0 || s.cellsY <= 0 || s.originX < 0 || s.originY < 0 || s.originX + s.cellsX >= hm.getWidth() ||
        s.originY + s.cellsY >= hm.getHeight() || layers.getWidth() != hm.getWidth() ||
        layers.getHeight() != hm.getHeight() || !std::isfinite(s.cellSize) || s.cellSize <= 0.f ||
        !std::isfinite(s.heightScale) || !std::isfinite(s.minWidth) || !std::isfinite(s.maxWidth) ||
        s.minWidth <= 0.f || s.maxWidth < s.minWidth || !std::isfinite(s.heightOffset) ||
        !std::isfinite(s.minSurfaceSlope) || !std::isfinite(s.maxSurfaceSlope) || s.minSurfaceSlope < 0.f ||
        s.maxSurfaceSlope <= s.minSurfaceSlope) {
        if (error) *error = "terrain river mesh: invalid bounds or settings";
        return false;
    }
    out = {};
    static constexpr std::array<int, 8> flowDx{-1, 0, 1, -1, 1, -1, 0, 1};
    static constexpr std::array<int, 8> flowDy{-1, -1, -1, 0, 0, 1, 1, 1};
    const size_t                        layerCells = size_t(layers.getWidth()) * size_t(layers.getHeight());
    std::vector<float>                  channelFlow(layerCells, 0.f);
    std::vector<uint16_t>               riverIndegree(layerCells, 0);
    for (int y = 0; y < layers.getHeight(); ++y)
        for (int x = 0; x < layers.getWidth(); ++x) {
            const size_t i = size_t(y * layers.getWidth() + x);
            if (!layers.isRiver(x, y)) continue;
            channelFlow[i]      = std::max(0.f, layers.getFlowAccumulation(x, y));
            const int direction = layers.getFlowDirection(x, y);
            if (direction < 0 || direction >= 8) continue;
            const int rx = x + flowDx[size_t(direction)], ry = y + flowDy[size_t(direction)];
            if (rx >= 0 && ry >= 0 && rx < layers.getWidth() && ry < layers.getHeight() && layers.isRiver(rx, ry))
                ++riverIndegree[size_t(ry * layers.getWidth() + rx)];
        }
    std::queue<size_t> riverQueue;
    for (size_t i = 0; i < layerCells; ++i)
        if (channelFlow[i] > 0.f && riverIndegree[i] == 0) riverQueue.push(i);
    while (!riverQueue.empty()) {
        const size_t i = riverQueue.front();
        riverQueue.pop();
        const int x         = int(i % size_t(layers.getWidth()));
        const int y         = int(i / size_t(layers.getWidth()));
        const int direction = layers.getFlowDirection(x, y);
        if (direction < 0 || direction >= 8) continue;
        const int rx = x + flowDx[size_t(direction)], ry = y + flowDy[size_t(direction)];
        if (rx < 0 || ry < 0 || rx >= layers.getWidth() || ry >= layers.getHeight() || !layers.isRiver(rx, ry))
            continue;
        const size_t receiver = size_t(ry * layers.getWidth() + rx);
        channelFlow[receiver] = std::max(channelFlow[receiver], channelFlow[i]);
        if (riverIndegree[receiver] > 0 && --riverIndegree[receiver] == 0) riverQueue.push(receiver);
    }
    float maxFlow = 1.f;
    for (float flow : channelFlow) maxFlow = std::max(maxFlow, flow);
    std::vector<uint8_t> calmReach(layerCells, 0);
    for (int y = 0; y < layers.getHeight(); ++y)
        for (int x = 0; x < layers.getWidth(); ++x) {
            const int d = layers.getFlowDirection(x, y);
            if (!layers.isRiver(x, y) || d < 0 || d >= 8) continue;
            const int rx = x + flowDx[size_t(d)], ry = y + flowDy[size_t(d)];
            if (rx < 0 || ry < 0 || rx >= hm.getWidth() || ry >= hm.getHeight()) continue;
            const float run =
                s.cellSize *
                std::sqrt(float(flowDx[size_t(d)] * flowDx[size_t(d)] + flowDy[size_t(d)] * flowDy[size_t(d)]));
            const float rise  = std::abs(hm.height(rx, ry) - hm.height(x, y)) * s.heightScale;
            const float grade = rise / run;
            calmReach[size_t(y * layers.getWidth() + x)] =
                uint8_t(grade >= s.minSurfaceSlope && grade < s.maxSurfaceSlope);
        }
    // Close isolated one-cell grade spikes only when a calm upstream segment
    // and calm downstream segment both exist. Long cascades remain suppressed.
    std::vector<uint8_t> closedReach = calmReach;
    for (int y = 1; y + 1 < layers.getHeight(); ++y)
        for (int x = 1; x + 1 < layers.getWidth(); ++x) {
            const size_t i = size_t(y * layers.getWidth() + x);
            const int    d = layers.getFlowDirection(x, y);
            if (calmReach[i] || !layers.isRiver(x, y) || d < 0 || d >= 8) continue;
            const int    rx = x + flowDx[size_t(d)], ry = y + flowDy[size_t(d)];
            const size_t receiver = size_t(ry * layers.getWidth() + rx);
            if (!calmReach[receiver]) continue;
            bool calmDonor = false;
            for (int incoming = 0; incoming < 8 && !calmDonor; ++incoming) {
                const int nx = x + flowDx[size_t(incoming)], ny = y + flowDy[size_t(incoming)];
                const int nd = layers.getFlowDirection(nx, ny);
                if (nd < 0 || nd >= 8) continue;
                const size_t n = size_t(ny * layers.getWidth() + nx);
                calmDonor      = calmReach[n] && nx + flowDx[size_t(nd)] == x && ny + flowDy[size_t(nd)] == y;
            }
            if (calmDonor) closedReach[i] = 1;
        }
    auto strongestDonor = [&](int sx, int sy) {
        float donorFlow = -1.f;
        int   donorX = sx, donorY = sy;
        for (int candidate = 0; candidate < 8; ++candidate) {
            const int dxCell = sx + flowDx[size_t(candidate)];
            const int dyCell = sy + flowDy[size_t(candidate)];
            if (dxCell < 0 || dyCell < 0 || dxCell >= layers.getWidth() || dyCell >= layers.getHeight()) continue;
            const int donorDirection = layers.getFlowDirection(dxCell, dyCell);
            if (donorDirection < 0 || donorDirection >= 8 || dxCell + flowDx[size_t(donorDirection)] != sx ||
                dyCell + flowDy[size_t(donorDirection)] != sy)
                continue;
            const float flow = layers.getFlowAccumulation(dxCell, dyCell);
            if (flow > donorFlow) {
                donorFlow = flow;
                donorX    = dxCell;
                donorY    = dyCell;
            }
        }
        return std::array<int, 2>{donorX, donorY};
    };
    auto receiver = [&](int sx, int sy) {
        const int direction = layers.getFlowDirection(sx, sy);
        if (direction < 0 || direction >= 8) return std::array<int, 2>{sx, sy};
        const int receiverX = sx + flowDx[size_t(direction)];
        const int receiverY = sy + flowDy[size_t(direction)];
        if (receiverX < 0 || receiverY < 0 || receiverX >= layers.getWidth() || receiverY >= layers.getHeight())
            return std::array<int, 2>{sx, sy};
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
        const auto pPlus1  = receiver(sx, sy);
        const auto pPlus2  = receiver(pPlus1[0], pPlus1[1]);
        return std::array<float, 2>{(float(pMinus2[0]) + 4.f * float(pMinus1[0]) + 6.f * float(sx) +
                                     4.f * float(pPlus1[0]) + float(pPlus2[0])) /
                                        16.f,
                                    (float(pMinus2[1]) + 4.f * float(pMinus1[1]) + 6.f * float(sy) +
                                     4.f * float(pPlus1[1]) + float(pPlus2[1])) /
                                        16.f};
    };
    auto sampleHeight = [&](float sx, float sy) {
        sx             = std::clamp(sx, 0.f, float(hm.getWidth() - 1));
        sy             = std::clamp(sy, 0.f, float(hm.getHeight() - 1));
        const int   x0 = int(std::floor(sx)), y0 = int(std::floor(sy));
        const int   x1 = std::min(x0 + 1, hm.getWidth() - 1);
        const int   y1 = std::min(y0 + 1, hm.getHeight() - 1);
        const float tx = sx - float(x0), ty = sy - float(y0);
        return std::lerp(std::lerp(hm.height(x0, y0), hm.height(x1, y0), tx),
                         std::lerp(hm.height(x0, y1), hm.height(x1, y1), tx), ty);
    };
    for (int y = s.originY; y <= s.originY + s.cellsY; ++y) {
        for (int x = s.originX; x <= s.originX + s.cellsX; ++x) {
            const int direction = layers.getFlowDirection(x, y);
            if (!layers.isRiver(x, y) || direction < 0 || direction >= 8) continue;
            if (!closedReach[size_t(y * layers.getWidth() + x)]) continue;
            const int   receiverX            = x + flowDx[size_t(direction)];
            const int   receiverY            = y + flowDy[size_t(direction)];
            const auto  startCenter          = smoothCenter(x, y);
            const auto  endCenter            = smoothCenter(receiverX, receiverY);
            const float tx0                  = endCenter[0] - startCenter[0];
            const float tz0                  = endCenter[1] - startCenter[1];
            const float tangentLengthSquared = tx0 * tx0 + tz0 * tz0;
            if (tangentLengthSquared <= 1e-8f) continue;
            const float invLength = 1.f / std::sqrt(tangentLengthSquared);
            const float tx = tx0 * invLength, tz = tz0 * invLength;
            auto        widthAt = [&](int sampleX, int sampleY) {
                // Downstream hydraulic geometry is commonly approximated by a
                // discharge power law. The 0.36 exponent retains readable head
                // streams while producing gradual main-stem widening.
                const float discharge = channelFlow[size_t(sampleY * layers.getWidth() + sampleX)];
                const float areaScale =
                    maxFlow > 1.f ? std::pow(std::clamp(discharge / maxFlow, 0.f, 1.f), 0.36f) : 0.f;
                const float orderScale =
                    std::clamp((float(layers.getStreamOrder(sampleX, sampleY)) - 1.f) / 4.f, 0.f, 1.f);
                const float flowScale = std::max(areaScale, orderScale * 0.82f);
                return 0.5f * std::lerp(s.minWidth, s.maxWidth, flowScale);
            };
            const float startWidth = widthAt(x, y);
            const float endWidth   = layers.isRiver(receiverX, receiverY) ? widthAt(receiverX, receiverY) : startWidth;
            float       startFlowX = layers.getFlowVectorX(x, y);
            float       startFlowY = layers.getFlowVectorY(x, y);
            float       endFlowX   = layers.getFlowVectorX(receiverX, receiverY);
            float       endFlowY   = layers.getFlowVectorY(receiverX, receiverY);
            if (std::hypot(startFlowX, startFlowY) < 0.1f) {
                startFlowX = tx;
                startFlowY = tz;
            }
            if (std::hypot(endFlowX, endFlowY) < 0.1f) {
                endFlowX = tx;
                endFlowY = tz;
            }
            const float chordCells = std::sqrt(tangentLengthSquared);
            auto        curvePoint = [&](float t) {
                const float t2 = t * t, t3 = t2 * t;
                const float h00 = 2.f * t3 - 3.f * t2 + 1.f;
                const float h10 = t3 - 2.f * t2 + t;
                const float h01 = -2.f * t3 + 3.f * t2;
                const float h11 = t3 - t2;
                return std::array<float, 2>{h00 * startCenter[0] + h10 * startFlowX * chordCells + h01 * endCenter[0] +
                                                h11 * endFlowX * chordCells,
                                            h00 * startCenter[1] + h10 * startFlowY * chordCells + h01 * endCenter[1] +
                                                h11 * endFlowY * chordCells};
            };
            constexpr int                                       curveSegments = 5;
            std::array<std::array<float, 2>, curveSegments + 1> curve{};
            for (int point = 0; point <= curveSegments; ++point)
                curve[size_t(point)] = curvePoint(float(point) / float(curveSegments));
            const uint32_t stripStart = uint32_t(out.getVertexCount());
            for (int point = 0; point <= curveSegments; ++point) {
                const int   previous  = std::max(0, point - 1);
                const int   following = std::min(curveSegments, point + 1);
                const float qdx       = curve[size_t(following)][0] - curve[size_t(previous)][0];
                const float qdy       = curve[size_t(following)][1] - curve[size_t(previous)][1];
                const float t         = float(point) / float(curveSegments);
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
                if (point == 0) {
                    frameX = startFlowX;
                    frameY = startFlowY;
                }
                if (point == curveSegments) {
                    frameX = endFlowX;
                    frameY = endFlowY;
                }
                float frameLength = std::hypot(frameX, frameY);
                if (frameLength < 1e-5f) {
                    frameX      = qdx;
                    frameY      = qdy;
                    frameLength = std::max(1e-5f, std::hypot(frameX, frameY));
                }
                const float qpx        = -frameY / frameLength;
                const float qpz        = frameX / frameLength;
                const float width      = std::lerp(startWidth, endWidth, t);
                const auto &q          = curve[size_t(point)];
                const float qx         = (q[0] - float(s.originX)) * s.cellSize;
                const float qz         = (q[1] - float(s.originY)) * s.cellSize;
                const float widthCells = width / s.cellSize;
                const float centerBed  = sampleHeight(q[0], q[1]);
                const float leftBed    = sampleHeight(q[0] - qpx * widthCells, q[1] - qpz * widthCells);
                const float rightBed   = sampleHeight(q[0] + qpx * widthCells, q[1] + qpz * widthCells);
                // One transverse water level must clear both banks. Sampling
                // only the center buried the uphill half of ribbons whenever
                // a smoothed centerline crossed a sloping raster triangle,
                // which appeared as broken rectangular river segments.
                const float qy = std::max({centerBed, leftBed, rightBed}) * s.heightScale + s.heightOffset;
                out.addVertex(qx - qpx * width, qy, qz - qpz * width, 0.f, 1.f, 0.f, 0.f, t);
                out.addVertex(qx + qpx * width, qy, qz + qpz * width, 0.f, 1.f, 0.f, 1.f, t);
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

bool TerrainLakeMeshBuilder::build(const Heightmap &hm, const TerrainLayers &layers, const TerrainLakeMeshSettings &s,
                                   MeshBuild &out, std::string *error) {
    if (s.cellsX <= 0 || s.cellsY <= 0 || s.originX < 0 || s.originY < 0 || s.originX + s.cellsX >= hm.getWidth() ||
        s.originY + s.cellsY >= hm.getHeight() || layers.getWidth() != hm.getWidth() ||
        layers.getHeight() != hm.getHeight() || !std::isfinite(s.cellSize) || s.cellSize <= 0.f ||
        !std::isfinite(s.heightScale) || !std::isfinite(s.minimumDepth) || s.minimumDepth < 0.f ||
        !std::isfinite(s.heightOffset)) {
        if (error) *error = "terrain lake mesh: invalid bounds or settings";
        return false;
    }
    out = {};
    struct ShoreVertex {
        float x, y, z, depth;
    };
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
                const ShoreVertex &a       = polygon[edge];
                const ShoreVertex &b       = polygon[(edge + 1) % polygon.size()];
                const bool         aInside = a.depth >= s.minimumDepth;
                const bool         bInside = b.depth >= s.minimumDepth;
                if (aInside) clipped.push_back(a);
                if (aInside == bInside) continue;
                const float t = std::clamp((s.minimumDepth - a.depth) / (b.depth - a.depth), 0.f, 1.f);
                clipped.push_back(
                    {std::lerp(a.x, b.x, t), std::lerp(a.y, b.y, t), std::lerp(a.z, b.z, t), s.minimumDepth});
            }
            if (clipped.size() < 3) continue;
            const uint32_t first = uint32_t(out.getVertexCount());
            for (const ShoreVertex &v : clipped) {
                const float u    = (v.x / (float(s.cellsX) * s.cellSize));
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

// TerrainStreaming.cpp
namespace eve::procgen {

uint64_t TerrainStreamingCache::key(int chunkX, int chunkY) {
    return uint64_t(uint32_t(chunkX)) | (uint64_t(uint32_t(chunkY)) << 32);
}

bool TerrainStreamingCache::open(const uint8_t *data, size_t size, std::string *error) {
    TerrainAsset next;
    if (!next.open(data, size, error)) return false;
    asset_ = std::move(next);
    resident_.clear();
    return true;
}

Result<void> TerrainStreamingCache::openSource(std::shared_ptr<const ITerrainArchiveSource> source) {
    TerrainAsset next;
    auto         opened = next.openSource(std::move(source));
    if (!opened) return opened;
    asset_ = std::move(next);
    resident_.clear();
    return Result<void>::success();
}

void TerrainStreamingCache::clear() { resident_.clear(); }

TerrainStreamStats TerrainStreamingCache::streamAround(int worldX, int worldY, int radius, int maxLoads,
                                                       std::string *error) {
    TerrainStreamStats stats;
    if (radius < 0 || asset_.getChunkSize() <= 0) {
        stats.resident = getResidentCount();
        return stats;
    }
    const int     chunkSize = asset_.getChunkSize();
    const int     centerX   = worldX >= 0 ? worldX / chunkSize : -((-worldX - 1) / chunkSize) - 1;
    const int     centerY   = worldY >= 0 ? worldY / chunkSize : -((-worldY - 1) / chunkSize) - 1;
    const int64_t radiusSq  = int64_t(radius) * radius;

    for (auto it = resident_.begin(); it != resident_.end();) {
        const int     cx = int32_t(uint32_t(it->first));
        const int     cy = int32_t(uint32_t(it->first >> 32));
        const int64_t dx = int64_t(cx) - centerX, dy = int64_t(cy) - centerY;
        if (dx * dx + dy * dy > radiusSq) {
            it = resident_.erase(it);
            ++stats.evicted;
        } else
            ++it;
    }

    struct Request {
        int     x, y;
        int64_t distanceSq;
    };
    std::vector<Request> requests;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            const int64_t distanceSq = int64_t(dx) * dx + int64_t(dy) * dy;
            if (distanceSq > radiusSq) continue;
            const int chunkX = centerX + dx, chunkY = centerY + dy;
            if (asset_.findChunk(chunkX, chunkY) && !getChunk(chunkX, chunkY))
                requests.push_back({chunkX, chunkY, distanceSq});
        }
    }
    std::sort(requests.begin(), requests.end(), [](const Request &a, const Request &b) {
        return std::tie(a.distanceSq, a.y, a.x) < std::tie(b.distanceSq, b.y, b.x);
    });
    const size_t loadCount = maxLoads > 0 ? std::min(requests.size(), size_t(maxLoads)) : requests.size();
    for (size_t i = 0; i < loadCount; ++i) {
        TerrainChunkData chunk;
        std::string      chunkError;
        if (!asset_.loadChunk(requests[i].x, requests[i].y, chunk, &chunkError)) {
            ++stats.failed;
            if (error && error->empty()) *error = chunkError;
            continue;
        }
        resident_.emplace(key(requests[i].x, requests[i].y), std::move(chunk));
        ++stats.loaded;
    }
    stats.pending  = int(requests.size() - loadCount);
    stats.resident = getResidentCount();
    return stats;
}

const TerrainChunkData *TerrainStreamingCache::getChunk(int chunkX, int chunkY) const {
    const auto it = resident_.find(key(chunkX, chunkY));
    return it == resident_.end() ? nullptr : &it->second;
}

bool TerrainStreamingCache::sampleCell(int worldX, int worldY, TerrainSample &out) const {
    if (worldX < 0 || worldY < 0 || worldX >= asset_.getWidth() || worldY >= asset_.getHeight()) return false;
    const int               cs = asset_.getChunkSize(), cx = worldX / cs, cy = worldY / cs;
    const TerrainChunkData *chunk = getChunk(cx, cy);
    if (!chunk) return false;
    const int lx = worldX - cx * cs, ly = worldY - cy * cs;
    if (lx >= chunk->width || ly >= chunk->height) return false;
    const size_t i       = size_t(ly) * size_t(chunk->width) + size_t(lx);
    out.height           = chunk->heights.data()[i];
    out.flowAccumulation = chunk->flowAccumulation[i];
    out.flowDirection    = int(chunk->flowDirection[i]);
    out.flowVectorX      = chunk->flowVectorX[i];
    out.flowVectorY      = chunk->flowVectorY[i];
    out.streamOrder      = int(chunk->streamOrder[i]);
    out.lakeDepth        = chunk->lakeDepth[i];
    out.lake             = out.lakeDepth > 0.001f;
    out.temperature      = chunk->temperature[i];
    out.moisture         = chunk->moisture[i];
    out.river            = chunk->rivers[i] != 0;
    out.biome            = chunk->biomes[i];
    return true;
}

bool TerrainStreamingCache::getReceiver(int worldX, int worldY, int &receiverX, int &receiverY) const {
    static constexpr int dx[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
    static constexpr int dy[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
    TerrainSample        source;
    if (!sampleCell(worldX, worldY, source) || source.flowDirection < 0 || source.flowDirection >= 8) return false;
    receiverX = worldX + dx[source.flowDirection];
    receiverY = worldY + dy[source.flowDirection];
    TerrainSample receiver;
    return sampleCell(receiverX, receiverY, receiver);
}

bool TerrainStreamingCache::traceFlow(int worldX, int worldY, int maxSteps,
                                      std::vector<std::pair<int, int>> &out) const {
    out.clear();
    TerrainSample start;
    if (maxSteps <= 0 || !sampleCell(worldX, worldY, start)) return false;
    out.emplace_back(worldX, worldY);
    for (int step = 0; step < maxSteps; ++step) {
        TerrainSample current;
        if (!sampleCell(worldX, worldY, current)) return false;
        if (current.flowDirection < 0) return true;
        int nextX = 0, nextY = 0;
        if (!getReceiver(worldX, worldY, nextX, nextY)) return false;
        if (std::find(out.begin(), out.end(), std::pair<int, int>{nextX, nextY}) != out.end()) return false;
        out.emplace_back(nextX, nextY);
        worldX = nextX;
        worldY = nextY;
    }
    TerrainSample current;
    return sampleCell(worldX, worldY, current) && current.flowDirection < 0;
}

bool TerrainStreamingCache::buildWindow(int originX, int originY, int width, int height,
                                        TerrainStreamingWindow &out) const {
    if (width <= 0 || height <= 0 || originX < 0 || originY < 0 || int64_t(originX) + width > asset_.getWidth() ||
        int64_t(originY) + height > asset_.getHeight())
        return false;
    TerrainStreamingWindow next;
    next.originX          = originX;
    next.originY          = originY;
    next.heights          = Heightmap(width, height);
    next.hydrology.width  = width;
    next.hydrology.height = height;
    next.climate.width    = width;
    next.climate.height   = height;
    const size_t count    = size_t(width) * size_t(height);
    next.hydrology.flowDirection.resize(count);
    next.hydrology.flowVectorX.resize(count);
    next.hydrology.flowVectorY.resize(count);
    next.hydrology.flowAccumulation.resize(count);
    next.hydrology.lakeDepth.resize(count);
    next.hydrology.rivers.resize(count);
    next.hydrology.streamOrder.resize(count);
    next.climate.temperature.resize(count);
    next.climate.moisture.resize(count);
    next.climate.biomes.resize(count);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            TerrainSample sample;
            if (!sampleCell(originX + x, originY + y, sample)) return false;
            const size_t i = size_t(y) * size_t(width) + size_t(x);
            next.heights.setHeight(x, y, sample.height);
            next.hydrology.flowDirection[i]    = int8_t(sample.flowDirection);
            next.hydrology.flowVectorX[i]      = sample.flowVectorX;
            next.hydrology.flowVectorY[i]      = sample.flowVectorY;
            next.hydrology.flowAccumulation[i] = sample.flowAccumulation;
            next.hydrology.lakeDepth[i]        = sample.lakeDepth;
            next.hydrology.rivers[i]           = uint8_t(sample.river);
            next.hydrology.streamOrder[i]      = uint8_t(sample.streamOrder);
            next.climate.temperature[i]        = sample.temperature;
            next.climate.moisture[i]           = sample.moisture;
            next.climate.biomes[i]             = sample.biome;
        }
    out = std::move(next);
    return true;
}

bool TerrainStreamingCache::sampleHeight(float worldX, float worldY, float &out) const {
    if (!std::isfinite(worldX) || !std::isfinite(worldY) || worldX < 0.f || worldY < 0.f ||
        worldX > float(asset_.getWidth() - 1) || worldY > float(asset_.getHeight() - 1))
        return false;
    const int     x0 = int(std::floor(worldX)), y0 = int(std::floor(worldY));
    const int     x1 = std::min(x0 + 1, asset_.getWidth() - 1), y1 = std::min(y0 + 1, asset_.getHeight() - 1);
    TerrainSample a, b, c, d;
    if (!sampleCell(x0, y0, a) || !sampleCell(x1, y0, b) || !sampleCell(x0, y1, c) || !sampleCell(x1, y1, d))
        return false;
    const float tx = worldX - float(x0), ty = worldY - float(y0);
    const float top    = a.height + (b.height - a.height) * tx;
    const float bottom = c.height + (d.height - c.height) * tx;
    out                = top + (bottom - top) * ty;
    return true;
}

}  // namespace eve::procgen

// TerrainTerraceRemover.cpp
namespace eve::procgen {
namespace {
bool valid(const TerrainTerraceRemovalSettings &s) {
    const std::array<float, 7> values = {s.perlinScale,
                                         s.perlinStrength,
                                         s.slopeTerraceThreshold,
                                         s.flatThreshold,
                                         s.verticalGradientThreshold,
                                         s.minimumTerraceThreshold,
                                         s.maximumTerraceThreshold};
    return std::all_of(values.begin(), values.end(), [](float value) { return std::isfinite(value) && value >= 0; }) &&
           s.minimumTerraceThreshold <= s.maximumTerraceThreshold && s.noiseSeed >= 0;
}
bool matching(const Heightmap &a, const Heightmap &b) {
    return raster_detail::validRaster(a) && raster_detail::validRaster(b) && a.getWidth() == b.getWidth() &&
           a.getHeight() == b.getHeight();
}
}  // namespace

Result<int> analyzeTerrainTerraces(Heightmap &target, const Heightmap &source, const TerrainTerraceRemovalSettings &s) {
    using namespace raster_detail;
    if (&target == &source || !matching(target, source) || !valid(s))
        return invalid(
            "terrain.terraceRemoval.analyze: distinct matching finite rasters and valid thresholds required");
    const int          width = source.getWidth(), height = source.getHeight();
    std::vector<float> output(size_t(width) * height, float(TerrainTerraceClass::Black));
    auto               at = [width](int x, int y) { return size_t(y) * width + x; };
    for (int y = 1; y < height - 1; ++y)
        for (int x = 1; x < width - 1; ++x) {
            const float current = source.data()[at(x, y)];
            const float gradientX =
                std::abs(current - source.data()[at(x - 1, y)]) + std::abs(current - source.data()[at(x + 1, y)]);
            const float gradientY =
                std::abs(current - source.data()[at(x, y - 1)]) + std::abs(current - source.data()[at(x, y + 1)]);
            const float gradient = gradientX + gradientY;
            const float vertical = std::abs(source.data()[at(x, y - 1)] - source.data()[at(x, y + 1)]);
            bool        slope    = false;
            if (gradient < s.slopeTerraceThreshold && vertical < s.verticalGradientThreshold)
                slope = std::abs(source.data()[at(x - 1, y)] - current) < s.slopeTerraceThreshold &&
                        std::abs(source.data()[at(x + 1, y)] - current) < s.slopeTerraceThreshold &&
                        std::abs(source.data()[at(x, y - 1)] - current) < s.slopeTerraceThreshold &&
                        std::abs(source.data()[at(x, y + 1)] - current) < s.slopeTerraceThreshold;
            const bool flat     = gradient < s.flatThreshold;
            const bool mountain = vertical > s.verticalGradientThreshold;
            const bool terrace  = vertical >= s.minimumTerraceThreshold && vertical <= s.maximumTerraceThreshold;
            output[at(x, y)]    = float(terrace    ? TerrainTerraceClass::Green
                                        : flat     ? TerrainTerraceClass::Black
                                        : mountain ? TerrainTerraceClass::Red
                                        : slope    ? TerrainTerraceClass::Blue
                                                   : TerrainTerraceClass::Black);
        }
    return publish(target, std::move(output));
}

Result<int> removeTerrainTerraces(Heightmap &target, const Heightmap &source, const Heightmap &classes,
                                  const TerrainTerraceRemovalSettings &s) {
    using namespace raster_detail;
    if (&target == &classes || !matching(target, source) || !matching(source, classes) || !valid(s))
        return invalid(
            "terrain.terraceRemoval.apply: matching finite rasters, distinct class output and valid settings required");
    const int                  width = source.getWidth(), height = source.getHeight();
    const auto                 original = source.data();
    auto                       index    = [width](int x, int y) { return size_t(y) * width + x; };
    std::vector<unsigned char> mask(original.size(), 0);
    for (int y = 1; y < height - 1; ++y)
        for (int x = 1; x < width - 1; ++x) {
            const float category = classes.data()[index(x, y)];
            if ((s.excludeRed && category == float(TerrainTerraceClass::Red)) ||
                (s.excludeBlack && category == float(TerrainTerraceClass::Black)))
                continue;
            const float current = original[index(x, y)];
            mask[index(x, y)]   = std::abs(current - original[index(x + 1, y)]) < 0.01F &&
                                std::abs(current - original[index(x, y + 1)]) < 0.01F;
        }
    auto smooth = original;
    for (int y = 1; y < height - 1; ++y)
        for (int x = 1; x < width - 1; ++x)
            if (mask[index(x, y)]) {
                double sum = 0;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) sum += original[index(x + dx, y + dy)];
                smooth[index(x, y)] = float(sum / 9.0);
            }
    auto        noisy = smooth;
    NoiseField  noise{static_cast<unsigned int>(s.noiseSeed)};
    const float workflowScale = s.terrainWorkflow ? 0.00042F / 0.0035F : 1.F;
    for (int y = 1; y < height - 1; ++y)
        for (int x = 1; x < width - 1; ++x)
            if (mask[index(x, y)]) {
                const float dx       = std::abs(smooth[index(x + 1, y)] - smooth[index(x - 1, y)]);
                const float dy       = std::abs(smooth[index(x, y + 1)] - smooth[index(x, y - 1)]);
                const float gradient = std::sqrt(dx * dx + dy * dy);
                const float strength = s.perlinStrength * workflowScale * (1.F - std::clamp(gradient * 10.F, 0.F, 1.F));
                const double value =
                    double(smooth[index(x, y)]) + noise.perlinNoise(x * s.perlinScale, y * s.perlinScale) * strength;
                if (!isRepresentable(value))
                    return invalid("terrain.terraceRemoval.apply: noise result exceeds float range");
                noisy[index(x, y)] = float(value);
            }
    auto          directional   = noisy;
    constexpr int weights[3][3] = {{1, 2, 1}, {2, 4, 2}, {1, 2, 1}};
    for (int y = 1; y < height - 1; ++y)
        for (int x = 1; x < width - 1; ++x) {
            double sum = 0;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) sum += noisy[index(x + dx, y + dy)] * weights[dy + 1][dx + 1];
            directional[index(x, y)] = float(sum / 16.0);
        }
    std::array<double, 25> kernel{};
    double                 kernelSum = 0;
    for (int ky = -2; ky <= 2; ++ky)
        for (int kx = -2; kx <= 2; ++kx) {
            const double weight                 = std::exp(-(kx * kx + ky * ky) / 2.0);
            kernel[size_t(ky + 2) * 5 + kx + 2] = weight;
            kernelSum += weight;
        }
    auto output = directional;
    for (int y = 2; y < height - 2; ++y)
        for (int x = 2; x < width - 2; ++x) {
            double sum = 0;
            for (int ky = -2; ky <= 2; ++ky)
                for (int kx = -2; kx <= 2; ++kx)
                    sum += directional[index(x + kx, y + ky)] * kernel[size_t(ky + 2) * 5 + kx + 2];
            const double value = sum / kernelSum;
            if (!isRepresentable(value))
                return invalid("terrain.terraceRemoval.apply: filtered result exceeds float range");
            output[index(x, y)] = float(value);
        }
    return publish(target, std::move(output));
}
}  // namespace eve::procgen
