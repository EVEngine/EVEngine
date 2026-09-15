#pragma once
#include <simplesquirrel/simplesquirrel.hpp>
#include "climbing/Climbing.h"
namespace eve::climbing {
struct ScriptClimbingRuntime {
    explicit ScriptClimbingRuntime(ClimbingRuntimeHandleRef value) : reference(value) {}
    ~ScriptClimbingRuntime() noexcept {
        Climbing::release(reference).ignore("script climbing runtime proxy destruction");
    }
    ClimbingRuntimeHandleRef reference;
};

// Internal bindings share the owned runtime proxy and one result projection.
eve::Value projectClimbingAdvance(ClimbingAdvance value);
void       exposeClimbingMotionBindings(ssq::Class& runtime, HSQUIRRELVM vm);
}  // namespace eve::climbing
