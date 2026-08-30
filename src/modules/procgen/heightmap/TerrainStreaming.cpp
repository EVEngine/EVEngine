#include "procgen/heightmap/TerrainStreaming.h"

#include <algorithm>
#include <cmath>
#include <tuple>
#include <vector>

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

void TerrainStreamingCache::clear() { resident_.clear(); }

TerrainStreamStats TerrainStreamingCache::streamAround(int worldX, int worldY, int radius,
                                                        int maxLoads, std::string *error) {
    TerrainStreamStats stats;
    if (radius < 0 || asset_.getChunkSize() <= 0) { stats.resident = getResidentCount(); return stats; }
    const int chunkSize = asset_.getChunkSize();
    const int centerX = worldX >= 0 ? worldX / chunkSize : -((-worldX - 1) / chunkSize) - 1;
    const int centerY = worldY >= 0 ? worldY / chunkSize : -((-worldY - 1) / chunkSize) - 1;
    const int64_t radiusSq = int64_t(radius) * radius;

    for (auto it = resident_.begin(); it != resident_.end();) {
        const int cx = int32_t(uint32_t(it->first));
        const int cy = int32_t(uint32_t(it->first >> 32));
        const int64_t dx = int64_t(cx) - centerX, dy = int64_t(cy) - centerY;
        if (dx * dx + dy * dy > radiusSq) { it = resident_.erase(it); ++stats.evicted; }
        else ++it;
    }

    struct Request { int x, y; int64_t distanceSq; };
    std::vector<Request> requests;
    for (const TerrainChunkEntry &entry : asset_.chunks()) {
        const int64_t dx = int64_t(entry.chunkX) - centerX, dy = int64_t(entry.chunkY) - centerY;
        const int64_t distanceSq = dx * dx + dy * dy;
        if (distanceSq <= radiusSq && !getChunk(entry.chunkX, entry.chunkY))
            requests.push_back({entry.chunkX, entry.chunkY, distanceSq});
    }
    std::sort(requests.begin(), requests.end(), [](const Request &a, const Request &b) {
        return std::tie(a.distanceSq, a.y, a.x) < std::tie(b.distanceSq, b.y, b.x);
    });
    const size_t loadCount = maxLoads > 0 ? std::min(requests.size(), size_t(maxLoads)) : requests.size();
    for (size_t i = 0; i < loadCount; ++i) {
        TerrainChunkData chunk;
        std::string chunkError;
        if (!asset_.loadChunk(requests[i].x, requests[i].y, chunk, &chunkError)) {
            ++stats.failed;
            if (error && error->empty()) *error = chunkError;
            continue;
        }
        resident_.emplace(key(requests[i].x, requests[i].y), std::move(chunk));
        ++stats.loaded;
    }
    stats.pending = int(requests.size() - loadCount);
    stats.resident = getResidentCount();
    return stats;
}

const TerrainChunkData *TerrainStreamingCache::getChunk(int chunkX, int chunkY) const {
    const auto it = resident_.find(key(chunkX, chunkY));
    return it == resident_.end() ? nullptr : &it->second;
}

bool TerrainStreamingCache::sampleCell(int worldX, int worldY, TerrainSample &out) const {
    if (worldX < 0 || worldY < 0 || worldX >= asset_.getWidth() || worldY >= asset_.getHeight()) return false;
    const int cs = asset_.getChunkSize(), cx = worldX / cs, cy = worldY / cs;
    const TerrainChunkData *chunk = getChunk(cx, cy);
    if (!chunk) return false;
    const int lx = worldX - cx * cs, ly = worldY - cy * cs;
    if (lx >= chunk->width || ly >= chunk->height) return false;
    const size_t i = size_t(ly) * size_t(chunk->width) + size_t(lx);
    out.height = chunk->heights.data()[i]; out.flowAccumulation = chunk->flowAccumulation[i];
    out.flowDirection = int(chunk->flowDirection[i]);
    out.flowVectorX = chunk->flowVectorX[i]; out.flowVectorY = chunk->flowVectorY[i];
    out.streamOrder = int(chunk->streamOrder[i]);
    out.lakeDepth = chunk->lakeDepth[i]; out.lake = out.lakeDepth > 0.001f;
    out.temperature = chunk->temperature[i]; out.moisture = chunk->moisture[i];
    out.river = chunk->rivers[i] != 0; out.biome = chunk->biomes[i];
    return true;
}

bool TerrainStreamingCache::getReceiver(int worldX, int worldY, int &receiverX,
                                         int &receiverY) const {
    static constexpr int dx[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
    static constexpr int dy[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
    TerrainSample source;
    if (!sampleCell(worldX, worldY, source) || source.flowDirection < 0 ||
        source.flowDirection >= 8) return false;
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
        if (std::find(out.begin(), out.end(), std::pair<int, int>{nextX, nextY}) != out.end())
            return false;
        out.emplace_back(nextX, nextY);
        worldX = nextX; worldY = nextY;
    }
    TerrainSample current;
    return sampleCell(worldX, worldY, current) && current.flowDirection < 0;
}

bool TerrainStreamingCache::buildWindow(int originX, int originY, int width, int height,
                                        TerrainStreamingWindow &out) const {
    if (width <= 0 || height <= 0 || originX < 0 || originY < 0 ||
        int64_t(originX) + width > asset_.getWidth() ||
        int64_t(originY) + height > asset_.getHeight()) return false;
    TerrainStreamingWindow next;
    next.originX = originX; next.originY = originY;
    next.heights = Heightmap(width, height);
    next.hydrology.width = width; next.hydrology.height = height;
    next.climate.width = width; next.climate.height = height;
    const size_t count = size_t(width) * size_t(height);
    next.hydrology.flowDirection.resize(count);
    next.hydrology.flowVectorX.resize(count); next.hydrology.flowVectorY.resize(count);
    next.hydrology.flowAccumulation.resize(count); next.hydrology.lakeDepth.resize(count);
    next.hydrology.rivers.resize(count); next.hydrology.streamOrder.resize(count);
    next.climate.temperature.resize(count); next.climate.moisture.resize(count);
    next.climate.biomes.resize(count);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        TerrainSample sample;
        if (!sampleCell(originX + x, originY + y, sample)) return false;
        const size_t i = size_t(y) * size_t(width) + size_t(x);
        next.heights.setHeight(x, y, sample.height);
        next.hydrology.flowDirection[i] = int8_t(sample.flowDirection);
        next.hydrology.flowVectorX[i] = sample.flowVectorX;
        next.hydrology.flowVectorY[i] = sample.flowVectorY;
        next.hydrology.flowAccumulation[i] = sample.flowAccumulation;
        next.hydrology.lakeDepth[i] = sample.lakeDepth;
        next.hydrology.rivers[i] = uint8_t(sample.river);
        next.hydrology.streamOrder[i] = uint8_t(sample.streamOrder);
        next.climate.temperature[i] = sample.temperature;
        next.climate.moisture[i] = sample.moisture;
        next.climate.biomes[i] = sample.biome;
    }
    out = std::move(next);
    return true;
}

bool TerrainStreamingCache::sampleHeight(float worldX, float worldY, float &out) const {
    if (!std::isfinite(worldX) || !std::isfinite(worldY) || worldX < 0.f || worldY < 0.f ||
        worldX > float(asset_.getWidth() - 1) || worldY > float(asset_.getHeight() - 1)) return false;
    const int x0 = int(std::floor(worldX)), y0 = int(std::floor(worldY));
    const int x1 = std::min(x0 + 1, asset_.getWidth() - 1), y1 = std::min(y0 + 1, asset_.getHeight() - 1);
    TerrainSample a, b, c, d;
    if (!sampleCell(x0, y0, a) || !sampleCell(x1, y0, b) || !sampleCell(x0, y1, c) || !sampleCell(x1, y1, d)) return false;
    const float tx = worldX - float(x0), ty = worldY - float(y0);
    const float top = a.height + (b.height - a.height) * tx;
    const float bottom = c.height + (d.height - c.height) * tx;
    out = top + (bottom - top) * ty;
    return true;
}

}  // namespace eve::procgen
