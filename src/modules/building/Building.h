#pragma once
#include "common/Export.h"


/**
 * @brief 建筑放置模块入口：定义 / 放置世界 / 鬼影 / 变更事件的脚本绑定点。
 * 设计文档：docs/dev/建筑放置系统设计.md
 */

#include "common/Module.h"
#include "building/Ghost.h"
#include "building/PlacementWorld.h"
#include "building/PlacementSession.h"

#include <string>

namespace eve::building {

/** @brief 建筑放置模块（eve.Building）。 */
class EVENGINE_API_WORLD Building : public Module {
public:
    Module_REG(Building);
    /** @brief Building. */
    Building() = default;
    /** @brief Building. */
    ~Building() override = default;

    /** @brief 从 JSON 注册建筑模板；返回成功注册数量。 */
    int registerBuildingsFromJson(const std::string &json);
    /** @brief 清空全部建筑模板。 */
    void clearBuildingDefinitions();
    /** @brief 已注册建筑模板数量。 */
    int getBuildingDefinitionCount();
    /** @brief 建筑模板查询（显示名/类别/占地/规则/标签/额外属性/成本）。 */
    bool hasBuildingDefinition(const std::string &buildingId);
    /** @brief Returns the building display name. */
    std::string getBuildingDisplayName(const std::string &buildingId);
    /** @brief Returns the building category. */
    std::string getBuildingCategory(const std::string &buildingId);
    /** @brief Returns the building footprint w. */
    int getBuildingFootprintW(const std::string &buildingId);
    /** @brief Returns the building footprint h. */
    int getBuildingFootprintH(const std::string &buildingId);
    /** @brief Returns the building snap mode. */
    std::string getBuildingSnapMode(const std::string &buildingId);
    /** @brief Returns the building rotation mode. */
    std::string getBuildingRotationMode(const std::string &buildingId);
    /** @brief Returns the building validate rule. */
    std::string getBuildingValidateRule(const std::string &buildingId);
    /** @brief Returns the building channel. */
    std::string getBuildingChannel(const std::string &buildingId);
    /** @brief Returns the building render mode. */
    std::string getBuildingRenderMode(const std::string &buildingId);
    /** @brief Definition placement domain (`cell` or `edge`). */
    std::string getBuildingPlacementKind(const std::string &buildingId);
    /** @brief Definition edge connection group, or empty when it falls back to id. */
    std::string getBuildingConnectionGroup(const std::string &buildingId);
    /** @brief Returns the building visual 2 d. */
    std::string getBuildingVisual2d(const std::string &buildingId, const std::string &key,
                                    const std::string &fallback = {});
    /** @brief Returns the building visual 3 d. */
    std::string getBuildingVisual3d(const std::string &buildingId, const std::string &key,
                                    const std::string &fallback = {});
    /** @brief Building has tag. */
    bool buildingHasTag(const std::string &buildingId, const std::string &tag);
    /** @brief Returns the building extra. */
    std::string getBuildingExtra(const std::string &buildingId, const std::string &key,
                                 const std::string &fallback = {});
    /** @brief Returns the building cost. */
    int getBuildingCost(const std::string &buildingId, const std::string &resource);

    /** @brief 工厂：创建放置世界 / 鬼影。 */
    PlacementWorld *newWorld(int width, int height, float cellSize = 32.f);
    /** @brief Creates a ghost. @ownership Caller deletes unless documented otherwise. */
    Ghost *newGhost();
    /** @brief Creates a session. @ownership Caller deletes unless documented otherwise. */
    PlacementSession *newSession();

    /** @brief 扩展规则是否存在（校验/吸附）。 */
    bool hasValidateRule(const std::string &name);
    /** @brief True when snap rule. */
    bool hasSnapRule(const std::string &name);
    /** @brief True when surface. */
    bool hasSurface(const std::string &name);
    /** @brief Returns the surface count. */
    int getSurfaceCount();
    /** @brief Returns the surface name. */
    std::string getSurfaceName(int index);
    /** @brief Sets the plane surface height. */
    void setPlaneSurfaceHeight(float h);
    /** @brief Returns the plane surface height. */
    float getPlaneSurfaceHeight();

    /** @brief 变更事件队列（放置/移除/移动）。 */
    void clearChangeEvents();
    /** @brief Returns the change event count. */
    int getChangeEventCount() const;
    /** @brief Returns the change event action. */
    std::string getChangeEventAction(int index) const;
    /** @brief Returns the change event world id. */
    std::string getChangeEventWorldId(int index) const;
    /** @brief Returns the change event building id. */
    std::string getChangeEventBuildingId(int index) const;
    /** @brief Returns the change event instance id. */
    int getChangeEventInstanceId(int index) const;
    /** @brief Returns the change event cell x. */
    int getChangeEventCellX(int index) const;
    /** @brief Returns the change event cell y. */
    int getChangeEventCellY(int index) const;
    /** @brief Returns the change event other cell x. */
    int getChangeEventOtherCellX(int index) const;
    /** @brief Returns the change event other cell y. */
    int getChangeEventOtherCellY(int index) const;
    /** @brief Returns the change event rotation. */
    float getChangeEventRotation(int index) const;
};

}  // namespace eve::building
