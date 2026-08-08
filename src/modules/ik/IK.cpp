#include "ik/IK.h"
#include "ik/Skeleton2D.h"
#include "ik/Skeleton3D.h"
#include "ik/Solver2D.h"
#include "ik/Solver3D.h"

#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::ik {

Module_IMPL(IK, new IK());

Skeleton2D *IK::newSkeleton2D() { return new Skeleton2D(); }
Skeleton3D *IK::newSkeleton3D() { return new Skeleton3D(); }
Solver2D   *IK::newSolver2D() { return new Solver2D(); }
Solver3D   *IK::newSolver3D() { return new Solver3D(); }

void IK::expose(ssq::Table &table) {
    auto cls = table.addClass(name, IK::create, false);
    expose(cls);

    auto sk2 = table.addClass<Skeleton2D>(
        "Skeleton2D", std::function<Skeleton2D *()>([]() -> Skeleton2D * { return nullptr; }),
        true);
    sk2.addFunc("createBone", &Skeleton2D::createBone);
    sk2.addFunc("getBoneCount", &Skeleton2D::getBoneCount);
    sk2.addFunc("getRootId", &Skeleton2D::getRootId);
    sk2.addFunc("getParent", &Skeleton2D::getParent);
    sk2.addFunc("getChildCount", &Skeleton2D::getChildCount);
    sk2.addFunc("getChild", &Skeleton2D::getChild);
    sk2.addFunc("getLength", &Skeleton2D::getLength);
    sk2.addFunc("setLength", &Skeleton2D::setLength);
    sk2.addFunc("setPosition", &Skeleton2D::setPosition);
    sk2.addFunc("getX", &Skeleton2D::getX);
    sk2.addFunc("getY", &Skeleton2D::getY);
    sk2.addFunc("setOrientation", &Skeleton2D::setOrientation);
    sk2.addFunc("getOrientationX", &Skeleton2D::getOrientationX);
    sk2.addFunc("getOrientationY", &Skeleton2D::getOrientationY);
    sk2.addFunc("setRotation", &Skeleton2D::setRotation);
    sk2.addFunc("getRotation", &Skeleton2D::getRotation);
    sk2.addFunc("setConstraints", &Skeleton2D::setConstraints);
    sk2.addFunc("clearConstraints", &Skeleton2D::clearConstraints);
    sk2.addFunc("hasConstraints", &Skeleton2D::hasConstraints);
    sk2.addFunc("initStraightPose", &Skeleton2D::initStraightPose);
    sk2.addFunc("bind", &Skeleton2D::bind);
    sk2.addFunc("forwardKinematics", &Skeleton2D::forwardKinematics);
    sk2.addFunc("updateRotations", &Skeleton2D::updateRotations);
    sk2.addFunc("totalLengthTo", &Skeleton2D::totalLengthTo);

    auto sk3 = table.addClass<Skeleton3D>(
        "Skeleton3D", std::function<Skeleton3D *()>([]() -> Skeleton3D * { return nullptr; }),
        true);
    sk3.addFunc("createBone", &Skeleton3D::createBone);
    sk3.addFunc("getBoneCount", &Skeleton3D::getBoneCount);
    sk3.addFunc("getRootId", &Skeleton3D::getRootId);
    sk3.addFunc("getParent", &Skeleton3D::getParent);
    sk3.addFunc("getChildCount", &Skeleton3D::getChildCount);
    sk3.addFunc("getChild", &Skeleton3D::getChild);
    sk3.addFunc("getLength", &Skeleton3D::getLength);
    sk3.addFunc("setLength", &Skeleton3D::setLength);
    sk3.addFunc("setPosition", &Skeleton3D::setPosition);
    sk3.addFunc("getX", &Skeleton3D::getX);
    sk3.addFunc("getY", &Skeleton3D::getY);
    sk3.addFunc("getZ", &Skeleton3D::getZ);
    sk3.addFunc("setOrientation", &Skeleton3D::setOrientation);
    sk3.addFunc("getOrientationX", &Skeleton3D::getOrientationX);
    sk3.addFunc("getOrientationY", &Skeleton3D::getOrientationY);
    sk3.addFunc("getOrientationZ", &Skeleton3D::getOrientationZ);
    sk3.addFunc("setRotation", &Skeleton3D::setRotation);
    sk3.addFunc("getRotationYaw", &Skeleton3D::getRotationYaw);
    sk3.addFunc("getRotationPitch", &Skeleton3D::getRotationPitch);
    sk3.addFunc("setConstraints", &Skeleton3D::setConstraints);
    sk3.addFunc("clearConstraints", &Skeleton3D::clearConstraints);
    sk3.addFunc("hasConstraints", &Skeleton3D::hasConstraints);
    sk3.addFunc("initStraightPose", &Skeleton3D::initStraightPose);
    sk3.addFunc("bind", &Skeleton3D::bind);
    sk3.addFunc("forwardKinematics", &Skeleton3D::forwardKinematics);
    sk3.addFunc("updateRotations", &Skeleton3D::updateRotations);
    sk3.addFunc("totalLengthTo", &Skeleton3D::totalLengthTo);

    auto s2 = table.addClass<Solver2D>(
        "Solver2D", std::function<Solver2D *()>([]() -> Solver2D * { return nullptr; }), true);
    s2.addFunc("setKeepTrace", &Solver2D::setKeepTrace);
    s2.addFunc("getKeepTrace", &Solver2D::getKeepTrace);
    s2.addFunc("setMaxIterations", &Solver2D::setMaxIterations);
    s2.addFunc("getMaxIterations", &Solver2D::getMaxIterations);
    s2.addFunc("setTolerance", &Solver2D::setTolerance);
    s2.addFunc("getTolerance", &Solver2D::getTolerance);
    s2.addFunc("setForce", &Solver2D::setForce);
    s2.addFunc("getForce", &Solver2D::getForce);
    s2.addFunc("clearTargets", &Solver2D::clearTargets);
    s2.addFunc("addTarget", &Solver2D::addTarget);
    s2.addFunc("getTargetCount", &Solver2D::getTargetCount);
    s2.addFunc("solve", &Solver2D::solve);
    s2.addFunc("step", &Solver2D::step);
    s2.addFunc("getTraceSize", &Solver2D::getTraceSize);
    s2.addFunc("clearTrace", &Solver2D::clearTrace);

    auto s3 = table.addClass<Solver3D>(
        "Solver3D", std::function<Solver3D *()>([]() -> Solver3D * { return nullptr; }), true);
    s3.addFunc("setKeepTrace", &Solver3D::setKeepTrace);
    s3.addFunc("getKeepTrace", &Solver3D::getKeepTrace);
    s3.addFunc("setMaxIterations", &Solver3D::setMaxIterations);
    s3.addFunc("getMaxIterations", &Solver3D::getMaxIterations);
    s3.addFunc("setTolerance", &Solver3D::setTolerance);
    s3.addFunc("getTolerance", &Solver3D::getTolerance);
    s3.addFunc("setForce", &Solver3D::setForce);
    s3.addFunc("getForce", &Solver3D::getForce);
    s3.addFunc("clearTargets", &Solver3D::clearTargets);
    s3.addFunc("addTarget", &Solver3D::addTarget);
    s3.addFunc("getTargetCount", &Solver3D::getTargetCount);
    s3.addFunc("solve", &Solver3D::solve);
    s3.addFunc("step", &Solver3D::step);
    s3.addFunc("getTraceSize", &Solver3D::getTraceSize);
    s3.addFunc("clearTrace", &Solver3D::clearTrace);
}

void IK::expose(ssq::Class &cls) {
    cls.addFunc("getName", &IK::getName);
    cls.addFunc("newSkeleton2D", &IK::newSkeleton2D);
    cls.addFunc("newSkeleton3D", &IK::newSkeleton3D);
    cls.addFunc("newSolver2D", &IK::newSolver2D);
    cls.addFunc("newSolver3D", &IK::newSolver3D);
}

}  // namespace eve::ik
