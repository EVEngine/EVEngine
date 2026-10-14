#pragma once

#include "common/Module.h"

namespace eve::ik {

class Skeleton2D;
class Skeleton3D;
class Solver2D;
class Solver3D;

/**
 * @brief Inverse Kinematics module — wraps header-only sunxfancy/ik.hpp (FABRIK).
 * Script: `ik <- eve.IK();`
 *
 * Provides 2D/3D skeleton + solver factories. No overloads: use Skeleton2D /
 * Skeleton3D and Solver2D / Solver3D explicitly.
 */
class EVENGINE_API_FOUNDATION IK : public Module {
public:
    Module_REG(IK);
    /** @brief Default-constructs the IK module. */
    IK() = default;
    /** @brief No owned skeletons/solvers; factories return caller-owned pointers. */
    ~IK() override = default;

    /** @brief Creates an empty 2D skeleton. @ownership Caller deletes. */
    Skeleton2D *newSkeleton2D();
    /** @brief Creates an empty 3D skeleton. @ownership Caller deletes. */
    Skeleton3D *newSkeleton3D();
    /** @brief Creates a 2D FABRIK solver. @ownership Caller deletes. */
    Solver2D   *newSolver2D();
    /** @brief Creates a 3D FABRIK solver. @ownership Caller deletes. */
    Solver3D   *newSolver3D();
};

}  // namespace eve::ik
