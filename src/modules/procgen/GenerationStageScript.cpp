#include "procgen/GenerationStageScript.h"

#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"
#include "procgen/Grid2D.h"
#include "procgen/MeshBuild.h"
#include "procgen/Params.h"
#include "procgen/ProcgenScriptObjects.h"
#include "procgen/ScalarField3D.h"
#include "procgen/algorithms/CaveGeneration.h"
#include "procgen/house/HouseGen.h"
#include "procgen/house/HouseGeneration.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <functional>
#include <sstream>

namespace eve::procgen {
namespace {
template <class T>
ssq::Table projectOwned(HSQUIRRELVM vm, Result<T> result) {
    if (!result.ok()) return script::projectStatusResult(vm, result.status());
    auto instance = script::makeOwnedSquirrelInstance<T>(vm, std::make_unique<T>(std::move(result).takeValue()));
    if (!instance.ok()) return script::projectStatusResult(vm, instance.status());
    return script::projectStatusResult(vm, Status::success(), std::move(instance).takeValue());
}
Result<void> invalidArgument() {
    return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                   "generation stage argument is null or unknown", {}, {},
                                                   "procgen.scriptStages"));
}
}  // namespace

void exposeGenerationStages(ssq::Table& table) {
    auto field = table.addClass<ScalarField3D>(
        "ScalarField3D", std::function<ScalarField3D*()>([] { return new ScalarField3D(); }), true);
    const auto vm = table.getHandle();
    field.addFunc("resize", [vm](ScalarField3D* self, int x, int y, int z, float value) {
        return script::projectResult(vm, self->resize(x, y, z, value));
    });
    field.addFunc("getWidth", [](ScalarField3D* self) { return self->width(); });
    field.addFunc("getHeight", [](ScalarField3D* self) { return self->height(); });
    field.addFunc("getDepth", [](ScalarField3D* self) { return self->depth(); });
    field.addFunc("carveEllipsoid", [vm](ScalarField3D* self, float x, float y, float z, float rx, float ry, float rz) {
        return script::projectResult(vm, self->carveEllipsoid(x, y, z, rx, ry, rz));
    });
    field.addFunc("extractMesh",
                  [vm](ScalarField3D* self, float isolevel) { return projectOwned(vm, self->extractMesh(isolevel)); });
    field.addFunc("sample", [vm](ScalarField3D* self, int x, int y, int z) {
        return script::projectResult(vm, self->sample(x, y, z), [](float value) { return value; });
    });
    field.addFunc("setSample", [vm](ScalarField3D* self, int x, int y, int z, float value) {
        return script::projectResult(vm, self->setSample(x, y, z, value));
    });

    auto cave = table.addClass<CaveGeneration>(
        "CaveGeneration", std::function<CaveGeneration*()>([] { return new CaveGeneration(); }), true);
    cave.addFunc("configure", [vm](CaveGeneration* self, ScriptProcgenParams* params) {
        if (!params) return script::projectResult(vm, invalidArgument());
        auto view = Procgen::resolve(params->reference);
        if (!view.isBound())
            return script::projectResult(vm, Result<void>::failure(Diagnostic::error(
                                                 DiagnosticCode::StaleHandle, "generation params handle is stale", {},
                                                 {}, "procgen.scriptStages")));
        return script::projectResult(vm, self->configure(*view));
    });
    cave.addFunc("buildSkeleton",
                 [vm](CaveGeneration* self) { return script::projectResult(vm, self->buildSkeleton()); });
    cave.addFunc("getChamberCount", [](CaveGeneration* self) { return self->chamberCount(); });
    cave.addFunc("setChamber",
                 [vm](CaveGeneration* self, int i, float x, float y, float z, float rx, float ry, float rz) {
                     return script::projectResult(vm, self->setChamber(i, x, y, z, rx, ry, rz));
                 });
    cave.addFunc("sampleField", [vm](CaveGeneration* self) { return script::projectResult(vm, self->sampleField()); });
    cave.addFunc("evolveSurface", [vm](CaveGeneration* self, const std::string& name) {
        if (name == "reactivity")
            return script::projectResult(vm, self->evolveSurface(CaveSurfaceOperation::Reactivity));
        if (name == "reactivePatches")
            return script::projectResult(vm, self->evolveSurface(CaveSurfaceOperation::ReactivePatches));
        if (name == "condensation")
            return script::projectResult(vm, self->evolveSurface(CaveSurfaceOperation::Condensation));
        if (name == "curvature") return script::projectResult(vm, self->evolveSurface(CaveSurfaceOperation::Curvature));
        return script::projectResult(vm, invalidArgument());
    });
    cave.addFunc("deposit", [vm](CaveGeneration* self) { return script::projectResult(vm, self->deposit()); });
    cave.addFunc("field", [vm](CaveGeneration* self) { return projectOwned(vm, self->field()); });
    cave.addFunc("replaceField", [vm](CaveGeneration* self, ScalarField3D* value) {
        return script::projectResult(vm, value ? self->replaceField(*value) : invalidArgument());
    });
    cave.addFunc("buildMesh", [vm](CaveGeneration* self) { return projectOwned(vm, self->buildMesh()); });

    auto house = table.addClass<housegen::HouseGeneration>(
        "HouseGeneration", std::function<housegen::HouseGeneration*()>([] { return new housegen::HouseGeneration(); }),
        true);
    house.addFunc("configure",
                  [vm](housegen::HouseGeneration* self, housegen::HouseRequest* request, housegen::HouseGen* kit) {
                      return script::projectResult(
                          vm, request && kit ? self->configure(*request, kit->library()) : invalidArgument());
                  });
    house.addFunc("nextFloorGrid",
                  [vm](housegen::HouseGeneration* self) { return projectOwned(vm, self->nextFloorGrid()); });
    house.addFunc("emitFloor", [vm](housegen::HouseGeneration* self, Grid2D* grid, const std::string& roomCsv) {
        if (!grid) return script::projectResult(vm, invalidArgument());
        std::vector<std::string> rooms;
        std::istringstream       input(roomCsv);
        std::string              room;
        while (std::getline(input, room, ',')) rooms.push_back(room);
        return script::projectResult(vm, self->emitFloor(*grid, rooms));
    });
    house.addFunc("finish", [vm](housegen::HouseGeneration* self) { return projectOwned(vm, self->finish()); });
    house.addFunc("getFloorCount", [](housegen::HouseGeneration* self) { return self->floorCount(); });
}
}  // namespace eve::procgen
