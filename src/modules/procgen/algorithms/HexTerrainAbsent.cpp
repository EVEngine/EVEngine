/**
 * @file HexTerrainAbsent.cpp
 * @brief Stubs when procgen is built without the hexmap module.
 */

#include "procgen/algorithms/HexTerrainBindings.h"

#include "common/Diagnostic.h"
#include "common/Result.h"
#include "common/SquirrelBinding.h"
#include "common/Value.h"
#include "procgen/GeneratorRegistry.h"
#include "procgen/Procgen.h"
#include "procgen/ProcgenScriptObjects.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <string>

namespace eve::procgen {
namespace {

[[nodiscard]] Result<Value> missingAlgorithm(const char* name) {
    return Result<Value>::failure(Diagnostic::error(
        DiagnosticCode::NotFound, std::string(name) + " requires the hexmap module", "algorithm", {}, "procgen"));
}

}  // namespace

void registerHexTerrainAlgorithms(GeneratorRegistry&) {}

Result<Value> generateHexTerrain(const Params&) { return missingAlgorithm("generateHexTerrain"); }

Result<Value> generateHexSphere(const Params&) { return missingAlgorithm("generateHexSphere"); }

void exposeHexTerrain(ssq::Class& cls) {
    const auto missing = [](HSQUIRRELVM vm, const char* name) {
        return eve::script::projectResult(vm, missingAlgorithm(name), [](Value value) { return value; });
    };
    cls.addFunc("generateHexTerrain", [vm = cls.getHandle(), missing](Procgen*, ScriptProcgenParams*) -> ssq::Table {
        return missing(vm, "generateHexTerrain");
    });
    cls.addFunc("generateHexSphere", [vm = cls.getHandle(), missing](Procgen*, ScriptProcgenParams*) -> ssq::Table {
        return missing(vm, "generateHexSphere");
    });
}

}  // namespace eve::procgen
