#pragma once

#include "voxel/Chunk.h"
#include "voxel/CubeTypeRegistry.h"
#include "voxel/FaceDir.h"
#include "voxel/Frustum.h"
#include "voxel/VoxelPack.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::graphics {
class Graphics;
class Texture;
}  // namespace eve::graphics

namespace eve::voxel {

/** One draw batch: all packed rects of one face direction for one chunk. */
struct DrawBatch {
    const Chunk *chunk = nullptr;
    FaceDir dir = FaceDir::PosX;
    const uint32_t *packed = nullptr;
    int count = 0;
};

/**
 * Sparse chunk map + visibility selection (frustum + distance + face orientation).
 * Script-friendly query via getVisible* after selectVisible.
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

    /** Remesh every dirty chunk. Returns number remeshed. */
    int remeshDirty();

    /**
     * Select chunks/faces to draw.
     * @param viewProj16 column-major 4x4 RH+ZO view-projection (16 floats)
     * @param eyeX/Y/Z  camera eye world position
     * @param viewRange max distance from eye to chunk center (world units; ≤0 disables)
     * @param faceCull  when true, drop face buffers whose outward normal points away
     */
    void selectVisible(const float *viewProj16, float eyeX, float eyeY, float eyeZ, float viewRange,
                       bool faceCull = true);

    int getVisibleBatchCount() const { return int(visible_.size()); }
    const DrawBatch &getVisibleBatch(int index) const { return visible_[size_t(index)]; }

    /** Script accessors for the last selectVisible result. */
    int getVisibleChunkCount() const { return int(visibleChunkKeys_.size()); }
    void getVisibleChunkCoord(int index, int &cx, int &cy, int &cz) const;
    int getVisibleRectCount() const;

    /**
     * Issue Graphics::drawVoxelFaceInstances for every visible batch.
     * Requires begin3DFrame + setMesh3DViewProj already done.
     */
    void drawVisible(graphics::Graphics *gfx, graphics::Texture *atlas, int tilesPerRow = 16);

    /** World-space voxel get/set (creates chunk on set). */
    uint8_t getVoxel(int wx, int wy, int wz) const;
    void setVoxel(int wx, int wy, int wz, uint8_t texId);

    /**
     * 按方块名 + orientation(0..3) 设置体素；内部解析为具体类型 id 后写 Chunk。
     * 未注册的名字按空气(0)处理。
     */
    void setVoxelByName(int wx, int wy, int wz, const std::string &name, int orientation = 0);

    /** 该体素所属方块类型名（未注册或空气返回空串）。 */
    std::string getCubeTypeName(int wx, int wy, int wz) const;
    /** 该体素在某面方向上的纹理 id（faceDir 如 "posX"/"+y"/"negZ"）。 */
    uint8_t getCubeTypeTex(int wx, int wy, int wz, const std::string &faceDir) const;

    /** 本世界持有的方块类型注册表（副本）。 */
    const CubeTypeRegistry &cubeTypes() const { return types_; }

private:
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

    std::unordered_map<uint64_t, std::unique_ptr<Chunk>> chunks_;
    std::vector<DrawBatch> visible_;
    std::vector<uint64_t> visibleChunkKeys_;
    CubeTypeRegistry types_;
};

}  // namespace eve::voxel
