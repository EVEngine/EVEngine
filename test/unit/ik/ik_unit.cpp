// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "ik.cpp"
  ]
}
*/
// clang-format on

#include "ik/IK.h"
#include "ik/Skeleton2D.h"
#include "ik/Skeleton3D.h"
#include "ik/Solver2D.h"
#include "ik/Solver3D.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <string>
#include <utility>

namespace {
using namespace eve::ik;
constexpr float halfPi = 1.57079632679f;
void near(float actual, float expected, float tolerance = 1e-3f) { CHECK(std::fabs(actual - expected) <= tolerance); }
}  // namespace

TEST_CASE("ik.skeleton2d.root_children_and_forward_kinematics") {
    Skeleton2D skeleton;
    CHECK_EQ(skeleton.getBoneCount(), 1);
    CHECK_EQ(skeleton.getRootId(), 0);
    CHECK_EQ(skeleton.getParent(0), -1);
    const int shoulder = skeleton.createBone(0, 1.f);
    const int hand     = skeleton.createBone(shoulder, 1.f);
    CHECK_EQ(skeleton.getBoneCount(), 3);
    CHECK_EQ(skeleton.getParent(hand), shoulder);
    CHECK_EQ(skeleton.getChildCount(0), 1);
    CHECK_EQ(skeleton.getChild(0, 0), shoulder);
    near(skeleton.totalLengthTo(hand), 2.f);

    skeleton.setPosition(0, 0.f, 0.f);
    skeleton.setRotation(0, 0.f);
    skeleton.setRotation(shoulder, 0.f);
    skeleton.setRotation(hand, halfPi);
    skeleton.forwardKinematics();
    near(skeleton.getX(shoulder), 1.f);
    near(skeleton.getY(shoulder), 0.f);
    near(skeleton.getX(hand), 1.f);
    near(skeleton.getY(hand), 1.f);
    bool rejected = false;
    try {
        (void)skeleton.getX(99);
    } catch (...) {
        rejected = true;
    }
    REQUIRE(rejected);
}

TEST_CASE("ik.solver2d.reachable_target_and_influence_boundaries") {
    Skeleton2D skeleton;
    const int  joint = skeleton.createBone(0, 1.f);
    const int  tip   = skeleton.createBone(joint, 1.f);
    Solver2D   solver;
    solver.setMaxIterations(32);
    solver.setTolerance(1e-3f);
    solver.addTarget(tip, 1.f, 1.f, 1.f);
    CHECK_EQ(solver.getTargetCount(), 1);
    skeleton.initStraightPose(0.f, 0.f);
    solver.solve(&skeleton);
    near(skeleton.getX(tip), 1.f, 1e-2f);
    near(skeleton.getY(tip), 1.f, 1e-2f);

    solver.clearTargets();
    solver.addTarget(tip, 0.f, 2.f, 1.f);
    skeleton.initStraightPose(0.f, 0.f);
    solver.setInfluence(.5f);
    solver.solve(&skeleton);
    near(skeleton.getX(tip), 1.f, 1e-2f);
    near(skeleton.getY(tip), 1.f, 1e-2f);
    skeleton.initStraightPose(0.f, 0.f);
    solver.setInfluence(0.f);
    solver.solve(&skeleton);
    near(skeleton.getX(tip), 2.f);
    near(skeleton.getY(tip), 0.f);
    bool rejected = false;
    try {
        solver.setInfluence(1.5f);
    } catch (...) {
        rejected = true;
    }
    REQUIRE(rejected);
}

TEST_CASE("ik.solver2d.hinge_limit_restricts_target_pose") {
    Skeleton2D skeleton;
    const int  joint = skeleton.createBone(0, 1.f);
    const int  tip   = skeleton.createBone(joint, 1.f);
    skeleton.initStraightPose(0.f, 0.f);
    skeleton.setConstraints(joint, -.2f, .2f);
    REQUIRE(skeleton.hasConstraints(joint));
    Solver2D solver;
    solver.setMaxIterations(64);
    solver.addTarget(tip, 0.f, 2.f, 1.f);
    solver.solve(&skeleton);
    CHECK(std::fabs(skeleton.getRotation(joint)) <= .251f);
    skeleton.clearConstraints(joint);
    CHECK(!skeleton.hasConstraints(joint));
}

TEST_CASE("ik.solver2d.chain_scope_pins_root_and_ignores_side_branch_target") {
    Skeleton2D skeleton;
    skeleton.createBone(0, 1.f);
    skeleton.createBone(1, 1.f);
    skeleton.createBone(0, 1.f);
    const int chainRoot = skeleton.getChild(0, 0);
    const int side      = skeleton.getChild(0, 1);
    const int tip       = skeleton.getChild(chainRoot, 0);
    skeleton.initStraightPose(10.f, 20.f);

    Solver2D solver;
    solver.setMaxIterations(32);
    solver.addTarget(side, 11.f, 21.f, 1.f);
    REQUIRE(solver.solveChain(&skeleton, chainRoot, tip));
    near(skeleton.getX(tip), 12.f);
    near(skeleton.getY(tip), 20.f);
    solver.clearTargets();
    solver.addTarget(tip, 11.f, 21.f, 1.f);
    REQUIRE(solver.solveChain(&skeleton, chainRoot, tip));
    near(skeleton.getX(0), 10.f);
    near(skeleton.getY(0), 20.f);
    near(skeleton.getX(chainRoot), 11.f);
    near(skeleton.getY(chainRoot), 20.f);
    near(skeleton.getX(side), 11.f);
    near(skeleton.getY(side), 20.f);
    near(skeleton.getX(tip), 11.f, 1e-2f);
    near(skeleton.getY(tip), 21.f, 1e-2f);
    for (const auto& [end, root] : {std::pair{tip, chainRoot}, std::pair{chainRoot, chainRoot}, std::pair{0, 99}}) {
        bool rejected = false;
        try {
            (void)solver.solveChain(&skeleton, end, root);
        } catch (...) {
            rejected = true;
        }
        CHECK(rejected);
    }
}

TEST_CASE("ik.solver3d.reaches_target_and_chain_preserves_other_branches") {
    Skeleton3D skeleton;
    skeleton.createBone(0, 1.f);
    skeleton.createBone(1, 1.f);
    skeleton.createBone(0, 1.f);
    const int chainRoot = skeleton.getChild(0, 0);
    const int side      = skeleton.getChild(0, 1);
    const int tip       = skeleton.getChild(chainRoot, 0);
    skeleton.initStraightPose(0.f, 0.f, 0.f);
    near(skeleton.getX(tip), 2.f);
    Solver3D solver;
    solver.setMaxIterations(48);
    solver.addTarget(tip, 1.f, 1.f, 0.f, 1.f);
    REQUIRE(solver.solveChain(&skeleton, chainRoot, tip));
    near(skeleton.getX(0), 0.f);
    near(skeleton.getX(chainRoot), 1.f);
    near(skeleton.getY(chainRoot), 0.f);
    near(skeleton.getX(side), 1.f);
    near(skeleton.getY(side), 0.f);
    near(skeleton.getX(tip), 1.f, 2e-2f);
    near(skeleton.getY(tip), 1.f, 2e-2f);
}

TEST_CASE("ik.solver3d.pole_selects_bend_direction_without_moving_tip_target") {
    Skeleton3D skeleton;
    const int  joint = skeleton.createBone(0, 1.f);
    const int  tip   = skeleton.createBone(joint, 1.f);
    Solver3D   solver;
    solver.setMaxIterations(64);
    solver.setTolerance(1e-4f);
    solver.addTarget(tip, 1.5f, .5f, 0.f, 1.f);

    skeleton.initStraightPose(0.f, 0.f, 0.f);
    solver.setPole(1.f, 1.f, 0.f, 1.f);
    REQUIRE(solver.hasPole());
    REQUIRE(solver.solveChain(&skeleton, 0, tip));
    CHECK(skeleton.getY(joint) > .1f);

    skeleton.initStraightPose(0.f, 0.f, 0.f);
    solver.setPole(1.f, -1.f, 0.f, 1.f);
    REQUIRE(solver.solveChain(&skeleton, 0, tip));
    CHECK(skeleton.getY(joint) < -.1f);
    solver.clearPole();
    CHECK(!solver.hasPole());
}

TEST_CASE("ik.solver3d.tip_orientation_can_be_set_and_cleared") {
    Skeleton3D skeleton;
    const int  joint = skeleton.createBone(0, 1.f);
    const int  tip   = skeleton.createBone(joint, 1.f);
    skeleton.initStraightPose(0.f, 0.f, 0.f);
    Solver3D solver;
    solver.setMaxIterations(48);
    solver.addTarget(tip, 2.f, 0.f, 0.f, 1.f);
    solver.setTipRotation(tip, 0.f, halfPi, 1.f);
    REQUIRE(solver.solveChain(&skeleton, 0, tip));
    near(skeleton.getX(tip), 2.f, 2e-2f);
    near(skeleton.getOrientationZ(tip), 1.f, 1e-2f);
    near(skeleton.getRotationPitch(tip), halfPi, 1e-2f);
    solver.clearTipRotation();
    REQUIRE(solver.solveChain(&skeleton, 0, tip));
    near(skeleton.getOrientationZ(tip), 0.f, 1e-2f);
}

TEST_CASE("ik.module_factory_returns_live_singleton_and_owned_solver_instances") {
    auto* module = IK::create();
    REQUIRE(module != nullptr);
    CHECK_EQ(module->getName(), std::string("IK"));
    CHECK_EQ(IK::create(), module);
    auto* skeleton = module->newSkeleton2D();
    auto* solver   = module->newSolver2D();
    REQUIRE(skeleton != nullptr);
    REQUIRE(solver != nullptr);
    CHECK_EQ(skeleton->getBoneCount(), 1);
    CHECK_EQ(solver->getMaxIterations(), 16);
    delete solver;
    delete skeleton;
}
