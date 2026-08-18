#pragma once

#include "housegen/HouseGenTypes.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace eve::housegen {

/** @brief 房屋组件注册表（按 id 索引，支持分类/风格查询）。 */
class HouseComponentLibrary {
public:
    /** @brief 从 JSON / 文件加载组件。 */
    bool loadFromJson(const std::string &json, std::string *error = nullptr);
    bool loadFromFile(const std::string &filename, std::string *error = nullptr);
    /** @brief 注册单个组件（id 重复会报错）。 */
    bool registerComponent(const HouseComponent &component, std::string *error = nullptr);
    /** @brief 清空注册表。 */
    void clear();
    /** @brief 按 id 查询组件。 */
    const HouseComponent *find(const std::string &id) const;
    /** @brief 全部组件 id。 */
    std::vector<std::string> ids() const;
    /** @brief 按分类（可选风格）查询组件。 */
    std::vector<const HouseComponent *> byCategory(const std::string &category,
                                                    const std::string &style = {}) const;
    /** @brief 组件总数。 */
    int count() const;

private:
    std::unordered_map<std::string, HouseComponent> components_;
};

}  // namespace eve::housegen
