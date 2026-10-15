#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "animation/AnimSkeleton.h"
#include "animation/DynamicBoneSolver.h"
#include "animation/HairDynamicBones.h"

using eve::animation::AnimSkeleton;
using eve::animation::DynamicBoneSolver;

TEST_CASE("animation.HairDynamicBones.setupChainsAndHeadCollider") {
    AnimSkeleton skeleton;
    const int head = skeleton.addBone("head");
    const int root = skeleton.addBone("hair_root", head);
    const int mid = skeleton.addBone("hair_mid", root);
    const int tip = skeleton.addBone("hair_tip", mid);
    REQUIRE(tip >= 0);
    skeleton.setBindPosition(mid, 0.f, -0.2f, 0.f);
    skeleton.setBindPosition(tip, 0.f, -0.2f, 0.f);

    DynamicBoneSolver solver(&skeleton);
    eve::animation::hair::ChainDesc chain;
    chain.rootBone = "hair_root";
    chain.tipBone = "hair_tip";
    chain.stiffness = 0.12f;
    chain.damping = 0.18f;
    chain.inertia = 0.8f;
    chain.endLength = 0.05f;
    chain.selfCollision = true;

    auto added = eve::animation::hair::addChain(solver, chain);
    REQUIRE(added.ok());
    CHECK_EQ(added.value(), 0);
    CHECK_EQ(solver.getChainCount(), 1);

    auto headCol = eve::animation::hair::addHeadCollider(solver, "head", 0.12f);
    REQUIRE(headCol.ok());
    CHECK(headCol.value() >= 1);

    std::vector<eve::animation::hair::ChainDesc> batch = {chain};
    batch[0].rootBone = "missing_root";
    auto bad = eve::animation::hair::setupChains(solver, batch, false);
    CHECK(!bad.ok());
}
