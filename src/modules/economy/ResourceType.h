#pragma once
#include "common/Export.h"


#include <string>
#include <unordered_map>

namespace eve::economy {

/** @brief 资源节点的供给模型。 */
enum class DepletionModel {
    Finite,     // 采完即枯竭（SC 矿簇）
    Renewable,  // 可再生（AoE 农田复种）
    Infinite,   // 无限供给
    Growing     // 随时间增长（C&C 泰伯利亚）
};

/**
 * @brief 资源类型的静态定义（数据驱动配置的最小单元）。
 */
struct ResourceTypeDef {
    std::string id;             // 唯一标识（"minerals" / "food" ...）
    std::string category = "stock";  // 存量型 / 速率型 / 特殊（人口）
    int         stockMax  = 0;       // 玩家持有上限；0 表示不限
    DepletionModel depletion = DepletionModel::Finite;  // 供给模型
    std::string displayName;         // UI 显示名（可空）
};

/**
 * @brief 资源类型注册表：注册 / 查询 / 清理。
 */
class EVENGINE_API_FOUNDATION ResourceTypeRegistry {
public:
    /** @brief 注册或替换一个资源类型定义。@return false 当 id 为空。 */
    static bool registerType(const ResourceTypeDef& def);
    /** @brief 按 id 查询；未注册时返回 nullptr。 */
    static const ResourceTypeDef* find(const std::string& id);
    /** @brief 已注册类型数量。 */
    static int count();
    /**
     * @brief 按 id 字典序返回第 index 个类型定义。
     * @param index 从 0 开始的序号。
     * @return 借用注册表内的定义，越界返回 nullptr；顺序稳定（便于观察与脚本遍历）。
     * @lifetime The returned pointer is borrowed from the registry: it stays valid
     *           until the next `registerType`/`clear` call on that registry.
     */
    static const ResourceTypeDef* typeAt(int index);
    /** @brief 清空注册表（测试用）。 */
    static void clear();

private:
    static std::unordered_map<std::string, ResourceTypeDef>& types();
};

}  // namespace eve::economy
