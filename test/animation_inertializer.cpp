#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <vector>
#include "animation/AnimClip.h"
#include "animation/AnimClipRegistry.h"
#include "animation/AnimImporter.h"
#include "animation/AnimInertializer.h"
#include "animation/AnimLattice.h"
#include "animation/AnimPlayer.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/Animation.h"
#include "animation/DynamicBoneSolver.h"
#include "animation/FootIKSolver.h"
#include "animation/OrientationWarping.h"
#include "animation/PhysicalBalancePose.h"
#include "animation/ProceduralBoneConfig.h"
#include "animation/editing/ProceduralBoneOverlay.h"
#include "common/Exception.h"
#include "common/Status.h"
#include "data/ByteData.h"
#include "filesystem/Filesystem.h"
#include "model3d/Model3D.h"
#include "model3d/ModelData.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// animation_inertializer.cpp
using namespace eve::animation;

TEST_CASE("animation.inertializer.ownsHistoryAndHonorsBoneDurations") {
    AnimInertializer inertia;
    AnimPose         source(3), previous(3), target(3), priorTarget(3);
    for (int i = 0; i < 3; ++i) source.setLocalPosition(i, 2.f, 0.f, 0.f);
    previous.copyFrom(&source);
    std::array<float, 3> factors{0.f, .75f, 1.f};
    REQUIRE(inertia.begin(source, previous, target, priorTarget, .01f, .2f, factors).ok());
    CHECK(inertia.pose().getLocalPositionX(0) == 0.f);
    CHECK(inertia.pose().getLocalPositionX(1) == 2.f);
    source.setLocalPosition(1, 100.f, 0.f, 0.f);
    factors[1] = 1.f;
    REQUIRE(inertia.evaluate(target, .16f).ok());
    CHECK(inertia.pose().getLocalPositionX(1) == 0.f);
    CHECK(inertia.pose().getLocalPositionX(2) > 0.f);
    REQUIRE(inertia.evaluate(target, .2f).ok());
    CHECK(inertia.pose().getLocalPositionX(2) == 0.f);
    // Evaluating absolute time again has no integration/order dependence.
    REQUIRE(inertia.evaluate(target, .16f).ok());
    float atSixteen = inertia.pose().getLocalPositionX(2);
    REQUIRE(inertia.evaluate(target, .03f).ok());
    REQUIRE(inertia.evaluate(target, .16f).ok());
    CHECK(inertia.pose().getLocalPositionX(2) == atSixteen);
}

TEST_CASE("animation.inertializer.invalidInputsLeaveTransitionAndOutputUnchanged") {
    AnimInertializer inertia;
    AnimPose         source(1), previous(1), target(1), priorTarget(1), wrong(2);
    CHECK(!inertia.evaluate(target, 0.f).ok());
    source.setLocalPosition(0, 2.f, 0.f, 0.f);
    previous.copyFrom(&source);
    REQUIRE(inertia.begin(source, previous, target, priorTarget, .01f, .2f).ok());
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(!inertia.begin(source, previous, wrong, priorTarget, .01f, .2f).ok());
    CHECK(!inertia.begin(source, previous, target, priorTarget, 0.f, .2f).ok());
    CHECK(!inertia.begin(source, previous, target, priorTarget, .01f, nan).ok());
    std::array<float, 1> factors{-1.f};
    CHECK(!inertia.begin(source, previous, target, priorTarget, .01f, .2f, factors).ok());
    CHECK(!inertia.evaluate(target, -1.f).ok());
    CHECK(!inertia.evaluate(wrong, 0.f).ok());
    target.setLocalPosition(0, nan, 0.f, 0.f);
    CHECK(!inertia.evaluate(target, .1f).ok());
    CHECK(inertia.pose().getLocalPositionX(0) == 2.f);
    target.setLocalPosition(0, 0.f, 0.f, 0.f);
    REQUIRE(inertia.evaluate(target, .2f).ok());
    CHECK(inertia.pose().getLocalPositionX(0) == 0.f);
}

TEST_CASE("animation.inertializer.interruptionPreservesMovingRotationAndVelocity") {
    AnimInertializer inertia;
    AnimPose         source(1), previous(1), target(1), priorTarget(1);
    source.setLocalPosition(0, 2.f, 0.f, 0.f);
    previous.setLocalPosition(0, 1.97f, 0.f, 0.f);
    source.setLocalRotation(0, 0.f, std::sin(.4f), 0.f, std::cos(.4f));
    previous.setLocalRotation(0, 0.f, std::sin(.39f), 0.f, std::cos(.39f));
    target.setLocalRotation(0, 0.f, std::sin(.1f), 0.f, std::cos(.1f));
    priorTarget.setLocalRotation(0, 0.f, std::sin(.095f), 0.f, std::cos(.095f));
    REQUIRE(inertia.begin(source, previous, target, priorTarget, .01f, .2f).ok());
    target.setLocalRotation(0, 0.f, std::sin(.10005f), 0.f, std::cos(.10005f));
    REQUIRE(inertia.evaluate(target, .0001f).ok());
    auto q = inertia.pose().local(0);
    CHECK(std::abs((2.f * std::atan2(q.qy, q.qw) - .8f) / .0001f - 2.f) < .02f);
    CHECK(std::abs((q.px - 2.f) / .0001f - 3.f) < .02f);
    REQUIRE(inertia.evaluate(target, .08f).ok());
    previous.copyFrom(&inertia.pose());
    REQUIRE(inertia.evaluate(target, .09f).ok());
    source.copyFrom(&inertia.pose());
    float expectedVelocity = (source.local(0).px - previous.local(0).px) / .01f;
    target.setLocalPosition(0, -3.f, 0.f, 0.f);
    priorTarget.copyFrom(&target);
    REQUIRE(inertia.begin(source, previous, target, priorTarget, .01f, .15f).ok());
    CHECK(std::abs(inertia.pose().local(0).px - source.local(0).px) < 1e-5f);
    REQUIRE(inertia.evaluate(target, .0001f).ok());
    CHECK(std::abs((inertia.pose().local(0).px - source.local(0).px) / .0001f - expectedVelocity) < .03f);
}

// animation_lattice.cpp
using namespace eve::animation;

namespace {

bool approx(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

/** @brief Minimal 4-vertex quad OBJ used to exercise the ModelData bind path. */
const char* kQuadObj =
    "v -1 -1 0\n"
    "v 1 -1 0\n"
    "v 1 1 0\n"
    "v -1 1 0\n"
    "f 1 2 3\n"
    "f 1 3 4\n";

eve::model3d::ModelData* loadQuadModel() {
    auto* fs = eve::filesystem::Filesystem::create();
    if (!fs) return nullptr;
    fs->setIdentity("animation_lattice_test", true);
    fs->setupWriteDirectory();
    eve::data::ByteData bytes(kQuadObj, std::strlen(kQuadObj));
    auto*               mod = eve::model3d::Model3D::create();
    return mod->newModelData(&bytes, ".obj");
}

}  // namespace

TEST_CASE("animation.lattice.identityDefault") {
    AnimLattice lat(2, 2, 2);
    lat.setSize(2.f, 2.f, 2.f);
    lat.setOrigin(0.f, 0.f, 0.f);

    const float bind[9] = {-0.75f, 0.25f, 0.5f, 0.1f, -0.3f, 0.9f, 0.4f, 0.6f, -0.8f};
    lat.bindPositions(bind, 3);
    REQUIRE(lat.getVertexCount() == 3);

    std::vector<float> out;
    REQUIRE(lat.updateDeformedPositions());
    for (int v = 0; v < 3; ++v) {
        CHECK(approx(lat.getDeformedPositionX(v), bind[v * 3 + 0]));
        CHECK(approx(lat.getDeformedPositionY(v), bind[v * 3 + 1]));
        CHECK(approx(lat.getDeformedPositionZ(v), bind[v * 3 + 2]));
    }

    // Default-constructed lattice is a valid identity too.
    AnimLattice def;
    CHECK(def.getPointCount() == 8);
    std::vector<float> defOut;
    REQUIRE(def.deformPositionsTo({0.5f, 0.5f, 0.5f}, defOut));
    CHECK(approx(defOut[0], 0.5f));
    CHECK(approx(defOut[1], 0.5f));
    CHECK(approx(defOut[2], 0.5f));
}

TEST_CASE("animation.lattice.uniformScale") {
    AnimLattice lat(2, 2, 2);
    lat.setSize(2.f, 2.f, 2.f);
    lat.setOrigin(0.f, 0.f, 0.f);
    lat.setScale(2.f, 1.f, 0.5f);

    const float bind[9] = {0.5f, 0.f, 0.f, 0.f, 0.f, 0.f, -0.5f, 0.75f, 1.f};
    lat.bindPositions(bind, 3);
    REQUIRE(lat.updateDeformedPositions());

    // Scaling happens about the lattice origin.
    CHECK(approx(lat.getDeformedPositionX(0), 1.f));
    CHECK(approx(lat.getDeformedPositionY(0), 0.f));
    CHECK(approx(lat.getDeformedPositionZ(0), 0.f));
    CHECK(approx(lat.getDeformedPositionX(1), 0.f));
    CHECK(approx(lat.getDeformedPositionY(1), 0.f));
    CHECK(approx(lat.getDeformedPositionZ(1), 0.f));
    CHECK(approx(lat.getDeformedPositionX(2), -1.f));
    CHECK(approx(lat.getDeformedPositionY(2), 0.75f));
    CHECK(approx(lat.getDeformedPositionZ(2), 0.5f));
}

TEST_CASE("animation.lattice.cornerScaleTrilinear") {
    AnimLattice lat(2, 2, 2);
    lat.setSize(2.f, 2.f, 2.f);
    lat.setOrigin(0.f, 0.f, 0.f);
    lat.setPointScale(1, 1, 1, 2.f, 1.f, 1.f);

    // Corner vertex: exactly the scaled control point.
    std::vector<float> corner = {1.f, 1.f, 1.f};
    std::vector<float> out;
    REQUIRE(lat.deformPositionsTo(corner, out));
    CHECK(approx(out[0], 2.f));
    CHECK(approx(out[1], 1.f));
    CHECK(approx(out[2], 1.f));

    // Center vertex: all 8 weights are 1/8 → scale x = 1 + 1/8.
    std::vector<float> center = {0.5f, 0.5f, 0.5f};
    REQUIRE(lat.deformPositionsTo(center, out));
    CHECK(approx(out[0], 0.5f * 1.125f));
    CHECK(approx(out[1], 0.5f));
    CHECK(approx(out[2], 0.5f));
}

TEST_CASE("animation.lattice.offsetBulge") {
    AnimLattice lat(2, 2, 2);
    lat.setSize(2.f, 2.f, 2.f);
    lat.setOrigin(0.f, 0.f, 0.f);
    lat.setPointOffset(1, 1, 1, 0.4f, -0.2f, 0.f);

    std::vector<float> corner = {1.f, 1.f, 1.f};
    std::vector<float> out;
    REQUIRE(lat.deformPositionsTo(corner, out));
    CHECK(approx(out[0], 1.4f));
    CHECK(approx(out[1], 0.8f));
    CHECK(approx(out[2], 1.f));

    // Center: offset diluted by 1/8, position unchanged otherwise.
    std::vector<float> center = {0.5f, 0.5f, 0.5f};
    REQUIRE(lat.deformPositionsTo(center, out));
    CHECK(approx(out[0], 0.5f + 0.4f / 8.f));
    CHECK(approx(out[1], 0.5f - 0.2f / 8.f));
    CHECK(approx(out[2], 0.5f));
}

TEST_CASE("animation.lattice.clampVsExtrapolate") {
    AnimLattice lat(2, 2, 2);
    lat.setSize(2.f, 2.f, 2.f);
    lat.setOrigin(0.f, 0.f, 0.f);
    lat.setPointScale(1, 0, 0, 2.f, 1.f, 1.f);

    // Vertex outside the box on +x: u = 1.5.
    std::vector<float> p = {2.f, 0.f, 0.f};
    std::vector<float> out;

    lat.setClamp(true);
    REQUIRE(lat.deformPositionsTo(p, out));
    CHECK(approx(out[0], 4.f));  // clamped to the corner cell scale

    lat.setClamp(false);
    REQUIRE(lat.deformPositionsTo(p, out));
    CHECK(approx(out[0], 5.f));  // linear extrapolation keeps growing
}

TEST_CASE("animation.lattice.normals") {
    AnimLattice lat(2, 2, 2);
    lat.setSize(2.f, 2.f, 2.f);
    lat.setOrigin(0.f, 0.f, 0.f);
    lat.setScale(2.f, 1.f, 1.f);

    const float pos[9] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
    const float nrm[9] = {1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f};
    float       out[9];
    REQUIRE(lat.deformNormals(pos, nrm, out, 3));

    CHECK(approx(out[0], 1.f));
    CHECK(approx(out[1], 0.f));
    CHECK(approx(out[2], 0.f));
    CHECK(approx(out[3], 0.f));
    CHECK(approx(out[4], 1.f));
    CHECK(approx(out[5], 0.f));
    // (2,1,0) normalized.
    const float inv = 1.f / std::sqrt(5.f);
    CHECK(approx(out[6], 2.f * inv));
    CHECK(approx(out[7], 1.f * inv));
    CHECK(approx(out[8], 0.f));
}

TEST_CASE("animation.lattice.bindModelAndFromModel") {
    std::unique_ptr<eve::model3d::ModelData> model(loadQuadModel());
    REQUIRE(model.get() != nullptr);
    REQUIRE(model->getMeshCount() >= 1);
    REQUIRE(model->getVertexCount(0) == 4);

    AnimLattice lat(2, 2, 2);
    lat.setSize(2.f, 2.f, 2.f);
    lat.setOrigin(0.f, 0.f, 0.f);
    lat.bindModel(model.get(), 0);
    REQUIRE(lat.getVertexCount() == 4);

    // Bind positions match the OBJ vertices.
    CHECK(approx(lat.getBindPositionX(0), -1.f));
    CHECK(approx(lat.getBindPositionY(1), -1.f));
    CHECK(approx(lat.getBindPositionZ(2), 0.f));

    // fromModel factory binds the same vertices with identity deformation.
    std::unique_ptr<AnimLattice> fromModel(AnimLattice::fromModel(model.get(), 0, 2, 2, 2));
    REQUIRE(fromModel.get() != nullptr);
    REQUIRE(fromModel->getVertexCount() == 4);
    REQUIRE(fromModel->updateDeformedPositions());
    CHECK(approx(fromModel->getDeformedPositionX(0), -1.f));
    CHECK(approx(fromModel->getDeformedPositionY(3), 1.f));
}

TEST_CASE("animation.lattice.validation") {
    AnimLattice lat(2, 2, 2);

    CHECK_THROWS((lat.setDivisions(1, 2, 2), false));
    CHECK_THROWS((lat.setDivisions(2, 0, 2), false));
    CHECK_THROWS((lat.setSize(0.f, 1.f, 1.f), false));
    CHECK_THROWS((lat.setPointScale(2, 0, 0, 1.f, 1.f, 1.f), false));
    CHECK_THROWS((lat.setPointOffset(0, -1, 0, 0.f, 0.f, 0.f), false));
    CHECK_THROWS((lat.getPointScaleX(0, 0, 2), false));
    CHECK_THROWS((lat.bindPositions(nullptr, 3), false));
    CHECK_THROWS((lat.getBindPositionX(0), false));
    CHECK_THROWS((lat.getDeformedPositionX(0), false));

    const float pos[3] = {0.f, 0.f, 0.f};
    lat.bindPositions(pos, 1);
    CHECK_THROWS((lat.getBindPositionY(1), false));
}

TEST_CASE("animation.lattice.cacheMatchesArray") {
    AnimLattice lat(3, 3, 3);
    lat.setSize(2.f, 2.f, 2.f);
    lat.setOrigin(0.f, 0.f, 0.f);

    std::vector<float> pos = {-0.8f, 0.2f, 0.1f, 0.7f, -0.6f, 0.9f, 0.f, 0.f, 0.f};
    std::vector<float> nrm = {0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f};
    lat.bindPositions(pos.data(), 3);
    lat.setPointScale(1, 1, 1, 1.5f, 1.f, 1.f);

    REQUIRE(lat.updateDeformedPositions(pos));
    REQUIRE(lat.updateDeformedNormals(pos, nrm));

    const std::vector<float> outPos = lat.getDeformedPositions();
    const std::vector<float> outNrm = lat.getDeformedNormals();
    REQUIRE(outPos.size() == 9u);
    REQUIRE(outNrm.size() == 9u);
    for (int v = 0; v < 3; ++v) {
        CHECK(approx(lat.getDeformedPositionX(v), outPos[v * 3 + 0]));
        CHECK(approx(lat.getDeformedPositionY(v), outPos[v * 3 + 1]));
        CHECK(approx(lat.getDeformedPositionZ(v), outPos[v * 3 + 2]));
    }
    CHECK(approx(outNrm[0], 0.f));
    CHECK(approx(outNrm[1], 0.f));
    CHECK(approx(outNrm[2], 1.f));

    lat.clearBind();
    CHECK(lat.getVertexCount() == 0);
    CHECK(lat.getDeformedPositions().empty());
}

// animation_orientation_warping.cpp
using namespace eve::animation;

namespace {
constexpr float kPi = 3.14159265359f;

float yawOf(const AnimPose& pose, int bone) {
    const auto& t = pose.world(bone);
    return std::atan2(2.f * (t.qw * t.qy + t.qx * t.qz), 1.f - 2.f * (t.qx * t.qx + t.qy * t.qy));
}

void makeWarpSkeleton(AnimSkeleton& skeleton) {
    skeleton.addBone("root");
    skeleton.addBone("spine", 0);
    skeleton.addBone("head", 1);
    skeleton.addBone("foot", 0);
}

AnimPose identityPose(int bones) { return AnimPose(bones); }
}  // namespace

TEST_CASE("animation.orientationWarping.rotatesRootTowardLocomotionAndLeavesMatcherUntouched") {
    AnimSkeleton skeleton;
    makeWarpSkeleton(skeleton);
    OrientationWarping warp;
    REQUIRE(warp.configure(skeleton, 0).ok());
    REQUIRE(warp.setRotationInterpSpeed(0.f).ok());
    AnimPose matcherPose = identityPose(4);
    matcherPose.setLocalPosition(0, 0.f, 0.f, 1.f);
    AnimPose display = identityPose(4);
    display.copyFrom(&matcherPose);
    REQUIRE(warp.apply(display, 1.f, 0.f, 0.f, 2.f, 1.f / 60.f).ok());
    display.computeWorld(&skeleton);
    CHECK(std::fabs(yawOf(display, 0) - kPi * 0.5f) < 0.02f);
    CHECK(std::fabs(warp.getAppliedAngle() - kPi * 0.5f) < 0.02f);
    CHECK_EQ(matcherPose.getLocalPositionZ(0), 1.f);
    CHECK_EQ(matcherPose.getLocalRotationY(0), 0.f);
    CHECK_EQ(matcherPose.getLocalRotationW(0), 1.f);
}

TEST_CASE("animation.orientationWarping.distributesSpineShareAndRestoresIkWorldYaw") {
    AnimSkeleton skeleton;
    makeWarpSkeleton(skeleton);
    OrientationWarping       warp;
    const std::array<int, 2> spine{1, 2};
    const std::array<int, 1> ik{3};
    REQUIRE(warp.configure(skeleton, 0, spine, ik).ok());
    REQUIRE(warp.setDistributedAlpha(0.5f).ok());
    REQUIRE(warp.setRotationInterpSpeed(0.f).ok());
    AnimPose pose = identityPose(4);
    REQUIRE(warp.apply(pose, 1.f, 0.f, 0.f, 1.f, 0.016f).ok());
    pose.computeWorld(&skeleton);
    CHECK(std::fabs(yawOf(pose, 0) - kPi * 0.25f) < 0.02f);
    CHECK(std::fabs(yawOf(pose, 2) - kPi * 0.5f) < 0.03f);
    CHECK(std::fabs(yawOf(pose, 3)) < 0.02f);
}

TEST_CASE("animation.orientationWarping.thresholdAndLowSpeedTargetZero") {
    AnimSkeleton skeleton;
    makeWarpSkeleton(skeleton);
    OrientationWarping warp;
    REQUIRE(warp.configure(skeleton, 0).ok());
    REQUIRE(warp.setRotationInterpSpeed(0.f).ok());
    REQUIRE(warp.setAngleThreshold(kPi * 0.5f).ok());
    AnimPose pose = identityPose(4);
    REQUIRE(warp.apply(pose, -1.f, 0.f, 0.f, 1.f, 0.016f).ok());
    CHECK(std::fabs(warp.getTargetAngle()) < 1e-5f);
    CHECK(std::fabs(warp.getAppliedAngle()) < 1e-5f);
    REQUIRE(warp.setAngleThreshold(kPi).ok());
    REQUIRE(warp.setMinRootMotionSpeed(1.f).ok());
    REQUIRE(warp.apply(pose, 0.2f, 0.f, 0.f, 0.2f, 0.016f).ok());
    CHECK(std::fabs(warp.getTargetAngle()) < 1e-5f);
}

TEST_CASE("animation.orientationWarping.invalidInputsAndDisabledApplyLeavePoseUnchanged") {
    AnimSkeleton skeleton;
    makeWarpSkeleton(skeleton);
    OrientationWarping warp;
    AnimPose           pose = identityPose(4);
    pose.setLocalPosition(0, 3.f, 0.f, 0.f);
    CHECK(!warp.apply(pose, 1.f, 0.f, 0.f, 1.f, 0.016f).ok());
    CHECK_EQ(pose.getLocalPositionX(0), 3.f);
    const std::array<int, 1> bad{9};
    CHECK(!warp.configure(skeleton, 0, bad).ok());
    REQUIRE(warp.configure(skeleton, 0).ok());
    REQUIRE(warp.setRotationInterpSpeed(0.f).ok());
    REQUIRE(warp.apply(pose, 1.f, 0.f, 0.f, 1.f, 0.016f).ok());
    const float warped = pose.getLocalRotationY(0);
    CHECK(std::fabs(warped) > 0.1f);
    warp.setEnabled(false);
    AnimPose disabled = identityPose(4);
    disabled.setLocalPosition(0, 3.f, 0.f, 0.f);
    auto skipped = warp.apply(disabled, 1.f, 0.f, 0.f, 1.f, 0.016f);
    REQUIRE(skipped.ok());
    CHECK_EQ(disabled.getLocalPositionX(0), 3.f);
    CHECK_EQ(disabled.getLocalRotationY(0), 0.f);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(!warp.setDistributedAlpha(nan).ok());
    CHECK(!warp.setAngleThreshold(0.f).ok());
    CHECK(!warp.apply(pose, nan, 0.f, 0.f, 1.f, 0.016f).ok());
    CHECK_EQ(pose.getLocalRotationY(0), warped);
}

TEST_CASE("animation.orientationWarping.interpolationUsesInjectedTime") {
    AnimSkeleton skeleton;
    makeWarpSkeleton(skeleton);
    OrientationWarping warp;
    REQUIRE(warp.configure(skeleton, 0).ok());
    REQUIRE(warp.setRotationInterpSpeed(8.f).ok());
    AnimPose pose = identityPose(4);
    REQUIRE(warp.apply(pose, 1.f, 0.f, 0.f, 1.f, 0.05f).ok());
    const float first = warp.getAppliedAngle();
    CHECK(first > 0.f);
    CHECK(first < kPi * 0.5f);
    REQUIRE(warp.apply(pose, 1.f, 0.f, 0.f, 1.f, 0.05f).ok());
    CHECK(warp.getAppliedAngle() > first);
}

// animation_physical_balance.cpp
using namespace eve::animation;

namespace {

std::unique_ptr<AnimSkeleton> makeBalanceSkeleton() {
    auto sk   = std::make_unique<AnimSkeleton>();
    int  root = sk->addBone("root", -1);
    sk->setBindPosition(root, 0.f, 0.f, 0.f);
    int spine = sk->addBone("spine", root);
    sk->setBindPosition(spine, 0.f, 1.f, 0.f);
    int chest = sk->addBone("chest", spine);
    sk->setBindPosition(chest, 0.f, 0.8f, 0.f);
    return sk;
}

}  // namespace

TEST_CASE("animation.physicalBalance.matchesTargetWithoutImpulse") {
    auto                sk = makeBalanceSkeleton();
    PhysicalBalancePose balance(sk.get());
    REQUIRE(balance.setBalanceBone(1).ok());
    REQUIRE(balance.setSupportBone(0).ok());
    AnimPose target(sk->getBoneCount());
    sk->applyBindPose(&target);
    target.setLocalRotation(1, 0.f, 0.f, 0.f, 1.f);
    REQUIRE(balance.setTargetPose(&target).ok());
    balance.update(1.f / 60.f);
    CHECK(std::fabs(balance.getLeanX()) < 1e-5f);
    CHECK(std::fabs(balance.getLeanZ()) < 1e-5f);
    CHECK(std::fabs(balance.getPose()->getLocalRotationW(1) - 1.f) < 1e-4f);
}

TEST_CASE("animation.physicalBalance.impulseLeansThenRecovers") {
    auto                sk = makeBalanceSkeleton();
    PhysicalBalancePose balance(sk.get());
    REQUIRE(balance.setBalanceBone(1).ok());
    REQUIRE(balance.setSupportBone(0).ok());
    REQUIRE(balance.setRecovery(2.f, 0.45f).ok());
    REQUIRE(balance.setInertia(1.f).ok());
    AnimPose target(sk->getBoneCount());
    sk->applyBindPose(&target);
    REQUIRE(balance.setTargetPose(&target).ok());
    REQUIRE(balance.applyImpulse(1, 8.f, 0.f, 0.f, 0.f, 1.2f, 0.f).ok());
    balance.update(1.f / 60.f);
    const float firstLean = balance.getLeanZ();
    CHECK(std::fabs(firstLean) > 1e-4f);
    // +X impulse at a point above the support moves COM toward +X.
    CHECK(balance.getCenterOfMassX() > 0.f);

    float maxAbs  = std::fabs(firstLean);
    bool  crossed = false;
    float prevVel = balance.getLeanVelocityZ();
    for (int i = 0; i < 180; ++i) {
        balance.update(1.f / 60.f);
        maxAbs = std::max(maxAbs, std::fabs(balance.getLeanZ()));
        if (prevVel * balance.getLeanVelocityZ() < 0.f) crossed = true;
        prevVel = balance.getLeanVelocityZ();
    }
    CHECK(std::fabs(balance.getLeanZ()) < 0.05f);
    CHECK(crossed);
    CHECK(maxAbs >= std::fabs(firstLean));
}

TEST_CASE("animation.physicalBalance.recoilDeflectsHitBone") {
    auto                sk = makeBalanceSkeleton();
    PhysicalBalancePose balance(sk.get());
    REQUIRE(balance.setBalanceBone(1).ok());
    REQUIRE(balance.setRecoil(3.f, 0.5f).ok());
    AnimPose target(sk->getBoneCount());
    sk->applyBindPose(&target);
    REQUIRE(balance.setTargetPose(&target).ok());
    REQUIRE(balance.applyImpulse(2, 0.f, 0.f, 4.f, 0.f, 1.8f, 0.1f).ok());
    balance.update(1.f / 60.f);
    const float hitW = balance.getPose()->getLocalRotationW(2);
    CHECK(hitW < 0.999f);
    for (int i = 0; i < 180; ++i) balance.update(1.f / 60.f);
    CHECK(std::fabs(balance.getPose()->getLocalRotationW(2) - 1.f) < 0.02f);
}

TEST_CASE("animation.physicalBalance.rejectsUnrecoverableGravity") {
    auto                sk = makeBalanceSkeleton();
    PhysicalBalancePose balance(sk.get());
    CHECK(!balance.setRecovery(0.2f, 0.5f).ok());
    CHECK(!balance.setGravity(500.f).ok());
    CHECK(std::fabs(balance.getRecoveryFrequency() - 2.f) < 1e-6f);
    CHECK(std::fabs(balance.getGravity() - 9.81f) < 1e-5f);
}

TEST_CASE("animation.physicalBalance.rejectsBadImpulse") {
    auto                sk = makeBalanceSkeleton();
    PhysicalBalancePose balance(sk.get());
    CHECK(!balance.applyImpulse(-1, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f).ok());
    CHECK(!balance.applyImpulse(0, std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f, 0.f, 0.f, 0.f).ok());
    auto zero = balance.applyImpulse(0, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
    REQUIRE(zero.ok());
    CHECK(zero.status().code() == eve::StatusCode::NoOp);
}

TEST_CASE("animation.physicalBalance.followsAnimPlayerTarget") {
    auto sk   = makeBalanceSkeleton();
    auto clip = std::make_unique<AnimClip>("idle");
    clip->setDuration(1.f);
    clip->addPositionKey(1, 0.f, 0.f, 1.f, 0.f);
    clip->addPositionKey(1, 1.f, 0.f, 1.f, 0.f);
    AnimPlayer player(sk.get());
    player.play(clip.get());
    player.update(0.1f);

    PhysicalBalancePose balance(sk.get());
    REQUIRE(balance.setBalanceBone(1).ok());
    REQUIRE(balance.setTargetPose(player.getPose()).ok());
    REQUIRE(balance.applyImpulse(1, 5.f, 0.f, 0.f, 0.f, 1.f, 0.f).ok());
    balance.update(1.f / 60.f);
    CHECK(std::fabs(balance.getLeanZ()) > 1e-4f);
    CHECK(std::fabs(balance.getPose()->getLocalPositionY(1) - 1.f) < 0.05f);
}

// animation_procedural_bones.cpp
using namespace eve::animation;

namespace {
class TestForceField final : public DynamicBoneForceField {
public:
    void sampleForce(float x, float, float, float, float& outX, float& outY, float& outZ) const override {
        outX = x >= 0.f ? 8.f : -8.f;
        outY = 0.f;
        outZ = 0.f;
    }
};

class TestGroundProvider final : public FootIKGroundProvider {
public:
    mutable bool            hit = true;
    FootIKGroundQueryStatus queryGround(float x, float, float z, float, float& hitX, float& hitY, float& hitZ,
                                        float& nx, float& ny, float& nz) const override {
        if (!hit) return FootIKGroundQueryStatus::NoHit;
        hitX = x;
        hitY = -2.25f;
        hitZ = z;
        nx   = 0.f;
        ny   = 1.f;
        nz   = 0.f;
        return FootIKGroundQueryStatus::Hit;
    }
};

float distance(const AnimPose& pose, int a, int b) {
    const float x = pose.getWorldPositionX(a) - pose.getWorldPositionX(b);
    const float y = pose.getWorldPositionY(a) - pose.getWorldPositionY(b);
    const float z = pose.getWorldPositionZ(a) - pose.getWorldPositionZ(b);
    return std::sqrt(x * x + y * y + z * z);
}
}  // namespace

TEST_CASE("animation.procedural.dynamicBoneRotatesAndPreservesBoneLengths") {
    AnimSkeleton skeleton;
    const int    root = skeleton.addBone("hair_root");
    const int    mid  = skeleton.addBone("hair_mid", root);
    const int    tip  = skeleton.addBone("hair_tip", mid);
    skeleton.setBindPosition(mid, 0.f, -1.f, 0.f);
    skeleton.setBindPosition(tip, 0.f, -1.f, 0.f);
    AnimPose pose;
    skeleton.applyBindPose(&pose);
    pose.computeWorld(&skeleton);

    DynamicBoneSolver solver(&skeleton);
    solver.setGlobalGravity(10.f, 0.f, 0.f);
    CHECK(solver.addChain(root, tip, 0.f, 0.f, 1.f, 1.f, 0.f, 6) == 0);
    solver.update(&pose, 0.1f);

    CHECK(pose.getWorldPositionX(tip) > 0.01f);
    CHECK(std::fabs(distance(pose, root, mid) - 1.f) < 1e-3f);
    CHECK(std::fabs(distance(pose, mid, tip) - 1.f) < 1e-3f);
    CHECK(std::fabs(pose.getLocalPositionX(mid)) < 1e-6f);
    CHECK(std::fabs(pose.getLocalPositionY(mid) + 1.f) < 1e-6f);
}

TEST_CASE("animation.procedural.dynamicBoneSameSkeletonRebindPreservesConfiguration") {
    AnimSkeleton skeleton;
    const int    root = skeleton.addBone("root");
    const int    tip  = skeleton.addBone("tip", root);
    skeleton.setBindPosition(tip, 0.f, 1.f, 0.f);
    DynamicBoneSolver solver(&skeleton);
    CHECK(solver.addChain(root, tip, 0.2f, 0.3f, 0.8f) == 0);
    solver.setSkeleton(&skeleton);
    CHECK(solver.getChainCount() == 1);
}

TEST_CASE("animation.procedural.dynamicBoneFreezeAxisAndCapsuleCollision") {
    AnimSkeleton skeleton;
    const int    root = skeleton.addBone("root");
    const int    tip  = skeleton.addBone("tip", root);
    skeleton.setBindPosition(tip, 0.f, -1.f, 0.f);
    AnimPose pose;
    skeleton.applyBindPose(&pose);

    DynamicBoneSolver solver(&skeleton);
    solver.setGlobalGravity(10.f, 0.f, 0.f);
    const int chain = solver.addChain(root, tip, 0.f, 0.f, 1.f, 1.f, 0.1f, 8);
    REQUIRE(chain == 0);
    solver.setChainFreezeAxis(chain, 3);
    solver.addColliderCapsule(0.15f, -1.5f, -0.5f, 0.15f, -0.5f, 0.5f, 0.25f);
    CHECK(solver.getColliderCount() == 1);
    solver.update(&pose, 0.1f);

    CHECK(pose.getWorldPositionX(tip) < -0.01f);
    CHECK(std::fabs(pose.getWorldPositionZ(tip)) < 1e-4f);
    CHECK(std::fabs(distance(pose, root, tip) - 1.f) < 1e-3f);
}

TEST_CASE("animation.procedural.dynamicBoneColliderModesAreRuntimeConfigurable") {
    AnimSkeleton skeleton;
    const int    root = skeleton.addBone("root");
    const int    tip  = skeleton.addBone("tip", root);
    skeleton.setBindPosition(tip, 0.f, -1.f, 0.f);
    AnimPose pose;
    skeleton.applyBindPose(&pose);

    DynamicBoneSolver solver(&skeleton);
    solver.setGlobalGravity(10.f, 0.f, 0.f);
    REQUIRE(solver.addChain(root, tip, 0.f, 0.f, 1.f, 1.f, 0.1f, 8) == 0);
    solver.addColliderSphere(0.f, -1.f, 0.f, 0.5f);
    solver.setColliderInside(0, true);
    solver.setColliderRadius(0, 0.25f);
    solver.update(&pose, 0.1f);
    CHECK(pose.getWorldPositionX(tip) <= 0.151f);

    skeleton.applyBindPose(&pose);
    solver.reset();
    solver.setColliderEnabled(0, false);
    solver.update(&pose, 0.1f);
    CHECK(pose.getWorldPositionX(tip) > 0.151f);
}

TEST_CASE("animation.procedural.dynamicBoneSupportsParticleProfilesAndVirtualEnds") {
    AnimSkeleton skeleton;
    const int    root = skeleton.addBone("root");
    const int    tip  = skeleton.addBone("tip", root);
    skeleton.setBindPosition(tip, 0.f, -1.f, 0.f);
    AnimPose pose;
    skeleton.applyBindPose(&pose);

    DynamicBoneSolver solver(&skeleton);
    solver.setGlobalGravity(10.f, 0.f, 0.f);
    const int chain = solver.addChain(root, tip, 0.f, 0.f, 1.f, 1.f, 0.f, 8);
    REQUIRE(chain == 0);
    solver.setChainEndLength(chain, 0.5f);
    solver.setChainParticleParameters(chain, 1, 1.f, 1.f, 0.f, 0.f, 0.f);
    solver.setChainParticleParameters(chain, 2, 0.f, 0.f, 1.f, 1.f, 0.f);
    solver.update(&pose, 0.1f);

    CHECK(std::fabs(pose.getWorldPositionX(tip)) < 1e-4f);
    const float localRotationMagnitude = std::fabs(pose.getLocalRotationX(tip)) +
                                         std::fabs(pose.getLocalRotationY(tip)) +
                                         std::fabs(pose.getLocalRotationZ(tip));
    CHECK(localRotationMagnitude > 1e-4f);
}

TEST_CASE("animation.procedural.dynamicBoneHandlesObjectMotionAndDistanceSleep") {
    AnimSkeleton skeleton;
    const int    root = skeleton.addBone("root");
    const int    tip  = skeleton.addBone("tip", root);
    skeleton.setBindPosition(tip, 0.f, -1.f, 0.f);
    AnimPose pose;
    skeleton.applyBindPose(&pose);

    DynamicBoneSolver solver(&skeleton);
    solver.setGlobalGravity(0.f, 0.f, 0.f);
    const int chain = solver.addChain(root, tip, 0.f, 1.f, 0.f, 0.f, 0.f, 4);
    REQUIRE(chain == 0);
    solver.update(&pose, 1.f / 60.f);
    solver.setObjectMoveResponse(0.f);
    pose.setLocalPosition(root, 1.f, 0.f, 0.f);
    solver.update(&pose, 1.f / 60.f);
    pose.computeWorld(&skeleton);
    CHECK(pose.getWorldPositionX(tip) < 0.9f);

    solver.setDistanceReference(100.f, 0.f, 0.f);
    solver.setDistanceLimit(1.f);
    solver.update(&pose, 1.f / 60.f);
    CHECK(solver.isChainSleeping(chain));
    solver.setDistanceReference(1.f, 0.f, 0.f);
    solver.update(&pose, 1.f / 60.f);
    CHECK(!solver.isChainSleeping(chain));
}

TEST_CASE("animation.procedural.dynamicBoneSamplesSpatialForceField") {
    AnimSkeleton skeleton;
    const int    root = skeleton.addBone("root");
    const int    tip  = skeleton.addBone("tip", root);
    skeleton.setBindPosition(tip, 0.f, -1.f, 0.f);
    AnimPose pose;
    skeleton.applyBindPose(&pose);

    TestForceField    field;
    DynamicBoneSolver solver(&skeleton);
    solver.setGlobalGravity(0.f, 0.f, 0.f);
    solver.setForceField(&field);
    REQUIRE(solver.getForceField() == &field);
    REQUIRE(solver.addChain(root, tip, 0.f, 0.f, 1.f, 1.f, 0.f, 6) == 0);
    solver.update(&pose, 0.1f);
    CHECK(pose.getWorldPositionX(tip) > 0.01f);
    solver.setForceField(nullptr);
    CHECK(solver.getForceField() == nullptr);
}

TEST_CASE("animation.procedural.footIkCompensatesPelvisAndRejectsSteepGround") {
    AnimSkeleton skeleton;
    const int    pelvis = skeleton.addBone("pelvis");
    const int    hip    = skeleton.addBone("hip", pelvis);
    const int    knee   = skeleton.addBone("knee", hip);
    const int    foot   = skeleton.addBone("foot", knee);
    skeleton.setBindPosition(knee, 0.f, -1.f, 0.f);
    skeleton.setBindPosition(foot, 0.f, -1.f, 0.f);

    AnimPose pose;
    skeleton.applyBindPose(&pose);
    FootIKSolver solver(&skeleton);
    solver.setPelvisBone(pelvis);
    solver.configureLeftLeg(hip, knee, foot);
    solver.setMaxPelvisOffset(0.25f);
    solver.setLeftContact(true, 0.f, -2.5f, 0.f, 0.f, 1.f, 0.f, 1.f);
    solver.apply(&pose, 1.f / 60.f);
    CHECK(std::fabs(pose.getLocalPositionY(pelvis) + 0.25f) < 1e-4f);

    skeleton.applyBindPose(&pose);
    solver.reset();
    solver.setLeftContact(true, 0.f, -2.5f, 0.f, 1.f, 0.f, 0.f, 1.f);
    solver.apply(&pose, 1.f / 60.f);
    CHECK(std::fabs(pose.getLocalPositionY(pelvis)) < 1e-6f);
}

TEST_CASE("animation.procedural.footIkQueriesGroundLocksAndUsesGracePeriod") {
    AnimSkeleton skeleton;
    const int    pelvis = skeleton.addBone("pelvis");
    const int    hip    = skeleton.addBone("hip", pelvis);
    const int    knee   = skeleton.addBone("knee", hip);
    const int    foot   = skeleton.addBone("foot", knee);
    const int    toe    = skeleton.addBone("toe", foot);
    skeleton.setBindPosition(knee, 0.f, -1.f, 0.f);
    skeleton.setBindPosition(foot, 0.f, -1.f, 0.f);
    skeleton.setBindPosition(toe, 0.f, 0.f, 0.25f);
    AnimPose pose;
    skeleton.applyBindPose(&pose);

    TestGroundProvider ground;
    FootIKSolver       solver(&skeleton);
    solver.setPelvisBone(pelvis);
    solver.configureLeftLeg(hip, knee, foot, 0.05f);
    solver.configureLeftToe(toe, 0.02f);
    solver.setGroundProvider(&ground);
    solver.setFootLockEnabled(true);
    solver.setFootLockThresholds(0.8f, 0.2f);
    solver.setContactGraceTime(0.1f);
    solver.apply(&pose, 1.f / 60.f);
    CHECK(solver.isLeftFootLocked());
    CHECK(pose.getLocalPositionY(pelvis) < 0.f);

    ground.hit = false;
    solver.apply(&pose, 0.05f);
    CHECK(solver.isLeftFootLocked());
    solver.apply(&pose, 0.1f);
    CHECK(!solver.isLeftFootLocked());
    solver.setGroundProvider(nullptr);
    CHECK(solver.getGroundProvider() == nullptr);
}

TEST_CASE("animation.procedural.dynamicBoneConfigRoundTripsUnknownFieldsAndAppliesAtomically") {
    DynamicBoneConfig      config;
    DynamicBoneChainConfig chain;
    chain.rootBone  = "root";
    chain.endBone   = "tip";
    chain.endMode   = 1;
    chain.endLength = 0.25f;
    chain.particles.resize(3);
    config.chains.push_back(chain);
    config.unknownFields["futureEditorData"] = "preserved";
    config.footIK.enabled                    = true;
    config.footIK.pelvisBone                 = "root";
    config.footIK.left                       = {"hip", "knee", "foot", "toe", 0.05f, 0.02f};
    config.footIK.footLockEnabled            = true;
    auto encoded                             = config.toValue();
    REQUIRE(encoded.ok());
    auto decoded = DynamicBoneConfig::fromValue(encoded.value());
    REQUIRE(decoded.ok());
    CHECK(decoded.value().unknownFields.contains("futureEditorData"));
    AnimSkeleton skeleton;
    const int    root = skeleton.addBone("root");
    const int    tip  = skeleton.addBone("tip", root);
    const int    hip  = skeleton.addBone("hip", root);
    const int    knee = skeleton.addBone("knee", hip);
    const int    foot = skeleton.addBone("foot", knee);
    const int    toe  = skeleton.addBone("toe", foot);
    REQUIRE(toe >= 0);
    skeleton.setBindPosition(tip, 0.f, -1.f, 0.f);
    DynamicBoneSolver solver(&skeleton);
    FootIKSolver      footSolver(&skeleton);
    REQUIRE(decoded.value().apply(skeleton, solver, footSolver).ok());
    CHECK(solver.getChainCount() == 1);
    DynamicBoneConfig invalidConfig = decoded.value();
    invalidConfig.chains[0].endBone = "missing";
    CHECK(!invalidConfig.apply(skeleton, solver).ok());
    CHECK(solver.getChainCount() == 1);
    eve::Value future                                      = encoded.value();
    (*future.getIf<eve::Value::Object>())["schemaVersion"] = static_cast<std::int64_t>(2);
    CHECK(!DynamicBoneConfig::fromValue(future).ok());
}

TEST_CASE("animation.procedural.overlayBuildsRendererNeutralRuntimeDiagnostics") {
    AnimSkeleton skeleton;
    const int    root = skeleton.addBone("root");
    const int    tip  = skeleton.addBone("tip", root);
    skeleton.setBindPosition(tip, 0.f, -1.f, 0.f);
    AnimPose pose;
    skeleton.applyBindPose(&pose);
    DynamicBoneSolver dynamic(&skeleton);
    REQUIRE(dynamic.addChain(root, tip, 0.f, 0.f, 1.f) == 0);
    dynamic.addColliderSphere(0.f, -1.f, 0.f, 0.2f);
    dynamic.update(&pose, 1.f / 60.f);
    eve::animation_editing::ProceduralBoneOverlayBuilder builder;
    auto                                                 overlay = builder.build("avatar", 1, &dynamic, nullptr);
    CHECK(overlay.status == eve::editing::Status::Applied);
    CHECK(overlay.primitives.size() >= 3);
    CHECK(overlay.primitives[0].kind == "dynamic-bone-particle");
}

TEST_CASE("animation.procedural.dynamicBoneRejectsInvalidFramesAndReportsWork") {
    AnimSkeleton skeleton;
    const int    root = skeleton.addBone("root");
    const int    mid  = skeleton.addBone("mid", root);
    const int    tip  = skeleton.addBone("tip", mid);
    skeleton.setBindPosition(mid, 0.f, -1.f, 0.f);
    skeleton.setBindPosition(tip, 0.f, -1.f, 0.f);
    AnimPose pose;
    skeleton.applyBindPose(&pose);
    DynamicBoneSolver solver(&skeleton);
    const int         chain = solver.addChain(root, tip, 0.f, 0.f, 1.f, 1.f, 0.2f, 4);
    REQUIRE(chain == 0);
    solver.setChainEndLength(chain, 0.5f);
    solver.setChainSelfCollision(chain, true);
    solver.update(&pose, std::numeric_limits<float>::quiet_NaN());
    CHECK(solver.getLastUpdateStats().activeChains == 0);
    solver.update(&pose, 1.f / 60.f);
    const auto stats = solver.getLastUpdateStats();
    CHECK(stats.activeChains == 1);
    CHECK(stats.particles == 4);
    CHECK(stats.substeps == 1);
    CHECK(stats.selfCollisionTests > 0);
    AnimSkeleton replacement;
    replacement.addBone("other");
    solver.setSkeleton(&replacement);
    CHECK(solver.getChainCount() == 0);
}

TEST_CASE("animation.procedural.footIkRejectsNonFiniteDeltaTime") {
    AnimSkeleton skeleton;
    skeleton.addBone("root");
    AnimPose pose;
    skeleton.applyBindPose(&pose);
    FootIKSolver solver(&skeleton);
    bool         threw = false;
    try {
        solver.apply(&pose, std::numeric_limits<float>::infinity());
    } catch (const eve::Exception&) {
        threw = true;
    }
    CHECK(threw);
}

// animation_reload.cpp
using namespace eve::animation;

namespace {

const char* kRefreshFixturePath = "eve_anim_reload_refresh.anim.txt";
const char* kBrokenFixturePath  = "eve_anim_reload_broken.anim.txt";

std::unique_ptr<AnimSkeleton> makeSkeleton() {
    auto sk   = std::make_unique<AnimSkeleton>();
    int  root = sk->addBone("root", -1);
    sk->setBindPosition(root, 0.f, 0.f, 0.f);
    int hip = sk->addBone("hip", root);
    sk->setBindPosition(hip, 0.f, 1.f, 0.f);
    return sk;
}

std::string makeAnimationFixture(float duration, int keyCount) {
    auto     sk = makeSkeleton();
    AnimClip clip("walk");
    clip.setDuration(duration);
    clip.setLoop(true);
    clip.setSampleRate(20.f);
    for (int i = 0; i < keyCount; ++i) {
        clip.addPositionKey(0, static_cast<float>(i) * 0.5f, 0.f, 0.f, static_cast<float>(i));
    }
    return AnimImporter::exportAnimationFixtureText(sk.get(), &clip);
}

void writeAnimationFixture(const char* path, const std::string& text) {
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    ofs << text;
}

}  // namespace

TEST_CASE("animClip.adoptSwapsPayloadInPlace") {
    std::unique_ptr<AnimClip> a(new AnimClip("a"));
    a->setDuration(1.f);

    std::unique_ptr<AnimClip> b(new AnimClip("b"));
    b->setDuration(2.f);
    b->setLoop(false);
    b->addPositionKey(0, 0.25f, 1.f, 2.f, 3.f);

    a->adopt(*b);
    CHECK(a->getName() == "b");
    CHECK(a->getDuration() == 2.f);
    CHECK(!a->getLoop());
    CHECK_EQ(a->getPositionKeyCount(0), 1);
}

TEST_CASE("animClip.registryTracksPathAndUnregistersOnDestroy") {
    AnimClipRegistry::clear();
    {
        std::unique_ptr<AnimClip> c(new AnimClip("c"));
        AnimClipRegistry::registerPath("anim\\walk.anim.txt", c.get());
        CHECK(AnimClipRegistry::hasPath("anim/walk.anim.txt"));
        CHECK_EQ(AnimClipRegistry::count(), 1);

        const std::vector<AnimClip*> found = AnimClipRegistry::findByPath("anim/walk.anim.txt");
        REQUIRE(found.size() == 1u);
        CHECK(found[0] == c.get());
    }
    CHECK_EQ(AnimClipRegistry::count(), 0);
    CHECK(!AnimClipRegistry::hasPath("anim/walk.anim.txt"));
    AnimClipRegistry::clear();
}

TEST_CASE("animClip.reloadPathRefreshesRegisteredClips") {
    writeAnimationFixture(kRefreshFixturePath, makeAnimationFixture(1.f, 2));

    Animation*                anim = Animation::create();
    std::unique_ptr<AnimClip> loaded(anim->newClipFromAnimationFixtureText(kRefreshFixturePath));
    REQUIRE(loaded.get() != nullptr);
    AnimClip* identity = loaded.get();
    CHECK(loaded->getDuration() == 1.f);
    CHECK_EQ(AnimClipRegistry::findByPath(kRefreshFixturePath).size(), 1u);

    // Rewrite the source with different content and hot-reload.
    writeAnimationFixture(kRefreshFixturePath, makeAnimationFixture(2.f, 4));
    auto refreshed = AnimClipRegistry::reloadPath(kRefreshFixturePath);
    REQUIRE(refreshed.ok());
    CHECK_EQ(std::move(refreshed).takeValue(), 1);

    CHECK(loaded.get() == identity);  // instance identity is stable
    CHECK(loaded->getDuration() == 2.f);
    CHECK_EQ(loaded->getPositionKeyCount(0), 4);

    AnimClipRegistry::clear();
    std::remove(kRefreshFixturePath);
}

TEST_CASE("animClip.reloadUnknownOrBrokenPathIsNoop") {
    auto absent = AnimClipRegistry::reloadPath("no_such_file.anim.txt");
    REQUIRE(absent.ok());
    CHECK_EQ(std::move(absent).takeValue(), 0);

    std::unique_ptr<AnimClip> c(new AnimClip("c"));
    AnimClipRegistry::registerPath(kBrokenFixturePath, c.get());
    writeAnimationFixture(kBrokenFixturePath, "not an animation fixture");
    auto broken = AnimClipRegistry::reloadPath(kBrokenFixturePath);
    REQUIRE(!broken.ok());
    AnimClipRegistry::clear();
    std::remove(kBrokenFixturePath);
}
