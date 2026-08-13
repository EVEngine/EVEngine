#include "voxel/VoxelWorld.h"

#include "graphics/Graphics.h"

#include <cmath>
#include <cstring>

namespace eve::voxel {

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

int VoxelWorld::remeshDirty() {
    int n = 0;
    for (auto &kv : chunks_) {
        if (kv.second->isDirty()) {
            kv.second->remesh(types_);
            ++n;
        }
    }
    return n;
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
        chunk->ensureMeshed(types_);

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
                                    b.chunk->originZ(), faceDirName(b.dir), atlas, tilesPerRow);
    }
}

uint8_t VoxelWorld::getVoxel(int wx, int wy, int wz) const {
    auto floorDiv = [](int v) {
        return v >= 0 ? (v >> 5) : -(((-v - 1) >> 5) + 1);
    };
    const int cx = floorDiv(wx);
    const int cy = floorDiv(wy);
    const int cz = floorDiv(wz);
    const Chunk *c = getChunk(cx, cy, cz);
    if (!c) return 0;
    return c->get(wx - cx * kChunkSize, wy - cy * kChunkSize, wz - cz * kChunkSize);
}

void VoxelWorld::setVoxel(int wx, int wy, int wz, uint8_t texId) {
    auto floorDiv = [](int v) {
        return v >= 0 ? (v >> 5) : -(((-v - 1) >> 5) + 1);
    };
    const int cx = floorDiv(wx);
    const int cy = floorDiv(wy);
    const int cz = floorDiv(wz);
    Chunk *c = getOrCreateChunk(cx, cy, cz);
    c->set(wx - cx * kChunkSize, wy - cy * kChunkSize, wz - cz * kChunkSize, texId);
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
