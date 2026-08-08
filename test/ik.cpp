#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "ik/IK.h"
#include "ik/Skeleton2D.h"
#include "ik/Skeleton3D.h"
#include "ik/Solver2D.h"
#include "ik/Solver3D.h"

#include "common/Exception.h"

#include <cmath>
#include <memory>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace eve::ik;

static bool near(float a, float b, float eps = 1e-3f) {
    return std::fabs(a - b) <= eps;
}

TEST_CASE("ik.module.name") {
    auto *mod = IK::create();
    CHECK_EQ(mod->getName(), std::string("IK"));
    CHECK_EQ(IK::create(), mod);
}

TEST_CASE("ik.skeleton2d.topology") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    CHECK_EQ(sk->getBoneCount(), 1);
    CHECK_EQ(sk->getRootId(), 0);
    CHECK_EQ(sk->getParent(0), -1);

    int b1 = sk->createBone(0, 1.f);
    int b2 = sk->createBone(b1, 1.f);
    CHECK_EQ(sk->getBoneCount(), 3);
    CHECK_EQ(b1, 1);
    CHECK_EQ(b2, 2);
    CHECK_EQ(sk->getParent(b2), b1);
    CHECK_EQ(sk->getChildCount(0), 1);
    CHECK_EQ(sk->getChild(0, 0), b1);
    CHECK(near(sk->getLength(b1), 1.f));
    CHECK(near(sk->totalLengthTo(b2), 2.f));
}

TEST_CASE("ik.skeleton2d.forwardKinematics") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    int b1 = sk->createBone(0, 1.f);
    int b2 = sk->createBone(b1, 1.f);

    sk->setPosition(0, 0.f, 0.f);
    sk->setRotation(0, 0.f);
    sk->setRotation(b1, 0.f);
    sk->setRotation(b2, float(M_PI / 2.0));
    sk->forwardKinematics();

    CHECK(near(sk->getX(b1), 1.f));
    CHECK(near(sk->getY(b1), 0.f));
    CHECK(near(sk->getX(b2), 1.f));
    CHECK(near(sk->getY(b2), 1.f));
}

TEST_CASE("ik.solver2d.reachesTarget") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    int b1 = sk->createBone(0, 1.f);
    int b2 = sk->createBone(b1, 1.f);
    sk->initStraightPose(0.f, 0.f);

    std::unique_ptr<Solver2D> solver(new Solver2D());
    solver->setMaxIterations(32);
    solver->setTolerance(1e-3f);
    solver->addTarget(b2, 1.f, 1.f, 1.f);
    CHECK_EQ(solver->getTargetCount(), 1);
    CHECK(solver->solve(sk.get()));
    CHECK(near(sk->getX(b2), 1.f, 1e-2f));
    CHECK(near(sk->getY(b2), 1.f, 1e-2f));
}

TEST_CASE("ik.solver2d.constraints") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    int b1 = sk->createBone(0, 1.f);
    int b2 = sk->createBone(b1, 1.f);
    sk->initStraightPose(0.f, 0.f);
    sk->setConstraints(b1, -0.2f, 0.2f);
    CHECK(sk->hasConstraints(b1));

    std::unique_ptr<Solver2D> solver(new Solver2D());
    solver->setMaxIterations(64);
    solver->setTolerance(1e-3f);
    solver->addTarget(b2, 0.f, 2.f, 1.f);
    solver->solve(sk.get());

    // Hinge limit on b1 should keep the chain from folding fully upward.
    CHECK(std::fabs(sk->getRotation(b1)) <= 0.25f + 1e-3f);
}

TEST_CASE("ik.skeleton3d.straightPoseAndSolve") {
    std::unique_ptr<Skeleton3D> sk(new Skeleton3D());
    int b1 = sk->createBone(0, 1.f);
    int b2 = sk->createBone(b1, 1.f);
    sk->initStraightPose(0.f, 0.f, 0.f);
    CHECK(near(sk->getX(b2), 2.f));
    CHECK(near(sk->getY(b2), 0.f));
    CHECK(near(sk->getZ(b2), 0.f));

    std::unique_ptr<Solver3D> solver(new Solver3D());
    solver->setMaxIterations(48);
    solver->addTarget(b2, 1.f, 1.f, 0.f, 1.f);
    CHECK(solver->solve(sk.get()));
    CHECK(near(sk->getX(b2), 1.f, 2e-2f));
    CHECK(near(sk->getY(b2), 1.f, 2e-2f));
}

TEST_CASE("ik.factory") {
    auto *mod = IK::create();
    std::unique_ptr<Skeleton2D> sk(mod->newSkeleton2D());
    std::unique_ptr<Solver2D> solver(mod->newSolver2D());
    CHECK_EQ(sk->getBoneCount(), 1);
    CHECK_EQ(solver->getMaxIterations(), 16);
}

TEST_CASE("ik.invalidBoneThrows") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    bool threw = false;
    try {
        sk->getX(99);
    } catch (const eve::Exception &) {
        threw = true;
    }
    CHECK(threw);
}
