#pragma once

#include "procgen/Procgen.h"

namespace eve::procgen {

/** @brief Internal Squirrel proxy for a module-owned Params handle. */
struct ScriptProcgenParams {
    explicit ScriptProcgenParams(ProcgenParamsHandleRef value) : reference(value) {}
    ProcgenParamsHandleRef reference;
};

/** @brief Internal Squirrel proxy for a module-owned Grid handle. */
struct ScriptProcgenGrid {
    explicit ScriptProcgenGrid(ProcgenGridHandleRef value) : reference(value) {}
    ProcgenGridHandleRef reference;
};

/** @brief Internal Squirrel proxy for a module-owned rebuild context handle. */
struct ScriptProcgenContext {
    explicit ScriptProcgenContext(ProcgenContextHandleRef value) : reference(value) {}
    ProcgenContextHandleRef reference;
};

}  // namespace eve::procgen
