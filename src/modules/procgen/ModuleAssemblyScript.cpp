#include "procgen/ModuleAssemblyScript.h"
#include <simplesquirrel/simplesquirrel.hpp>
#include "common/SquirrelBinding.h"
#include "procgen/ModuleAssembly.h"

namespace eve::procgen {
void exposeModuleAssembly(ssq::Table& table) {
    auto vm       = table.getHandle();
    auto assembly = table.addClass<ModuleAssemblyPlan>(
        "ProcgenModuleAssemblyPlan", std::function<ModuleAssemblyPlan*()>([] { return new ModuleAssemblyPlan(); }),
        true);
    assembly.addFunc(
        "configure", [vm](ModuleAssemblyPlan* value, float cellSize, float floorHeight, int minCellX, int maxCellX,
                          int minCellZ, int maxCellZ, int maxLevels, bool bounded, bool requireConnection) {
            return eve::script::projectResult(vm, value->configure(cellSize, floorHeight, minCellX, maxCellX, minCellZ,
                                                                   maxCellZ, maxLevels, bounded, requireConnection));
        });
    assembly.addFunc(
        "setAllowedQuarterTurns", [vm](ModuleAssemblyPlan* value, bool turn0, bool turn90, bool turn180, bool turn270) {
            return eve::script::projectResult(vm, value->setAllowedQuarterTurns(turn0, turn90, turn180, turn270));
        });
    assembly.addFunc("setMinimumSupportRatio", [vm](ModuleAssemblyPlan* value, float ratio) {
        return eve::script::projectResult(vm, value->setMinimumSupportRatio(ratio));
    });
    assembly.addFunc("applyConfigJson", [vm](ModuleAssemblyPlan* value, const std::string& json) {
        return eve::script::projectResult(vm, value->applyConfigJson(json));
    });
    assembly.addFunc("registerModule",
                     [vm](ModuleAssemblyPlan* value, const std::string& id, int widthCells, int depthCells) {
                         return eve::script::projectResult(vm, value->registerModuleType(id, widthCells, depthCells));
                     });
    assembly.addFunc("registerVolumeModule", [vm](ModuleAssemblyPlan* value, const std::string& id, int widthCells,
                                                  int depthCells, int heightLevels) {
        return eve::script::projectResult(vm,
                                          value->registerVolumeModuleType(id, widthCells, depthCells, heightLevels));
    });
    assembly.addFunc("addConnector", [vm](ModuleAssemblyPlan* value, const std::string& id, int cellX, int cellZ,
                                          int facing, const std::string& tag, const std::string& accepts) {
        return eve::script::projectResult(vm, value->addConnector(id, cellX, cellZ, facing, tag, accepts));
    });
    assembly.addFunc("addVolumeConnector",
                     [vm](ModuleAssemblyPlan* value, const std::string& id, int cellX, int cellZ, int levelOffset,
                          int facing, const std::string& tag, const std::string& accepts) {
                         return eve::script::projectResult(
                             vm, value->addVolumeConnector(id, cellX, cellZ, levelOffset, facing, tag, accepts));
                     });
    assembly.addFunc("place", [vm](ModuleAssemblyPlan* value, const std::string& id, int cellX, int cellZ, int level,
                                   int quarterTurn) {
        return eve::script::projectResult(vm, value->place({id, cellX, cellZ, level, quarterTurn}));
    });
    assembly.addFunc("getPlacementCount", [](ModuleAssemblyPlan* value) { return value->getPlacementCount(); });
    assembly.addFunc("getPlacementModule",
                     [](ModuleAssemblyPlan* value, int index) { return value->getPlacementModule(index); });
    assembly.addFunc("getPlacementX", [](ModuleAssemblyPlan* value, int index) { return value->getPlacementX(index); });
    assembly.addFunc("getPlacementY", [](ModuleAssemblyPlan* value, int index) { return value->getPlacementY(index); });
    assembly.addFunc("getPlacementZ", [](ModuleAssemblyPlan* value, int index) { return value->getPlacementZ(index); });
    assembly.addFunc("getPlacementYawDegrees",
                     [](ModuleAssemblyPlan* value, int index) { return value->getPlacementYawDegrees(index); });
}
}  // namespace eve::procgen
