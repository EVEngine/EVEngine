#pragma once

// 放置操作静态入口 + 可插拔校验 / 吸附 / 变更钩子。
//
// C++ 侧通过 register* 扩展；脚本侧通过定义/世界上的策略名字符串选用已注册规则。

#include "building/BuildingTypes.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::building {

class PlacementWorld;
class Ghost;

class PlacementSystem {
public:
    using ValidateFn = std::function<bool(const PlacementWorld &world, const PlacementQuery &q,
                                          std::string *reason)>;
    using SnapFn =
        std::function<SnapResult(const PlacementWorld &world, float worldX, float worldY)>;
    using ChangeHook = std::function<void(const BuildingChangeEvent &ev)>;

    static void registerValidateRule(const std::string &name, ValidateFn fn);
    static void unregisterValidateRule(const std::string &name);
    static bool hasValidateRule(const std::string &name);

    static void registerSnapRule(const std::string &name, SnapFn fn);
    static void unregisterSnapRule(const std::string &name);
    static bool hasSnapRule(const std::string &name);

    static void registerChangeHook(const std::string &name, ChangeHook fn);
    static void unregisterChangeHook(const std::string &name);
    static bool hasChangeHook(const std::string &name);

    /** 确保内置规则已注册（模块首次使用时自动调用）。 */
    static void ensureBuiltins();

    static SnapResult snap(const PlacementWorld &world, const std::string &buildingId, float worldX,
                           float worldY);
    static SnapResult snapWithMode(const PlacementWorld &world, const std::string &mode,
                                   float worldX, float worldY);

    /** 规范化旋转角（cardinal → 0/90/180/270；none → 0）。 */
    static float normalizeRotation(const std::string &buildingId, float rotationDeg);

    /** 旋转后的占地宽高（cardinal 90/270 交换）。 */
    static void effectiveFootprint(const BuildingDefinition &def, float rotationDeg, int *outW,
                                   int *outH);

    /** 枚举占地格子（旋转后局部 → 世界格子）。返回 false 若定义未知。 */
    static bool foreachFootprintCell(const BuildingDefinition &def, int originCellX,
                                     int originCellY, float rotationDeg,
                                     const std::function<bool(int cx, int cy)> &fn);

    static bool canPlace(PlacementWorld *world, const std::string &buildingId, int cellX, int cellY,
                         float rotationDeg = 0.f, int excludeInstanceId = 0,
                         std::string *reason = nullptr);

    /** 成功返回 instanceId；失败返回 0。 */
    static int placeAt(PlacementWorld *world, const std::string &buildingId, int cellX, int cellY,
                       float rotationDeg = 0.f);
    /** 先吸附再放置；free/自定义 snap 会保留吸附后的世界坐标。 */
    static int placeAtWorld(PlacementWorld *world, const std::string &buildingId, float worldX,
                            float worldY, float rotationDeg = 0.f);
    static int placeGhost(PlacementWorld *world, Ghost *ghost);

    static bool removeBuilding(PlacementWorld *world, int instanceId);
    static bool moveBuilding(PlacementWorld *world, int instanceId, int cellX, int cellY,
                             float rotationDeg = -1.f);
    static void clearBuildings(PlacementWorld *world);

    static void pushEvent(BuildingChangeEvent ev);
    static void pollEvents(std::vector<BuildingChangeEvent> &out);
    static void clearEvents();
    static const std::vector<BuildingChangeEvent> &events();

    static int nextInstanceId();

private:
    static bool runValidate(const PlacementWorld &world, const BuildingDefinition &def,
                            const PlacementQuery &q, std::string *reason);
    static bool checkBoundsAndOccupancy(const PlacementWorld &world, const BuildingDefinition &def,
                                        const PlacementQuery &q, bool checkOccupancy,
                                        std::string *reason);
    static bool checkTerrain(const PlacementWorld &world, const BuildingDefinition &def,
                             const PlacementQuery &q, std::string *reason);
    static bool checkAdjacency(const PlacementWorld &world, const BuildingDefinition &def,
                               const PlacementQuery &q, std::string *reason);
    static void writeOccupancy(PlacementWorld &world, const BuildingDefinition &def,
                               const PlacedBuilding &placed, int instanceId);
    static void clearOccupancy(PlacementWorld &world, int instanceId);
    static void emit(BuildingChangeEvent ev);

    static std::unordered_map<std::string, ValidateFn> &validateRules();
    static std::unordered_map<std::string, SnapFn> &snapRules();
    static std::unordered_map<std::string, ChangeHook> &changeHooks();
    static std::vector<BuildingChangeEvent> &eventQueue();
    static int &instanceCounter();
    static bool &builtinsReady();
};

}  // namespace eve::building
