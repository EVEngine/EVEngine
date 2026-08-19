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
class IK : public Module {
public:
    Module_REG(IK);
    IK() = default;
    ~IK() override = default;

    Skeleton2D *newSkeleton2D();
    Skeleton3D *newSkeleton3D();
    Solver2D   *newSolver2D();
    Solver3D   *newSolver3D();
};

}  // namespace eve::ik
