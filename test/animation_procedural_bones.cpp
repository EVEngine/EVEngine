#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/DynamicBoneSolver.h"
#include "animation/FootIKSolver.h"
#include "animation/ProceduralBoneConfig.h"
#include "animation_editing/ProceduralBoneOverlay.h"
#include "common/Exception.h"

#include <cmath>
#include <limits>

using namespace eve::animation;

namespace {
class TestForceField final : public DynamicBoneForceField {
public:
    void sampleForce(float x,float,float,float,float &outX,float &outY,float &outZ) const override {
        outX=x>=0.f?8.f:-8.f; outY=0.f; outZ=0.f;
    }
};

class TestGroundProvider final : public FootIKGroundProvider {
public:
    mutable bool hit=true;
    FootIKGroundQueryStatus queryGround(float x,float,float z,float,float& hitX,float& hitY,float& hitZ,float& nx,float& ny,float& nz) const override {
        if(!hit)return FootIKGroundQueryStatus::NoHit;
        hitX=x;hitY=-2.25f;hitZ=z;nx=0.f;ny=1.f;nz=0.f;return FootIKGroundQueryStatus::Hit;
    }
};

float distance(const AnimPose& pose, int a, int b) {
    const float x = pose.getWorldPositionX(a) - pose.getWorldPositionX(b);
    const float y = pose.getWorldPositionY(a) - pose.getWorldPositionY(b);
    const float z = pose.getWorldPositionZ(a) - pose.getWorldPositionZ(b);
    return std::sqrt(x * x + y * y + z * z);
}
}

TEST_CASE("animation.procedural.dynamicBoneRotatesAndPreservesBoneLengths") {
    AnimSkeleton skeleton;
    const int root = skeleton.addBone("hair_root");
    const int mid = skeleton.addBone("hair_mid", root);
    const int tip = skeleton.addBone("hair_tip", mid);
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
    const int root = skeleton.addBone("root");
    const int tip = skeleton.addBone("tip", root);
    skeleton.setBindPosition(tip, 0.f, 1.f, 0.f);
    DynamicBoneSolver solver(&skeleton);
    CHECK(solver.addChain(root, tip, 0.2f, 0.3f, 0.8f) == 0);
    solver.setSkeleton(&skeleton);
    CHECK(solver.getChainCount() == 1);
}

TEST_CASE("animation.procedural.dynamicBoneFreezeAxisAndCapsuleCollision") {
    AnimSkeleton skeleton;
    const int root = skeleton.addBone("root");
    const int tip = skeleton.addBone("tip", root);
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
    const int root = skeleton.addBone("root");
    const int tip = skeleton.addBone("tip", root);
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
    const int root = skeleton.addBone("root");
    const int tip = skeleton.addBone("tip", root);
    skeleton.setBindPosition(tip, 0.f, -1.f, 0.f);
    AnimPose pose;
    skeleton.applyBindPose(&pose);

    DynamicBoneSolver solver(&skeleton);
    solver.setGlobalGravity(10.f, 0.f, 0.f);
    const int chain=solver.addChain(root,tip,0.f,0.f,1.f,1.f,0.f,8);
    REQUIRE(chain == 0);
    solver.setChainEndLength(chain, 0.5f);
    solver.setChainParticleParameters(chain, 1, 1.f, 1.f, 0.f, 0.f, 0.f);
    solver.setChainParticleParameters(chain, 2, 0.f, 0.f, 1.f, 1.f, 0.f);
    solver.update(&pose, 0.1f);

    CHECK(std::fabs(pose.getWorldPositionX(tip)) < 1e-4f);
    const float localRotationMagnitude=std::fabs(pose.getLocalRotationX(tip))+std::fabs(pose.getLocalRotationY(tip))+std::fabs(pose.getLocalRotationZ(tip));
    CHECK(localRotationMagnitude > 1e-4f);
}

TEST_CASE("animation.procedural.dynamicBoneHandlesObjectMotionAndDistanceSleep") {
    AnimSkeleton skeleton;
    const int root=skeleton.addBone("root");
    const int tip=skeleton.addBone("tip",root);
    skeleton.setBindPosition(tip,0.f,-1.f,0.f);
    AnimPose pose;
    skeleton.applyBindPose(&pose);

    DynamicBoneSolver solver(&skeleton);
    solver.setGlobalGravity(0.f,0.f,0.f);
    const int chain=solver.addChain(root,tip,0.f,1.f,0.f,0.f,0.f,4);
    REQUIRE(chain==0);
    solver.update(&pose,1.f/60.f);
    solver.setObjectMoveResponse(0.f);
    pose.setLocalPosition(root,1.f,0.f,0.f);
    solver.update(&pose,1.f/60.f);
    pose.computeWorld(&skeleton);
    CHECK(pose.getWorldPositionX(tip)<0.9f);

    solver.setDistanceReference(100.f,0.f,0.f);
    solver.setDistanceLimit(1.f);
    solver.update(&pose,1.f/60.f);
    CHECK(solver.isChainSleeping(chain));
    solver.setDistanceReference(1.f,0.f,0.f);
    solver.update(&pose,1.f/60.f);
    CHECK(!solver.isChainSleeping(chain));
}

TEST_CASE("animation.procedural.dynamicBoneSamplesSpatialForceField") {
    AnimSkeleton skeleton;
    const int root=skeleton.addBone("root");
    const int tip=skeleton.addBone("tip",root);
    skeleton.setBindPosition(tip,0.f,-1.f,0.f);
    AnimPose pose;
    skeleton.applyBindPose(&pose);

    TestForceField field;
    DynamicBoneSolver solver(&skeleton);
    solver.setGlobalGravity(0.f,0.f,0.f);
    solver.setForceField(&field);
    REQUIRE(solver.getForceField()==&field);
    REQUIRE(solver.addChain(root,tip,0.f,0.f,1.f,1.f,0.f,6)==0);
    solver.update(&pose,0.1f);
    CHECK(pose.getWorldPositionX(tip)>0.01f);
    solver.setForceField(nullptr);
    CHECK(solver.getForceField()==nullptr);
}

TEST_CASE("animation.procedural.footIkCompensatesPelvisAndRejectsSteepGround") {
    AnimSkeleton skeleton;
    const int pelvis = skeleton.addBone("pelvis");
    const int hip = skeleton.addBone("hip", pelvis);
    const int knee = skeleton.addBone("knee", hip);
    const int foot = skeleton.addBone("foot", knee);
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
    const int pelvis=skeleton.addBone("pelvis");
    const int hip=skeleton.addBone("hip",pelvis);
    const int knee=skeleton.addBone("knee",hip);
    const int foot=skeleton.addBone("foot",knee);
    const int toe=skeleton.addBone("toe",foot);
    skeleton.setBindPosition(knee,0.f,-1.f,0.f);skeleton.setBindPosition(foot,0.f,-1.f,0.f);skeleton.setBindPosition(toe,0.f,0.f,0.25f);
    AnimPose pose;skeleton.applyBindPose(&pose);

    TestGroundProvider ground;
    FootIKSolver solver(&skeleton);
    solver.setPelvisBone(pelvis);solver.configureLeftLeg(hip,knee,foot,0.05f);solver.configureLeftToe(toe,0.02f);
    solver.setGroundProvider(&ground);solver.setFootLockEnabled(true);solver.setFootLockThresholds(0.8f,0.2f);solver.setContactGraceTime(0.1f);
    solver.apply(&pose,1.f/60.f);
    CHECK(solver.isLeftFootLocked());
    CHECK(pose.getLocalPositionY(pelvis)<0.f);

    ground.hit=false;
    solver.apply(&pose,0.05f);
    CHECK(solver.isLeftFootLocked());
    solver.apply(&pose,0.1f);
    CHECK(!solver.isLeftFootLocked());
    solver.setGroundProvider(nullptr);
    CHECK(solver.getGroundProvider()==nullptr);
}

TEST_CASE("animation.procedural.dynamicBoneConfigRoundTripsUnknownFieldsAndAppliesAtomically") {
    DynamicBoneConfig config;DynamicBoneChainConfig chain;chain.rootBone="root";chain.endBone="tip";chain.endMode=1;chain.endLength=0.25f;chain.particles.resize(3);config.chains.push_back(chain);config.unknownFields["futureEditorData"]="preserved";config.footIK.enabled=true;config.footIK.pelvisBone="root";config.footIK.left={"hip","knee","foot","toe",0.05f,0.02f};config.footIK.footLockEnabled=true;
    auto encoded=config.toValue();REQUIRE(encoded.ok());auto decoded=DynamicBoneConfig::fromValue(encoded.value());REQUIRE(decoded.ok());CHECK(decoded.value().unknownFields.contains("futureEditorData"));
    AnimSkeleton skeleton;const int root=skeleton.addBone("root");const int tip=skeleton.addBone("tip",root);const int hip=skeleton.addBone("hip",root);const int knee=skeleton.addBone("knee",hip);const int foot=skeleton.addBone("foot",knee);const int toe=skeleton.addBone("toe",foot);REQUIRE(toe>=0);skeleton.setBindPosition(tip,0.f,-1.f,0.f);DynamicBoneSolver solver(&skeleton);FootIKSolver footSolver(&skeleton);REQUIRE(decoded.value().apply(skeleton,solver,footSolver).ok());CHECK(solver.getChainCount()==1);
    DynamicBoneConfig invalidConfig=decoded.value();invalidConfig.chains[0].endBone="missing";CHECK(!invalidConfig.apply(skeleton,solver).ok());CHECK(solver.getChainCount()==1);
    eve::Value future=encoded.value();(*future.getIf<eve::Value::Object>())["schemaVersion"]=static_cast<std::int64_t>(2);CHECK(!DynamicBoneConfig::fromValue(future).ok());
}

TEST_CASE("animation.procedural.overlayBuildsRendererNeutralRuntimeDiagnostics") {
    AnimSkeleton skeleton;const int root=skeleton.addBone("root");const int tip=skeleton.addBone("tip",root);skeleton.setBindPosition(tip,0.f,-1.f,0.f);AnimPose pose;skeleton.applyBindPose(&pose);DynamicBoneSolver dynamic(&skeleton);REQUIRE(dynamic.addChain(root,tip,0.f,0.f,1.f)==0);dynamic.addColliderSphere(0.f,-1.f,0.f,0.2f);dynamic.update(&pose,1.f/60.f);
    eve::animation_editing::ProceduralBoneOverlayBuilder builder;auto overlay=builder.build("avatar",1,&dynamic,nullptr);CHECK(overlay.status==eve::editing::Status::Applied);CHECK(overlay.primitives.size()>=3);CHECK(overlay.primitives[0].kind=="dynamic-bone-particle");
}

TEST_CASE("animation.procedural.dynamicBoneRejectsInvalidFramesAndReportsWork") {
    AnimSkeleton skeleton;const int root=skeleton.addBone("root");const int mid=skeleton.addBone("mid",root);const int tip=skeleton.addBone("tip",mid);skeleton.setBindPosition(mid,0.f,-1.f,0.f);skeleton.setBindPosition(tip,0.f,-1.f,0.f);AnimPose pose;skeleton.applyBindPose(&pose);DynamicBoneSolver solver(&skeleton);const int chain=solver.addChain(root,tip,0.f,0.f,1.f,1.f,0.2f,4);REQUIRE(chain==0);solver.setChainEndLength(chain,0.5f);solver.setChainSelfCollision(chain,true);solver.update(&pose,std::numeric_limits<float>::quiet_NaN());CHECK(solver.getLastUpdateStats().activeChains==0);solver.update(&pose,1.f/60.f);const auto stats=solver.getLastUpdateStats();CHECK(stats.activeChains==1);CHECK(stats.particles==4);CHECK(stats.substeps==1);CHECK(stats.selfCollisionTests>0);
    AnimSkeleton replacement;replacement.addBone("other");solver.setSkeleton(&replacement);CHECK(solver.getChainCount()==0);
}

TEST_CASE("animation.procedural.footIkRejectsNonFiniteDeltaTime") {
    AnimSkeleton skeleton;skeleton.addBone("root");AnimPose pose;skeleton.applyBindPose(&pose);FootIKSolver solver(&skeleton);bool threw=false;try{solver.apply(&pose,std::numeric_limits<float>::infinity());}catch(const eve::Exception&){threw=true;}CHECK(threw);
}
