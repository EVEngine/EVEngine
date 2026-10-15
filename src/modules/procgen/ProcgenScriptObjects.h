#pragma once

#include "procgen/Procgen.h"

namespace eve::procgen {

/** @brief Internal Squirrel proxy for a module-owned Params handle. */
struct ScriptProcgenParams {
    /** @brief Constructs a ScriptProcgenParams. */
    explicit ScriptProcgenParams(ProcgenParamsHandleRef value) : reference(value) {}
    ProcgenParamsHandleRef reference;
};

/** @brief Internal Squirrel proxy for a module-owned Grid handle. */
struct ScriptProcgenGrid {
    /** @brief Constructs a ScriptProcgenGrid. */
    explicit ScriptProcgenGrid(ProcgenGridHandleRef value) : reference(value) {}
    ProcgenGridHandleRef reference;
};

/** @brief Internal Squirrel proxy for a module-owned rebuild context handle. */
struct ScriptProcgenContext {
    /** @brief Constructs a ScriptProcgenContext. */
    explicit ScriptProcgenContext(ProcgenContextHandleRef value) : reference(value) {}
    ProcgenContextHandleRef reference;
};

}  // namespace eve::procgen
