#pragma once

#include "voxel/Chunk.h"
#include "voxel/CubeTypeRegistry.h"
#include "voxel/FaceDir.h"
#include "voxel/Frustum.h"
#include "voxel/VoxelPack.h"

#include "procgen/heightmap/TerrainSampler.h"

#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::graphics {
class Graphics;
class Texture;
}  // namespace eve::graphics

namespace eve::data {
class ByteData;
}  // namespace eve::data

namespace eve::voxel {

/** @brief Result of a streaming pass. */
struct StreamStats {
    int created = 0;  // chunks allocated + filled this pass
    int evicted = 0;  // chunks unloaded outside the radius
};

/** @brief One draw batch: all packed rects of one face direction for one chunk. */
struct DrawBatch {
    const Chunk *chunk = nullptr;
    FaceDir dir = FaceDir::PosX;
    const uint32_t *packed = nullptr;
    const uint32_t *ao = nullptr;
    int count = 0;
};

/**
 * @brief Sparse chunk map + visibility selection (frustum + distance + face orientation).
 * Meshing is neighbor-aware (cross-chunk seam culling), parallel-capable and
 * script-friendly via getVisible getters and raycast helpers.
 */
class VoxelWorld {
public:
    VoxelWorld() = default;
    explicit VoxelWorld(const CubeTypeRegistry &types) : types_(types) {}

    Chunk *getOrCreateChunk(int cx, int cy, int cz);
    Chunk *getChunk(int cx, int cy, int cz);
    const Chunk *getChunk(int cx, int cy, int cz) const;
    bool hasChunk(int cx, int cy, int cz) const;
    void removeChunk(int cx, int cy, int cz);
    void clear();

    int getChunkCount() const { return int(chunks_.size()); }

    /**
     * @brief Streaming eviction: drop chunks whose center is farther than
     * `radiusChunks` chunk units from (centerX, centerY, centerZ).
     * Radius < 0 is a no-op. Returns the number of chunks evicted.
     */
    int unloadChunksOutside(int centerX, int centerY, int centerZ, int radiusChunks);

    /**
     * @brief Player-centered streaming: evict chunks outside `radiusChunks`, then
     * allocate + fill + mesh missing chunks inside it (sphere, chunk coords).
     * `generator` is called for each new chunk (chunk, cx, cy, cz); when null
     * the stored terrain sampler (procgen::TerrainSampler) is used if enabled,
     * otherwise chunks stay empty.
     * Radius < 0 is a no-op.
     */
    StreamStats streamAround(int centerX, int centerY, int centerZ, int radiusChunks,
                             const std::function<void(Chunk &, int, int, int)> &generator = {});

    /**
     * @brief Configure the built-in terrain generator used by streamAround(). The
     * sampling itself lives in procgen::TerrainSampler (deterministic Perlin
     * fBm); voxel only adapts it to 32³ chunk filling.
     */
    void setTerrainParams(uint32_t seed, uint8_t top, uint8_t sub, uint8_t stone,
                          float baseHeight, float amplitude, float scale) {
        terrainSampler_.setSeed(seed);
        terrainSampler_.setBase(0.f);
        terrainSampler_.setAmplitude(1.f);
        terrainSampler_.setClamp(true, 0.f, 1.f);
        terrainSampler_.setFrequency(scale > 0.f ? scale : 1.f / 32.f);
        terrainTop_ = top;
        terrainSub_ = sub;
        terrainStone_ = stone;
        terrainBase_ = baseHeight;
        terrainAmplitude_ = amplitude < 0.f ? 0.f : amplitude;
        terrainEnabled_ = true;
    }
    void disableTerrain() { terrainEnabled_ = false; }
    bool terrainEnabled() const { return terrainEnabled_; }

    /** @brief Terrain height (world blocks) at a column for the configured seed. */
    int terrainHeightAt(int wx, int wz) const {
        const float e = terrainSampler_.sample(float(wx), float(wz));
        return int(std::floor(terrainBase_ + terrainAmplitude_ * e));
    }

    /**
     * @brief Persistence: serialize every chunk (coords + raw voxels) into `out`.
     * Format is portable little-endian: "EVVX" + version + count + per-chunk
     * (cx, cy, cz, 32³ voxels). Meshes are rebuilt on demand after loading.
     */
    void serializeWorld(std::vector<uint8_t> &out) const;
    bool deserializeWorld(const uint8_t *data, size_t size);

    /** @brief Script-facing wrappers around serialize/deserialize. */
    data::ByteData *saveWorld() const;
    bool loadWorld(data::ByteData *bytes);

    /**
     * @brief Remesh every dirty chunk. Returns number remeshed.
     * @param maxThreads 0 = auto (parallel up to an internal cap on desktop),
     *                   1 = serial, >1 = that many workers. Falls back to
     *                   serial when threads are unavailable.
     */
    int remeshDirty(int maxThreads = 0);

    /**
     * @brief Select chunks/faces to draw.
     * @param viewProj16 column-major 4x4 RH+ZO view-projection (16 floats)
     * @param eyeX/Y/Z  camera eye world position
     * @param viewRange max distance from eye to chunk center (world units; ≤0 disables)
     * @param faceCull  when true, drop face buffers whose outward normal points away
     */
    void selectVisible(const float *viewProj16, float eyeX, float eyeY, float eyeZ, float viewRange,
                       bool faceCull = true);

    int getVisibleBatchCount() const { return int(visible_.size()); }
    const DrawBatch &getVisibleBatch(int index) const { return visible_[size_t(index)]; }

    /** @brief Script accessors for the last selectVisible result. */
    int getVisibleChunkCount() const { return int(visibleChunkKeys_.size()); }
    void getVisibleChunkCoord(int index, int &cx, int &cy, int &cz) const;
    int getVisibleRectCount() const;

    /**
     * @brief Issue Graphics::drawVoxelFaceInstances for every visible batch.
     * Requires begin3DFrame + setMesh3DViewProj already done.
     */
    void drawVisible(graphics::Graphics *gfx, graphics::Texture *atlas, int tilesPerRow = 16);

    /** @brief World-space voxel get/set. Air (0) never allocates a chunk; a border
     *  edit also invalidates the adjacent chunk's mesh. */
    uint8_t getVoxel(int wx, int wy, int wz) const;
    void setVoxel(int wx, int wy, int wz, uint8_t texId);

    /**
     * @brief Voxel raycast (Amanatides & Woo DDA). Returns true when a solid voxel is
     * found within `maxDist` world units along the ray; fills hit/prev coords
     * and the face normal of the surface the ray entered. `prev` is the last
     * air voxel before the hit (equals hit when the ray starts inside solid).
     */
    bool raycast(float ox, float oy, float oz, float dx, float dy, float dz, float maxDist,
                 int &hitX, int &hitY, int &hitZ, int &prevX, int &prevY, int &prevZ,
                 int &faceX, int &faceY, int &faceZ) const;

    /** @brief Script-facing raycast: stores the last result, returns hit/miss. */
    bool raycastScript(float ox, float oy, float oz, float dx, float dy, float dz, float maxDist);
    bool lastRaycastHit() const { return raycastHit_; }
    int lastRaycastHitX() const { return raycastHitX_; }
    int lastRaycastHitY() const { return raycastHitY_; }
    int lastRaycastHitZ() const { return raycastHitZ_; }
    int lastRaycastPrevX() const { return raycastPrevX_; }
    int lastRaycastPrevY() const { return raycastPrevY_; }
    int lastRaycastPrevZ() const { return raycastPrevZ_; }
    int lastRaycastFaceX() const { return raycastFaceX_; }
    int lastRaycastFaceY() const { return raycastFaceY_; }
    int lastRaycastFaceZ() const { return raycastFaceZ_; }

    /**
     * @brief 按方块名 + orientation(0..3) 设置体素；内部解析为具体类型 id 后写 Chunk。
     * 未注册的名字按空气(0)处理。
     */
    void setVoxelByName(int wx, int wy, int wz, const std::string &name, int orientation = 0);

    /** @brief 该体素所属方块类型名（未注册或空气返回空串）。 */
    std::string getCubeTypeName(int wx, int wy, int wz) const;
    /** @brief 该体素在某面方向上的纹理 id（faceDir 如 "posX"/"+y"/"negZ"）。 */
    uint8_t getCubeTypeTex(int wx, int wy, int wz, const std::string &faceDir) const;

    /** @brief 本世界持有的方块类型注册表（副本）。 */
    const CubeTypeRegistry &cubeTypes() const { return types_; }

private:
    static int floorDiv(int v) {
        return v >= 0 ? (v >> 5) : -(((-v - 1) >> 5) + 1);
    }

    static uint64_t key(int cx, int cy, int cz) {
        const uint64_t ux = uint64_t(uint32_t(cx) & 0x1FFFFFu);
        const uint64_t uy = uint64_t(uint32_t(cy) & 0x1FFFFFu);
        const uint64_t uz = uint64_t(uint32_t(cz) & 0x1FFFFFu);
        return ux | (uy << 21) | (uz << 42);
    }

    static void unpackKey(uint64_t k, int &cx, int &cy, int &cz) {
        auto signExtend21 = [](uint32_t v) -> int {
            if (v & 0x100000u) return int(v | 0xFFE00000u);
            return int(v);
        };
        cx = signExtend21(uint32_t(k & 0x1FFFFFu));
        cy = signExtend21(uint32_t((k >> 21) & 0x1FFFFFu));
        cz = signExtend21(uint32_t((k >> 42) & 0x1FFFFFu));
    }

    /** ChunkSampler: world lookup through this VoxelWorld (const, thread-safe reads). */
    static uint8_t chunkNeighborSampler(void *userData, int chunkX, int chunkY, int chunkZ,
                                        int localX, int localY, int localZ);

    /** Mark the six adjacent chunks dirty when an edit lands on a border face. */
    void markNeighborChunksDirty(int cx, int cy, int cz, int lx, int ly, int lz);

    std::unordered_map<uint64_t, std::unique_ptr<Chunk>> chunks_;
    std::vector<DrawBatch> visible_;
    std::vector<uint64_t> visibleChunkKeys_;
    CubeTypeRegistry types_;
    procgen::TerrainSampler terrainSampler_;
    uint8_t terrainTop_ = 1;
    uint8_t terrainSub_ = 2;
    uint8_t terrainStone_ = 3;
    float terrainBase_ = 8.f;
    float terrainAmplitude_ = 14.f;
    bool terrainEnabled_ = false;

    bool raycastHit_ = false;
    int raycastHitX_ = 0;
    int raycastHitY_ = 0;
    int raycastHitZ_ = 0;
    int raycastPrevX_ = 0;
    int raycastPrevY_ = 0;
    int raycastPrevZ_ = 0;
    int raycastFaceX_ = 0;
    int raycastFaceY_ = 0;
    int raycastFaceZ_ = 0;
};

}  // namespace eve::voxel
