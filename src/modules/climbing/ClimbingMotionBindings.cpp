#include "climbing/ClimbingBindingInternal.h"
#include "common/SquirrelBinding.h"
#include "physics/World3D.h"
namespace eve::climbing {
namespace {}  // namespace
void exposeClimbingMotionBindings(ssq::Class& runtime, HSQUIRRELVM vm) {
    runtime.addFunc(
        "advance", [vm](ScriptClimbingRuntime* value, physics::World3D* world, std::int64_t tick, float deltaSeconds) {
            if (!value || !world || tick < 0)
                return eve::script::projectResult(
                    vm,
                    eve::Result<ClimbingAdvance>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument, "runtime, world, and non-negative tick are required",
                        "advance", {}, "climbing.binding")),
                    projectClimbingAdvance);
            auto resolved = Climbing::resolve(value->reference);
            if (!resolved.isBound())
                return eve::script::projectResult(
                    vm,
                    eve::Result<ClimbingAdvance>::failure(eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle,
                                                                                 "climbing runtime handle is stale",
                                                                                 "runtime", {}, "climbing.binding")),
                    projectClimbingAdvance);
            auto duration = eve::Duration::fromSeconds(deltaSeconds);
            if (!duration) return eve::script::projectStatusResult(vm, duration.status());
            return eve::script::projectResult(
                vm,
                resolved->advance(
                    *world, {eve::SimulationTick(static_cast<std::uint64_t>(tick)), std::move(duration).takeValue()}),
                projectClimbingAdvance);
        });
    runtime.addFunc("advanceWarped", [vm](ScriptClimbingRuntime* value, physics::World3D* world, std::int64_t tick,
                                          float dt, float dx, float dy, float dz, float fx, float fz,
                                          bool ignoreObstacle) {
        if (!value || !world || tick < 0)
            return eve::script::projectResult(
                vm,
                eve::Result<ClimbingAdvance>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::InvalidArgument, "runtime, world, and nonnegative tick required",
                    "advanceWarped", {}, "climbing.binding")),
                projectClimbingAdvance);
        auto resolved = Climbing::resolve(value->reference);
        if (!resolved.isBound())
            return eve::script::projectResult(vm,
                                              eve::Result<ClimbingAdvance>::failure(eve::Diagnostic::error(
                                                  eve::DiagnosticCode::StaleHandle, "climbing runtime handle is stale",
                                                  "runtime", {}, "climbing.binding")),
                                              projectClimbingAdvance);
        auto duration = eve::Duration::fromSeconds(dt);
        if (!duration) return eve::script::projectStatusResult(vm, duration.status());
        ClimbingMotionInput motion;
        motion.rootTranslation  = {dx, dy, dz};
        motion.facing           = {fx, 0.f, fz};
        motion.hasRootMotion    = true;
        motion.rootMotionPolicy = ClimbingRootMotionPolicy::PreserveSuppliedDelta;
        motion.obstacleCollision =
            ignoreObstacle ? ClimbingObstacleCollision::IgnoreTraversedShape : ClimbingObstacleCollision::Collide;
        return eve::script::projectResult(
            vm,
            resolved->advance(*world,
                              {eve::SimulationTick(static_cast<std::uint64_t>(tick)), std::move(duration).takeValue()},
                              motion),
            projectClimbingAdvance);
    });
}
}  // namespace eve::climbing
