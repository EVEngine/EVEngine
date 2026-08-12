#pragma once

// 建筑放置模块入口：定义 / 放置世界 / 鬼影 / 变更事件的脚本绑定点。
// 设计文档：docs/dev/建筑放置系统设计.md

#include "common/Module.h"
#include "building/Ghost.h"
#include "building/PlacementWorld.h"

#include <string>

namespace eve::building {

class Building : public Module {
public:
    Module_REG(Building);
    Building() = default;
    ~Building() override = default;

    // ---- Building definitions ----
    int registerBuildingsFromJson(const std::string &json);
    void clearBuildingDefinitions();
    int getBuildingDefinitionCount();
    bool hasBuildingDefinition(const std::string &buildingId);
    std::string getBuildingDisplayName(const std::string &buildingId);
    std::string getBuildingCategory(const std::string &buildingId);
    int getBuildingFootprintW(const std::string &buildingId);
    int getBuildingFootprintH(const std::string &buildingId);
    std::string getBuildingSnapMode(const std::string &buildingId);
    std::string getBuildingRotationMode(const std::string &buildingId);
    std::string getBuildingValidateRule(const std::string &buildingId);
    bool buildingHasTag(const std::string &buildingId, const std::string &tag);
    std::string getBuildingExtra(const std::string &buildingId, const std::string &key,
                                 const std::string &fallback = {});
    int getBuildingCost(const std::string &buildingId, const std::string &resource);

    // ---- Factories ----
    PlacementWorld *newWorld(int width, int height, float cellSize = 32.f);
    Ghost *newGhost();

    // ---- Extension introspection ----
    bool hasValidateRule(const std::string &name);
    bool hasSnapRule(const std::string &name);

    // ---- Change events ----
    void clearChangeEvents();
    int getChangeEventCount() const;
    std::string getChangeEventAction(int index) const;
    std::string getChangeEventWorldId(int index) const;
    std::string getChangeEventBuildingId(int index) const;
    int getChangeEventInstanceId(int index) const;
    int getChangeEventCellX(int index) const;
    int getChangeEventCellY(int index) const;
    int getChangeEventOtherCellX(int index) const;
    int getChangeEventOtherCellY(int index) const;
    float getChangeEventRotation(int index) const;
};

}  // namespace eve::building
