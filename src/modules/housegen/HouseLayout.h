#pragma once

#include "common/ECS.h"
#include "common/Result.h"
#include "housegen/HouseGenTypes.h"

#include <string>
#include <string_view>
#include <vector>

namespace eve {
namespace graphics { class Graphics; class Renderable3D; }
namespace model3d { class Model3D; }
namespace housegen {

class HouseComponentLibrary;

/** @brief 一次生成的房屋布局：实例 + 房间 + 元信息，可 JSON 序列化 / 实例化。 */
class HouseLayout {
public:
    uint32_t seed = 1;
    /** @brief 生成参数回显。 */
    float moduleSize = 1.f;
    float floorHeight = 3.f;
    /** @brief 布局风格结果。 */
    std::string footprintStyle = "rectangle";
    std::string roofStyle = "gable";
    std::string entranceSide = "north";
    /** @brief 组件实例 / 房间 / 诊断信息。 */
    std::vector<HouseInstance> instances;
    std::vector<HouseRoom> rooms;
    std::vector<std::string> diagnostics;

    /** @brief 清空布局。 */
    void clear();
    /** @brief 序列化为 JSON / 从 JSON 恢复。 */
    std::string toJson() const;
    [[nodiscard]] eve::Result<void> fromJson(std::string_view json);
    /** @brief 校验布局是否满足组件库规则。 */
    [[nodiscard]] eve::Result<void> validate(const HouseComponentLibrary &library) const;
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
