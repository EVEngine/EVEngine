#pragma once

#include "common/Module.h"
#include "voxel/CubeTypeRegistry.h"
#include "voxel/VoxelWorld.h"

#include <string>

namespace eve::graphics {
class Graphics;
class Texture;
}  // namespace eve::graphics

namespace eve::voxel {

/**
 * @brief High-performance voxel rendering module.
 *
 * - 32³ chunks, greedy-meshed into face rectangles
 * - Each rect packed into one uint32 (xyz + wh + tex)
 * - Six per-chunk face buffers (pos/neg X/Y/Z)
 * - Camera frustum + view-range + face-orientation cull → instanced draws
 *
 * 门面只负责「组织世界」与「定义方块类型」；打包 / 网格化等实现细节
 * 归渲染侧与内部头文件。方块类型由注册表定义，创建世界时传入。
 *
 * Script: `voxel <- eve.Voxel(); types <- voxel.newCubeTypes(); world <- voxel.newWorld(types);`
 */
class Voxel : public Module {
public:
    Module_REG(Voxel);
    Voxel() = default;
    ~Voxel() override = default;

    int getChunkSize() const { return kChunkSize; }

    /** @brief 返回一个新的方块类型注册表（由调用者持有 / 脚本持有）。 */
    CubeTypeRegistry *newCubeTypes();

    /** @brief 创建世界；内部拷贝注册表（传 nullptr 表示空注册表，类型 id 即纹理 id）。 */
    VoxelWorld *newWorld(const CubeTypeRegistry *types = nullptr);
};

}  // namespace eve::voxel
