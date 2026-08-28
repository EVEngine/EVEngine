#pragma once

#include "common/BorrowedRef.h"
#include "common/Result.h"
#include "housegen/HouseGenTypes.h"

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace eve::housegen {

/** @brief 房屋组件注册表（按 id 索引，支持分类/风格查询）。 */
class HouseComponentLibrary {
public:
    /** @brief 从 JSON / 文件加载组件。 */
    [[nodiscard]] eve::Result<void> loadFromJson(std::string_view json);
    [[nodiscard]] eve::Result<void> loadFromFile(std::string_view filename);
    /** @brief 注册单个组件（id 重复会报错）。 */
    [[nodiscard]] eve::Result<void> registerComponent(const HouseComponent &component);
    /** @brief 清空注册表。 */
    void clear();
    /** @brief 按 id 查询组件；缺失是正常查询结果。 */
    [[nodiscard]] eve::OptionalRef<const HouseComponent> find(std::string_view id) const;
    /** @brief 全部组件 id。 */
    std::vector<std::string> ids() const;
    /** @brief 按分类（可选风格）查询组件。 */
    [[nodiscard]] std::vector<std::reference_wrapper<const HouseComponent>> byCategory(
        std::string_view category, std::string_view style = {}) const;
    /**
     * @brief 报告一个风格是否构成完整组件包。
     * @param style 风格标签；空表示未指定。
     * @return 当 foundation/floor/wall/door/roof 五个核心分类在该风格下都有组件时为 true。
     * @remarks 完整包保证按风格生成时不会退化为与无风格组件混搭（见 byCategory 的回退）。
     */
    [[nodiscard]] bool hasCompletePack(std::string_view style) const;
    /** @brief 所有能构成完整组件包（五个核心分类齐全）的风格标签。 */
    [[nodiscard]] std::vector<std::string> completePacks() const;
    /** @brief 组件总数。 */
    int count() const;

private:
    std::unordered_map<std::string, HouseComponent> components_;
};

}  // namespace eve::housegen
