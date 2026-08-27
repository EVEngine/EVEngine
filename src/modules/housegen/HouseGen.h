#pragma once

#include "common/Module.h"
#include "common/Result.h"
#include "housegen/HouseComponentLibrary.h"
#include "housegen/HouseGenerator.h"
#include "housegen/HouseLayout.h"

namespace eve::housegen {

/** @brief 程序化房屋生成模块（eve.HouseGen）：组件库 + 布局生成。 */
class HouseGen : public Module {
public:
    Module_REG(HouseGen);
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
    /** @brief 组件库（可直接访问）。 */
    HouseComponentLibrary &library() { return library_; }
    const HouseComponentLibrary &library() const { return library_; }

private:
    HouseComponentLibrary library_;
};

}  // namespace eve::housegen
