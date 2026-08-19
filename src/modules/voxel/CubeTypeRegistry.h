#pragma once

// 方块类型注册表：定义有哪些 cube、各面材质、方向性与组合声明。
// 注册表在创建 world 时传入；网格化阶段用 resolveFaceTex 把“类型 id”解析为
// 各面“纹理 id”输出给渲染器。空注册表下行为退化为“类型 id == 纹理 id”，
// 与旧的体素语义完全一致。

#include "voxel/CubeType.h"
#include "voxel/FaceDir.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::voxel {

class CubeTypeRegistry {
public:
    CubeTypeRegistry() = default;

    /**
     * 注册一个方块类型，返回其基础类型 id（0 保留给空气）。
     * 方向性方块会按 orientation ∈ {0,1,2,3} 绕 Y 轴旋转 faceTex，
     * 展开成 4 个连续的具体类型变体；基础 id 指向 0 度变体。
     */
    uint8_t add(const CubeType &type);

    /**
     * 从 JSON 数组或单对象批量注册，返回成功数量。
     * 元素形如：
     * {
     *   "name": "furnace", "faceTex": [4, 4, 4, 4, 5, 4],
     *   "directional": true, "composeGroup": "furnace", "connects": false
     * }
     */
    int loadFromJson(const std::string &json, std::string *error = nullptr);

    /** 按名字返回 0 度变体；未找到返回 nullptr。 */
    const CubeType *find(const std::string &name) const;
    /** 按类型 id 返回变体（含方向变体）；未找到返回 nullptr。 */
    const CubeType *find(uint8_t id) const;

    /** 名字 + orientation(0..3) 对应的具体类型 id；非方向性类型忽略 orientation。 */
    uint8_t variantId(const std::string &name, int orientation) const;

    /** 命名的方块类型数量（不含方向变体）。 */
    int count() const { return int(byName_.size()); }
    /** 类型总数（含方向变体，不含空气占位）。 */
    int variantCount() const { return int(types_.size()) - 1; }

    void clear();

    /** 进程级空注册表（默认参数与向后兼容回退用）。 */
    static const CubeTypeRegistry &empty();

private:
    std::unordered_map<std::string, uint8_t> byName_;  // name -> 0 度变体 id
    std::vector<CubeType> types_{CubeType{}};          // index 0 = 空气占位
};

/**
 * 求某个体素在某面方向上的实际图集纹理 id。
 * 未注册的 id 退化为“所有面 = 原 id”（向后兼容：体素值即纹理 id）。
 */
inline uint8_t resolveFaceTex(const CubeTypeRegistry &types, uint8_t id, FaceDir dir) {
    const CubeType *t = types.find(id);
    if (!t) return id;
    return t->faceTex[int(dir)];
}

}  // namespace eve::voxel
