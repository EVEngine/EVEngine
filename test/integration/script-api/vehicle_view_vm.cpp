// eve-test-policy: {"schema":"evengine.test-policy","version":1,"module":"vehicle"}
#include "ScriptTest.h"
#include "vehicle/Vehicle.h"
#include "zeroerr/unittest.h"

namespace {
constexpr const char* viewScript = R"SQ(
function vehicleViewContract() {
    local vehicle = eve.Vehicle()
    vehicle.registerVehiclesFromJson("[{\"id\":\"jeep\"}]")
    local created = vehicle.newVehicle("jeep", 5.0, 6.0, 0.0, "")
    if (created == null) return false
    foreach (entity in eve.view(eve.VehicleEntity)) {
        if (entity.getId() == created.getId()) return true
    }
    return false
}
)SQ";

class VehicleScriptContract final : public ScriptTest {
public:
    VehicleScriptContract() : ScriptTest(viewScript) {}
    bool run() { return callScript("vehicleViewContract"); }
};
}  // namespace

TEST_CASE("vehicle.script.eveViewFindsConstructedEntity") {
    VehicleScriptContract script;
    REQUIRE(script.run());
}
