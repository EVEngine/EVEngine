#pragma once

#include "common/Identity.h"
#include "common/Module.h"
#include "common/Result.h"
#include "common/Value.h"
#include "housegen/HouseComponentLibrary.h"
#include "housegen/HouseGenerator.h"
#include "housegen/HouseLayout.h"

#include <memory>
#include <string>
#include <vector>

namespace eve::procgen {
class ArtifactStore;
}  // namespace eve::procgen

namespace eve::housegen {

/**
 * @brief 程序化房屋生成模块（eve.HouseGen）：组件库 + 布局生成 + procgen 持久化。
 *
 * 布局生成（generate）保持纯逻辑；持久化与热重载通过 procgen 的 BuildKey /
 * ArtifactStore 协议落地（见 HousePersistence.h），本模块持有一个 procgen
 * 所有权仓库来快照 / 恢复生成的房屋。
 */
class HouseGen : public Module {
public:
    Module_REG(HouseGen);
    HouseGen();
    ~HouseGen() override;

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
    /** @brief 所有能构成完整组件包（五个核心分类齐全）的风格标签。 */
    [[nodiscard]] std::vector<std::string> getStylePacks() const;

    // --- procgen-backed persistence and hot-reload identity ---

    /** @brief 生成请求的确定性身份文本（热重载比较用）。 */
    [[nodiscard]] std::string requestBuildKey(const HouseRequest &request) const;
    /** @brief 已生成布局的确定性身份文本。 */
    [[nodiscard]] std::string layoutBuildKey(const HouseLayout &layout) const;
    /** @brief 把布局作为 procgen 工件原子发布进本模块仓库。 */
    [[nodiscard]] eve::Result<eve::ArtifactId> publishLayout(HouseLayout &layout);
    /** @brief 按工件身份读回一个已发布的布局。 */
    [[nodiscard]] eve::Result<HouseLayout> findLayout(eve::ArtifactId id) const;
    /** @brief 导出全部已发布布局的状态（供存档）。 */
    [[nodiscard]] eve::Result<eve::Value> snapshotLayouts() const;
    /** @brief 事务化恢复先前导出的布局状态。 */
    [[nodiscard]] eve::Result<void> restoreLayouts(const eve::Value &state);
    /** @brief 清空已发布布局。 */
    void clearLayouts();
    /** @brief 已发布布局数量。 */
    int layoutCount() const;

private:
    HouseComponentLibrary library_;
    std::unique_ptr<procgen::ArtifactStore> layouts_;
};

}  // namespace eve::housegen
