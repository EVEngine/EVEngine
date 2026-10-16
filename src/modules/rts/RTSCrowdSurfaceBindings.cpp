#include "common/SquirrelBinding.h"
#include "rts/RTS.h"
#include "rts/RTSScriptInternal.h"

namespace eve::rts::script_internal {
void exposeCrowdSurfaceBindings(ssq::Class& cls) {
    const auto vm = cls.getHandle();
    cls.addFunc("configureCrowdSurfaceTracking", [vm](RTS* self, ssq::Table input) -> ssq::Table {
        CrowdSurfaceTracking settings;
        try {
            const auto integer = [&](const char* key) {
                auto value = input.find(key);
                if (value.getType() != ssq::Type::INTEGER)
                    throw ssq::TypeException("Tracking identity/budget requires exact integers");
                return value.to<std::int64_t>();
            };
            const auto positive = [&](const char* key) {
                auto value = integer(key);
                if (value <= 0) throw ssq::TypeException("Tracking identities and revisions must be positive");
                return static_cast<std::uint64_t>(value);
            };
            settings.surface            = decltype(settings.surface)(positive("surfaceId"));
            settings.movement           = decltype(settings.movement)(positive("movementId"));
            settings.movementRevision   = positive("movementRevision");
            settings.costModelRevision  = positive("costModelRevision");
            settings.clearanceRadius    = input.get<float>("clearanceRadius");
            settings.maxInflationChecks = integer("maxInflationChecks");
        } catch (const std::exception& error) {
            return script::projectStatusResult(
                vm, Status::failure(
                        Diagnostic::error(DiagnosticCode::InvalidArgument, error.what(), "crowdSurfaceTracking")));
        }
        return script::projectResult(vm, self->configureCrowdSurfaceTracking(settings));
    });
    cls.addFunc("configureCrowdFlowNavigation", [vm](RTS* self, ssq::Table input) {
        CrowdFlowNavigationSettings settings;
        try {
            const auto quota = [&](const char* key, int maximum) {
                auto value = input.find(key);
                if (value.getType() != ssq::Type::INTEGER)
                    throw ssq::TypeException("Flow quota requires exact integer");
                auto count = value.to<std::int64_t>();
                if (count < 1 || count > maximum) throw ssq::TypeException("Flow quota out of range");
                return static_cast<int>(count);
            };
            settings.maxWorkPerTick   = quota("maxWorkPerTick", 1000000);
            settings.maxStartsPerTick = quota("maxStartsPerTick", 16);
        } catch (const std::exception& error) {
            return script::projectStatusResult(
                vm, Status::failure(
                        Diagnostic::error(DiagnosticCode::InvalidArgument, error.what(), "crowdFlowNavigation")));
        }
        return script::projectResult(vm, self->configureCrowdFlowNavigation(settings));
    });
    cls.addFunc("clearCrowdFlowNavigation", [](RTS* self) { self->clearCrowdFlowNavigation(); });
    cls.addFunc("getCrowdFlowNavigationStatus", [vm](RTS* self) {
        const auto state = self->getCrowdFlowNavigationStatus();
        return script::projectResult(vm,
                                     Result<Value>::success(Value(Value::Object{
                                         {"pendingGoals", Value(static_cast<std::int64_t>(state.pendingGoals))},
                                         {"readyGoals", Value(static_cast<std::int64_t>(state.readyGoals))},
                                         {"lastWork", Value(state.lastWork)},
                                         {"lastStarts", Value(state.lastStarts)},
                                         {"prepared", Value(std::to_string(state.prepared))},
                                         {"cacheHits", Value(std::to_string(state.cacheHits))},
                                         {"cachePressure", Value(state.cachePressure)}})),
                                     [](Value value) { return value; });
    });
    cls.addFunc("clearCrowdSurfaceTracking", [](RTS* self) { self->clearCrowdSurfaceTracking(); });
}
}  // namespace eve::rts::script_internal
