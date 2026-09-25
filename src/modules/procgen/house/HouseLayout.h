#pragma once
#include "common/Export.h"


#include "common/ECS.h"
#include "common/Result.h"
#include "procgen/Grid2D.h"
#include "procgen/PointSet.h"
#include "procgen/house/HouseGenTypes.h"

#include <string>
#include <string_view>
#include <vector>

namespace eve {
namespace graphics { class Graphics; class Renderable3D; }
namespace model3d { class Model3D; }
namespace housegen {

class HouseComponentLibrary;

/** @brief 一次生成的房屋布局：实例 + 房间 + 元信息，可 JSON 序列化 / 实例化。 */
class EVENGINE_API_DOMAINS HouseLayout {
public:
    uint32_t seed = 1;
    /** @brief 生成参数回显。 */
    float moduleSize = 1.f;
    float floorHeight = 3.f;
    /** @brief 布局风格结果。 */
    std::string footprintStyle = "rectangle";
    std::string roofStyle = "gable";
    std::string entranceSide = "north";
    /** @brief Canonical base-floor occupancy mask owned by this layout. */
    int                  footprintWidth = 0;
    int                  footprintDepth = 0;
    std::vector<uint8_t> footprintMask;
    /** @brief 组件实例 / 房间 / 诊断信息。 */
    std::vector<HouseInstance> instances;
    std::vector<HouseRoom> rooms;
    std::vector<std::string> diagnostics;

    /** @brief 清空布局。 */
    void clear();
    /**
     * @brief Serialize or restore the eve.house-layout version 1 JSON schema.
     * @remarks Unknown fields
     * are ignored for forward-compatible readers; unsupported explicit schemas or versions
     *          are rejected
     * transactionally without changing this layout.
     */
    std::string toJson() const;
    [[nodiscard]] eve::Result<void> fromJson(std::string_view json);
    /** @brief 校验布局是否满足组件库规则。 */
    [[nodiscard]] eve::Result<void> validate(const HouseComponentLibrary &library) const;
    /**
     * @brief Export the generated footprint as the canonical procgen grid value.
     * @return Success after atomically replacing @p out; invalid dimensions return a diagnostic.
     * @cost Linear in the number of generated component instances.
     */
    [[nodiscard]] eve::Result<void> writeFootprintGrid(procgen::Grid2D &out) const;
    /**
     * @brief Export component placements as canonical attributed procgen points.
     * @return Success after atomically replacing @p out; conversion failure leaves it unchanged.
     * @cost Linear in the number of generated component instances.
     */
    [[nodiscard]] eve::Result<void> writeComponentPoints(procgen::PointSet &out) const;
    /**
     * @brief 把布局实例化为场景中的 Renderable3D ECS 实体。
     * @return Generation-checked ECS handles; the graphics ECS world owns the entities.
     * @remarks Callers must resolve handles before use and must not retain resolved pointers
     *          across world mutation. Failure leaves no partially-created entities.
     */
    [[nodiscard]] eve::Result<std::vector<ecs::EntityHandle>> instantiate(graphics::Graphics          &gfx,
                                                                          model3d::Model3D            &models,
                                                                          const HouseComponentLibrary &library) const;
};

}  // namespace housegen
}  // namespace eve
