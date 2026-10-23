#pragma once
#include <simplesquirrel/simplesquirrel.hpp>
#include "climbing/Climbing.h"
namespace eve::climbing {
/** @brief ScriptClimbingRuntime public API. */
struct ScriptClimbingRuntime {
    /** @brief Constructs a ScriptClimbingRuntime. */
    explicit ScriptClimbingRuntime(ClimbingRuntimeHandleRef value) : reference(value) {}
    /** @brief Releases ScriptClimbingRuntime resources. */
    ~ScriptClimbingRuntime() noexcept {
        /** @brief Release. */
        Climbing::release(reference).ignore("script climbing runtime proxy destruction");
    }
    ClimbingRuntimeHandleRef reference;
};

// Internal bindings share the owned runtime proxy and one result projection.
/** @brief Project climbing advance. */
eve::Value projectClimbingAdvance(ClimbingAdvance value);
/** @brief Expose climbing motion bindings. */
void       exposeClimbingMotionBindings(ssq::Class& runtime, HSQUIRRELVM vm);
}  // namespace eve::climbing
