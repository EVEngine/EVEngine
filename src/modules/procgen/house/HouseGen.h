#pragma once

#include "common/Result.h"
#include "procgen/house/HouseComponentLibrary.h"
#include "procgen/house/HouseGenerator.h"
#include "procgen/house/HouseLayout.h"

namespace ssq { class Table; class Class; }
namespace eve::graphics { class Graphics; }
namespace eve::model3d { class Model3D; }

namespace eve::housegen {

/** @brief 程序化房屋生成模块（eve.HouseGen）：组件库 + 布局生成。 */
class HouseGen {
public:
    static void expose(ssq::Class &cls);
    /** @brief 从 JSON / 文件加载房屋组件库。 */
    [[nodiscard]] eve::Result<void> loadComponentsFromJson(const std::string &json);
    [[nodiscard]] eve::Result<void> loadComponentsFromFile(const std::string &filename);
    /** @brief 清空组件库。 */
    void clearComponents();
    /** @brief 组件数量。 */
    int getComponentCount() const;
    /** @brief 工厂：生成请求 / 布局。 */
    [[nodiscard]] HouseRequest newRequest() const;
    [[nodiscard]] HouseLayout  newLayout() const;
    /** @brief 按请求生成布局；失败返回结构化诊断。 */
    [[nodiscard]] eve::Result<void> generate(const HouseRequest &request, HouseLayout &layout);
    /**
     * @brief Instantiate a generated layout through the current component library.
     * @return Applied on complete ECS publication; failure rolls back every entity created by this call.
     * @cost Linear in component instances plus model import and GPU resource creation for uncached assets.
     */
    [[nodiscard]] eve::Result<void> instantiate(const HouseLayout &layout, graphics::Graphics &gfx,
                                                model3d::Model3D &models) const;
    /** @brief 组件库（可直接访问）。 */
    HouseComponentLibrary &library() { return library_; }
    const HouseComponentLibrary &library() const { return library_; }

private:
    HouseComponentLibrary library_;
};

/** @brief Register the procgen-owned house generation compatibility API. */
void exposeHouseGeneration(ssq::Table &table);

}  // namespace eve::housegen
