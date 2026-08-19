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

    /** 3D 放置表面命中结果（真实世界坐标）。 */
    struct PlacementHit {
        float worldX = 0.f;
        float worldY = 0.f;
        float worldZ = 0.f;
    };
    /**
     * 放置表面：把指针坐标（2D 场景为鼠标世界坐标；3D 场景由游戏自行做
     * 射线/高度采样后传入平面坐标）换算成真实世界命中点。
     * 引擎只提供接口 + 内置 "plane"（Y=常数平面），物理射线 / 高度场等
     * 表面由游戏侧按接口实现。
     */
    using SurfaceFn = std::function<bool(const PlacementWorld &world, float x, float y,
                                         PlacementHit *hit)>;

    static void registerValidateRule(const std::string &name, ValidateFn fn);
    static void unregisterValidateRule(const std::string &name);
    static bool hasValidateRule(const std::string &name);

    static void registerSnapRule(const std::string &name, SnapFn fn);
    static void unregisterSnapRule(const std::string &name);
    static bool hasSnapRule(const std::string &name);

    static void registerChangeHook(const std::string &name, ChangeHook fn);
    static void unregisterChangeHook(const std::string &name);
    static bool hasChangeHook(const std::string &name);

    static void registerSurface(const std::string &name, SurfaceFn fn);
    static void unregisterSurface(const std::string &name);
    static bool hasSurface(const std::string &name);
    static bool surfaceHit(const PlacementWorld &world, const std::string &name, float x,
                           float y, PlacementHit *hit);
    static std::vector<std::string> surfaceNames();
    /** 内置 "plane" 表面的常量高度（默认 0）。 */
    static void setPlaneSurfaceHeight(float h);
    static float getPlaneSurfaceHeight();

    /** @brief 确保内置规则已注册（模块首次使用时自动调用）。 */
    static void ensureBuiltins();

    static SnapResult snap(const PlacementWorld &world, const std::string &buildingId, float worldX,
                           float worldY);
    /** 3D 版本：按世界平面轴把 (wx, wy, wz) 映射到网格平面坐标再吸附。 */
    static SnapResult snap3D(const PlacementWorld &world, const std::string &buildingId,
                             float worldX, float worldY, float worldZ);
    static SnapResult snapWithMode(const PlacementWorld &world, const std::string &mode,
                                   float worldX, float worldY);

    /** @brief 规范化旋转角（cardinal → 0/90/180/270；none → 0）。 */
    static float normalizeRotation(const std::string &buildingId, float rotationDeg);

    /** @brief 旋转后的占地宽高（cardinal 90/270 交换）。 */
    static void effectiveFootprint(const BuildingDefinition &def, float rotationDeg, int *outW,
                                   int *outH);

    /** @brief 枚举占地格子（旋转后局部 → 世界格子）。返回 false 若定义未知。 */
    static bool foreachFootprintCell(const BuildingDefinition &def, int originCellX,
                                     int originCellY, float rotationDeg,
                                     const std::function<bool(int cx, int cy)> &fn);

    static bool canPlace(PlacementWorld *world, const std::string &buildingId, int cellX, int cellY,
                         float rotationDeg = 0.f, int excludeInstanceId = 0,
                         std::string *reason = nullptr);
    /** 带 elevation 的校验（校验语义仍按 2D 占用/地形，elevation 只进 query 上下文）。 */
    static bool canPlaceElev(PlacementWorld *world, const std::string &buildingId, int cellX,
                             int cellY, float elevation, float rotationDeg = 0.f,
                             int excludeInstanceId = 0, std::string *reason = nullptr);

    /** @brief 成功返回 instanceId；失败返回 0。 */
    static int placeAt(PlacementWorld *world, const std::string &buildingId, int cellX, int cellY,
                       float rotationDeg = 0.f);
    /** @brief 先吸附再放置；free/自定义 snap 会保留吸附后的世界坐标。 */
    static int placeAtWorld(PlacementWorld *world, const std::string &buildingId, float worldX,
                            float worldY, float rotationDeg = 0.f);
    /** 3D 版本：真实世界坐标 (wx, wy, wz)，按平面轴吸附后放置。 */
    static int placeAtWorld3D(PlacementWorld *world, const std::string &buildingId, float worldX,
                              float worldY, float worldZ, float rotationDeg = 0.f);
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
    static std::unordered_map<std::string, SurfaceFn> &surfaces();
    static float &planeSurfaceHeight();
    static std::vector<BuildingChangeEvent> &eventQueue();
    static int &instanceCounter();
    static bool &builtinsReady();
};

}  // namespace eve::building
