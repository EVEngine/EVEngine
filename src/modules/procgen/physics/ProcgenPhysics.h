#pragma once

#include "common/Module.h"

namespace eve::procgen_physics {
class GtsTerrainColliderRuntime;
/** @brief Script bridge for optional GTS terrain physics composition. */
class ProcgenPhysics : public Module {
public:
    Module_REG(ProcgenPhysics);
    /** @brief Allocate a caller-owned GTS terrain collider runtime. */
    [[nodiscard]] GtsTerrainColliderRuntime* newGtsTerrainColliderRuntime();
};
}