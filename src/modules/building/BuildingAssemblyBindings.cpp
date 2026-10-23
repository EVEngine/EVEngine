#include "building/BuildingAssemblyProtocol.h"
#include "building/PlacementWorld.h"
#include "common/SquirrelBinding.h"
namespace eve::building {
void exposeHouseBindings(ssq::Table& table) {
    const auto vm = table.getHandle();
    table.addFunc("buildingHouseRequest", [vm](PlacementWorld* world, const ssq::Object& request) {
        auto execute = [&]() -> Result<Value> {
            if (!world)
                return Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "World is required"));
            auto value = script::valueFromSquirrel(request);
            if (!value.ok()) return Result<Value>::failure(value.status());
            auto result = executeHouseRequest(*world, value.value());
            if (!result.ok()) return result;
            auto projected = script::pushValue(vm, result.value());
            if (!projected.ok()) return Result<Value>::failure(projected.status());
            sq_pop(vm, 1);
            return result;
        };
        return script::projectResult(vm, execute(), [](Value value) { return value; });
    });
}
}  // namespace eve::building
