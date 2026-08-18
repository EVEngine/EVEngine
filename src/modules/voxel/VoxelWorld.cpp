#include "voxel/VoxelWorld.h"

#include "data/ByteData.h"
#include "graphics/Graphics.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <thread>

namespace eve::voxel {

namespace {
// Cap remesh worker count: enough to parallelize chunk meshing without
// oversubscribing small devices / browser pthread pools.
constexpr int kMaxRemeshWorkers = 4;
}  // namespace

Chunk *VoxelWorld::getOrCreateChunk(int cx, int cy, int cz) {
    const uint64_t k = key(cx, cy, cz);
    auto it = chunks_.find(k);
    if (it != chunks_.end()) return it->second.get();
    auto chunk = std::make_unique<Chunk>(cx, cy, cz);
    Chunk *raw = chunk.get();
    chunks_.emplace(k, std::move(chunk));
    return raw;
}

Chunk *VoxelWorld::getChunk(int cx, int cy, int cz) {
    auto it = chunks_.find(key(cx, cy, cz));
    return it == chunks_.end() ? nullptr : it->second.get();
}

const Chunk *VoxelWorld::getChunk(int cx, int cy, int cz) const {
    auto it = chunks_.find(key(cx, cy, cz));
    return it == chunks_.end() ? nullptr : it->second.get();
}

bool VoxelWorld::hasChunk(int cx, int cy, int cz) const {
    return chunks_.find(key(cx, cy, cz)) != chunks_.end();
}

void VoxelWorld::removeChunk(int cx, int cy, int cz) { chunks_.erase(key(cx, cy, cz)); }

void VoxelWorld::clear() {
    chunks_.clear();
    visible_.clear();
    visibleChunkKeys_.clear();
}

int VoxelWorld::unloadChunksOutside(int centerX, int centerY, int centerZ, int radiusChunks) {
    if (radiusChunks < 0) return 0;
    const int64_t r2 = int64_t(radiusChunks) * int64_t(radiusChunks);
    std::vector<uint64_t> evict;
    evict.reserve(chunks_.size() / 4);
    for (auto &kv : chunks_) {
        int cx, cy, cz;
        unpackKey(kv.first, cx, cy, cz);
        const int64_t dx = int64_t(cx) - centerX;
        const int64_t dy = int64_t(cy) - centerY;
        const int64_t dz = int64_t(cz) - centerZ;
        if (dx * dx + dy * dy + dz * dz > r2) evict.push_back(kv.first);
    }
    for (uint64_t k : evict) chunks_.erase(k);
    if (!evict.empty()) {
        // Batch pointers may dangle after eviction; force re-selection.
        visible_.clear();
        visibleChunkKeys_.clear();
    }
    return int(evict.size());
}

StreamStats VoxelWorld::streamAround(int centerX, int centerY, int centerZ, int radiusChunks,
                                     const std::function<void(Chunk &, int, int, int)> &generator) {
    StreamStats stats;
    if (radiusChunks < 0) return stats;

    // Evict first so far-away dirty chunks are not remeshed below.
    stats.evicted = unloadChunksOutside(centerX, centerY, centerZ, radiusChunks);

    const int64_t r2 = int64_t(radiusChunks) * int64_t(radiusChunks);
    for (int dz = -radiusChunks; dz <= radiusChunks; ++dz)
        for (int dy = -radiusChunks; dy <= radiusChunks; ++dy)
            for (int dx = -radiusChunks; dx <= radiusChunks; ++dx) {
                const int64_t d2 = int64_t(dx) * dx + int64_t(dy) * dy + int64_t(dz) * dz;
                if (d2 > r2) continue;
                const int nx = centerX + dx;
                const int ny = centerY + dy;
                const int nz = centerZ + dz;
                if (hasChunk(nx, ny, nz)) continue;
                Chunk *c = getOrCreateChunk(nx, ny, nz);
                if (generator)
                    generator(*c, nx, ny, nz);
                else if (terrainEnabled_) {
                    // Heightmap terrain via procgen::TerrainSampler: one column
                    // per (x, z); sampling world coords keeps chunk seams flush.
                    const int wy0 = ny * kChunkSize;
                    for (int lz = 0; lz < kChunkSize; ++lz)
                        for (int lx = 0; lx < kChunkSize; ++lx) {
                            const int h = terrainHeightAt(nx * kChunkSize + lx,
                                                          nz * kChunkSize + lz);
                            for (int ly = 0; ly < kChunkSize; ++ly) {
                                const int wy = wy0 + ly;
                                if (wy <= h - 4)
                                    c->set(lx, ly, lz, terrainStone_);
                                else if (wy <= h - 1)
                                    c->set(lx, ly, lz, terrainSub_);
                                else if (wy == h)
                                    c->set(lx, ly, lz, terrainTop_);
                            }
                        }
                }
                ++stats.created;
            }

    if (stats.created > 0) remeshDirty();
    return stats;
}

namespace {

void putU32(std::vector<uint8_t> &out, uint32_t v) {
    out.push_back(uint8_t(v));
    out.push_back(uint8_t(v >> 8));
    out.push_back(uint8_t(v >> 16));
    out.push_back(uint8_t(v >> 24));
}

void putI32(std::vector<uint8_t> &out, int32_t v) { putU32(out, uint32_t(v)); }

bool getU32(const uint8_t *&p, const uint8_t *end, uint32_t &out) {
    if (end - p < 4) return false;
    out = uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
          (uint32_t(p[3]) << 24);
    p += 4;
    return true;
}

bool getI32(const uint8_t *&p, const uint8_t *end, int32_t &out) {
    uint32_t v;
    if (!getU32(p, end, v)) return false;
    out = int32_t(v);
    return true;
}

}  // namespace

void VoxelWorld::serializeWorld(std::vector<uint8_t> &out) const {
    out.clear();
    const char magic[4] = {'E', 'V', 'V', 'X'};
    out.insert(out.end(), magic, magic + 4);
    out.push_back(1);  // version
    putU32(out, uint32_t(chunks_.size()));

    // Deterministic output: sort chunk keys so saves are byte-stable.
    std::vector<uint64_t> keys;
    keys.reserve(chunks_.size());
    for (auto &kv : chunks_) keys.push_back(kv.first);
    std::sort(keys.begin(), keys.end());
    for (uint64_t key : keys) {
        const auto &chunk = chunks_.at(key);
        int cx, cy, cz;
        unpackKey(key, cx, cy, cz);
        putI32(out, int32_t(cx));
        putI32(out, int32_t(cy));
        putI32(out, int32_t(cz));
        const uint8_t *raw = chunk->rawVoxels();
        out.insert(out.end(), raw, raw + kChunkSize * kChunkSize * kChunkSize);
    }
}

bool VoxelWorld::deserializeWorld(const uint8_t *data, size_t size) {
    const uint8_t *p = data;
    const uint8_t *end = data + size;
    if (size < 9 || std::memcmp(p, "EVVX", 4) != 0 || p[4] != 1) return false;
    p += 5;
    uint32_t count = 0;
    if (!getU32(p, end, count)) return false;
    const size_t voxelBytes = size_t(kChunkSize) * kChunkSize * kChunkSize;
    if (uint64_t(count) > (uint64_t(end - p)) / (12 + voxelBytes)) return false;

    clear();
    for (uint32_t i = 0; i < count; ++i) {
        int32_t cx = 0, cy = 0, cz = 0;
        if (!getI32(p, end, cx) || !getI32(p, end, cy) || !getI32(p, end, cz)) return false;
        if (size_t(end - p) < voxelBytes) return false;
        Chunk *c = getOrCreateChunk(cx, cy, cz);
        c->setVoxelData(p);
        p += voxelBytes;
    }
    return true;
}

data::ByteData *VoxelWorld::saveWorld() const {
    std::vector<uint8_t> bytes;
    serializeWorld(bytes);
    return new data::ByteData(bytes.data(), bytes.size());
}

bool VoxelWorld::loadWorld(data::ByteData *bytes) {
    if (!bytes) return false;
    return deserializeWorld(static_cast<const uint8_t *>(bytes->getData()), bytes->getSize());
}

int VoxelWorld::remeshDirty(int maxThreads) {
    std::vector<Chunk *> dirty;
    dirty.reserve(chunks_.size());
    for (auto &kv : chunks_) {
        if (kv.second->isDirty()) dirty.push_back(kv.second.get());
    }
    const int count = int(dirty.size());
    if (count == 0) return 0;

    const auto remeshOne = [this](Chunk *c) {
        c->remesh(types_, &VoxelWorld::chunkNeighborSampler, this);
    };

    int workers = maxThreads;
    if (workers <= 0) {
        workers = static_cast<int>(std::thread::hardware_concurrency());
        if (workers < 1) workers = 1;
        if (workers > kMaxRemeshWorkers) workers = kMaxRemeshWorkers;
    }

    if (workers <= 1 || count <= 1) {
        for (Chunk *c : dirty) remeshOne(c);
        return count;
    }

    // Parallel remesh: each worker remeshes its own slice of distinct chunks.
    // The sampler only reads the chunk map (no concurrent mutation), and each
    // chunk is touched by exactly one thread, so this is safe. The main thread
    // takes the remainder slice, then joins.
    std::vector<std::thread> threads;
    int next = 0;
    const int perWorker = (count + workers - 1) / workers;
    try {
        threads.reserve(size_t(workers - 1));
        for (int i = 0; i < workers - 1 && next < count; ++i) {
            const int begin = next;
            const int end = std::min(count, begin + perWorker);
            next = end;
            threads.emplace_back([this, &dirty, &remeshOne, begin, end] {
                for (int k = begin; k < end; ++k) remeshOne(dirty[size_t(k)]);
            });
        }
    } catch (...) {
        // Thread creation failed (resource limits): join what we have and
        // finish everything serially. Remesh is idempotent, so any chunks the
        // created workers already handled are simply done twice.
        for (auto &w : threads) {
            if (w.joinable()) w.join();
        }
        threads.clear();
        for (Chunk *c : dirty) remeshOne(c);
        return count;
    }
    for (int k = next; k < count; ++k) remeshOne(dirty[size_t(k)]);
    for (auto &w : threads) {
        if (w.joinable()) w.join();
    }
    return count;
}

void VoxelWorld::selectVisible(const float *viewProj16, float eyeX, float eyeY, float eyeZ,
                               float viewRange, bool faceCull) {
    visible_.clear();
    visibleChunkKeys_.clear();
    if (!viewProj16) return;

    const Frustum frustum = Frustum::fromViewProjColumnMajor(viewProj16);
    const float rangeSq = viewRange > 0.f ? viewRange * viewRange : 0.f;

    for (auto &kv : chunks_) {
        Chunk *chunk = kv.second.get();

        float minX, minY, minZ, maxX, maxY, maxZ;
        chunk->worldAABB(minX, minY, minZ, maxX, maxY, maxZ);
        const float cx = (minX + maxX) * 0.5f;
        const float cy = (minY + maxY) * 0.5f;
        const float cz = (minZ + maxZ) * 0.5f;

        if (viewRange > 0.f) {
            const float dx = cx - eyeX;
            const float dy = cy - eyeY;
            const float dz = cz - eyeZ;
            if (dx * dx + dy * dy + dz * dz > rangeSq) continue;
        }

        if (!frustum.intersectsAABB(minX, minY, minZ, maxX, maxY, maxZ)) continue;

        // Mesh only after range/frustum culling so edits far outside the view
        // are not remeshed every frame.
        chunk->ensureMeshed(types_, &VoxelWorld::chunkNeighborSampler, this);

        visibleChunkKeys_.push_back(kv.first);

        const float toCamX = eyeX - cx;
        const float toCamY = eyeY - cy;
        const float toCamZ = eyeZ - cz;
        for (int i = 0; i < faceDirCount(); ++i) {
            const FaceDir dir = FaceDir(i);
            const int count = chunk->faceRectCount(dir);
            if (count <= 0) continue;

            if (faceCull) {
                float nx, ny, nz;
                faceNormal(dir, nx, ny, nz);
                if (nx * toCamX + ny * toCamY + nz * toCamZ <= 0.f) continue;
            }

            DrawBatch batch;
            batch.chunk = chunk;
            batch.dir = dir;
            batch.packed = chunk->facePackedData(dir);
            batch.ao = chunk->faceAOPackedData(dir);
            batch.count = count;
            visible_.push_back(batch);
        }
    }
}

void VoxelWorld::getVisibleChunkCoord(int index, int &cx, int &cy, int &cz) const {
    if (index < 0 || index >= int(visibleChunkKeys_.size())) {
        cx = cy = cz = 0;
        return;
    }
    unpackKey(visibleChunkKeys_[size_t(index)], cx, cy, cz);
}

int VoxelWorld::getVisibleRectCount() const {
    int n = 0;
    for (const auto &b : visible_) n += b.count;
    return n;
}

void VoxelWorld::drawVisible(graphics::Graphics *gfx, graphics::Texture *atlas, int tilesPerRow) {
    if (!gfx) return;
    for (const auto &b : visible_) {
        if (!b.chunk || !b.packed || b.count <= 0) continue;
        gfx->drawVoxelFaceInstances(b.packed, b.count, b.chunk->originX(), b.chunk->originY(),
                                    b.chunk->originZ(), faceDirName(b.dir), atlas, tilesPerRow,
                                    b.ao);
    }
}

uint8_t VoxelWorld::getVoxel(int wx, int wy, int wz) const {
    const int cx = floorDiv(wx);
    const int cy = floorDiv(wy);
    const int cz = floorDiv(wz);
    const Chunk *c = getChunk(cx, cy, cz);
    if (!c) return 0;
    return c->get(wx - cx * kChunkSize, wy - cy * kChunkSize, wz - cz * kChunkSize);
}

void VoxelWorld::setVoxel(int wx, int wy, int wz, uint8_t texId) {
    const int cx = floorDiv(wx);
    const int cy = floorDiv(wy);
    const int cz = floorDiv(wz);
    const int lx = wx - cx * kChunkSize;
    const int ly = wy - cy * kChunkSize;
    const int lz = wz - cz * kChunkSize;

    Chunk *c = getChunk(cx, cy, cz);
    if (texId == 0) {
        // Clearing an unallocated chunk is a no-op (air needs no storage).
        if (!c) return;
        c->set(lx, ly, lz, 0);
    } else {
        if (!c) c = getOrCreateChunk(cx, cy, cz);
        c->set(lx, ly, lz, texId);
    }
    markNeighborChunksDirty(cx, cy, cz, lx, ly, lz);
}

void VoxelWorld::markNeighborChunksDirty(int cx, int cy, int cz, int lx, int ly, int lz) {
    auto mark = [this](int nx, int ny, int nz) {
        if (Chunk *n = getChunk(nx, ny, nz)) n->markDirty();
    };
    if (lx == 0) mark(cx - 1, cy, cz);
    if (lx == kChunkSize - 1) mark(cx + 1, cy, cz);
    if (ly == 0) mark(cx, cy - 1, cz);
    if (ly == kChunkSize - 1) mark(cx, cy + 1, cz);
    if (lz == 0) mark(cx, cy, cz - 1);
    if (lz == kChunkSize - 1) mark(cx, cy, cz + 1);
}

uint8_t VoxelWorld::chunkNeighborSampler(void *userData, int chunkX, int chunkY, int chunkZ,
                                         int localX, int localY, int localZ) {
    const auto *self = static_cast<const VoxelWorld *>(userData);
    const int wx = chunkX * kChunkSize + localX;
    const int wy = chunkY * kChunkSize + localY;
    const int wz = chunkZ * kChunkSize + localZ;
    return self->getVoxel(wx, wy, wz);
}

bool VoxelWorld::raycast(float ox, float oy, float oz, float dx, float dy, float dz,
                         float maxDist, int &hitX, int &hitY, int &hitZ, int &prevX,
                         int &prevY, int &prevZ, int &faceX, int &faceY, int &faceZ) const {
    const float lenSq = dx * dx + dy * dy + dz * dz;
    if (lenSq <= 1e-12f || maxDist <= 0.f) return false;
    const float invLen = 1.f / std::sqrt(lenSq);
    const float rx = dx * invLen;
    const float ry = dy * invLen;
    const float rz = dz * invLen;

    int ix = int(std::floor(ox));
    int iy = int(std::floor(oy));
    int iz = int(std::floor(oz));

    if (getVoxel(ix, iy, iz) != 0) {
        hitX = prevX = ix;
        hitY = prevY = iy;
        hitZ = prevZ = iz;
        faceX = faceY = faceZ = 0;
        return true;
    }

    const float inf = std::numeric_limits<float>::infinity();
    const int stepX = rx > 0.f ? 1 : -1;
    const int stepY = ry > 0.f ? 1 : -1;
    const int stepZ = rz > 0.f ? 1 : -1;
    const float absInvX = rx != 0.f ? std::fabs(1.f / rx) : inf;
    const float absInvY = ry != 0.f ? std::fabs(1.f / ry) : inf;
    const float absInvZ = rz != 0.f ? std::fabs(1.f / rz) : inf;
    float tMaxX = rx != 0.f ? (rx > 0.f ? float(ix + 1) - ox : ox - float(ix)) * absInvX : inf;
    float tMaxY = ry != 0.f ? (ry > 0.f ? float(iy + 1) - oy : oy - float(iy)) * absInvY : inf;
    float tMaxZ = rz != 0.f ? (rz > 0.f ? float(iz + 1) - oz : oz - float(iz)) * absInvZ : inf;

    prevX = ix;
    prevY = iy;
    prevZ = iz;
    faceX = faceY = faceZ = 0;

    constexpr int kMaxSteps = 4096;
    for (int iter = 0; iter < kMaxSteps; ++iter) {
        float t;
        if (tMaxX <= tMaxY && tMaxX <= tMaxZ) {
            ix += stepX;
            t = tMaxX;
            tMaxX += absInvX;
            faceX = -stepX;
            faceY = 0;
            faceZ = 0;
        } else if (tMaxY <= tMaxZ) {
            iy += stepY;
            t = tMaxY;
            tMaxY += absInvY;
            faceX = 0;
            faceY = -stepY;
            faceZ = 0;
        } else {
            iz += stepZ;
            t = tMaxZ;
            tMaxZ += absInvZ;
            faceX = 0;
            faceY = 0;
            faceZ = -stepZ;
        }
        if (t > maxDist) return false;
        if (getVoxel(ix, iy, iz) != 0) {
            hitX = ix;
            hitY = iy;
            hitZ = iz;
            return true;
        }
        prevX = ix;
        prevY = iy;
        prevZ = iz;
    }
    return false;
}

bool VoxelWorld::raycastScript(float ox, float oy, float oz, float dx, float dy, float dz,
                               float maxDist) {
    raycastHit_ = raycast(ox, oy, oz, dx, dy, dz, maxDist, raycastHitX_, raycastHitY_,
                          raycastHitZ_, raycastPrevX_, raycastPrevY_, raycastPrevZ_,
                          raycastFaceX_, raycastFaceY_, raycastFaceZ_);
    return raycastHit_;
}

void VoxelWorld::setVoxelByName(int wx, int wy, int wz, const std::string &name, int orientation) {
    const CubeType *t = types_.find(name);
    uint8_t id = 0;
    if (t) id = types_.variantId(name, orientation);
    setVoxel(wx, wy, wz, id);
}

std::string VoxelWorld::getCubeTypeName(int wx, int wy, int wz) const {
    const CubeType *t = types_.find(getVoxel(wx, wy, wz));
    return t ? t->name : std::string{};
}

uint8_t VoxelWorld::getCubeTypeTex(int wx, int wy, int wz, const std::string &faceDir) const {
    FaceDir d;
    if (!faceDirFromName(faceDir, d)) return 0;
    const uint8_t id = getVoxel(wx, wy, wz);
    return resolveFaceTex(types_, id, d);
}

}  // namespace eve::voxel
