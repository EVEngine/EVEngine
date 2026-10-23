#include <cmath>
#include <limits>
#include "animation/AnimClip.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/ControlRig.h"
#include "common/Value.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::animation;
namespace {
RigDefinition fkRig() {
    RigDefinition d;
    RigControl    c;
    c.id                   = "rootControl";
    c.initial.px           = 2;
    c.keys                 = {{0, c.initial}, {2, c.initial}};
    c.keys[1].transform.px = 4;
    d.controls.push_back(c);
    RigNode n;
    n.id      = "rootFK";
    n.control = c.id;
    n.bone    = "root";
    d.nodes.push_back(n);
    return d;
}
}  // namespace
TEST_CASE("animation.controlRig.fkTimelineBakeAndOwnedSkeleton") {
    std::unique_ptr<ControlRig> rig;
    {
        AnimSkeleton skeleton;
        skeleton.addBone("root");
        auto compiled = ControlRig::compile(fkRig(), skeleton);
        REQUIRE(compiled.ok());
        rig = std::move(compiled).takeValue();
    }
    AnimPose input(1), output;
    REQUIRE(rig->evaluate(1, input, output).ok());
    CHECK(std::abs(output.local(0).px - 3) < 1e-5f);
    auto baked = rig->bake(30, input);
    REQUIRE(baked.ok());
    AnimPose sampled;
    baked.value()->sample(1, &sampled);
    CHECK(std::abs(sampled.local(0).px - output.local(0).px) < 1e-5f);
    auto matched = rig->matchFK(output);
    REQUIRE(matched.ok());
    CHECK(std::abs(matched.value()[0].px - 3) < 1e-5f);
    auto encoded = encodeControlRig(fkRig());
    REQUIRE(encoded.ok());
    auto decoded = decodeControlRig(encoded.value());
    REQUIRE(decoded.ok());
    CHECK(decoded.value().controls[0].keys.size() == 2);
    encoded.value().set("unknown", true);
    CHECK(!decodeControlRig(encoded.value()).ok());
}
TEST_CASE("animation.controlRig.rejectsCyclesAndPreservesOutputOnFailure") {
    AnimSkeleton skeleton;
    skeleton.addBone("root");
    auto d               = fkRig();
    d.controls[0].parent = "rootControl";
    CHECK(!ControlRig::compile(d, skeleton).ok());
    d                = fkRig();
    d.nodes[0].after = {"rootFK"};
    CHECK(!ControlRig::compile(d, skeleton).ok());
    d               = fkRig();
    d.nodes[0].bone = "missing";
    CHECK(!ControlRig::compile(d, skeleton).ok());
    d                          = fkRig();
    d.controls[0].keys[1].time = 0;
    CHECK(!validateRigDefinition(d).ok());
    d                        = fkRig();
    d.controls[0].initial.sz = 1.0000012f;
    CHECK(validateRigDefinition(d).ok());
    d.controls[0].initial.sz = 1.01f;
    CHECK(!validateRigDefinition(d).ok());
    auto compiled = ControlRig::compile(fkRig(), skeleton);
    REQUIRE(compiled.ok());
    AnimPose input(1), output(1);
    output.local(0).px = 99;
    CHECK(!compiled.value()->evaluate(std::numeric_limits<double>::quiet_NaN(), input, output).ok());
    CHECK(output.local(0).px == 99);
    input.local(0).sx = 0;
    CHECK(!compiled.value()->evaluate(0, input, output).ok());
    CHECK(output.local(0).px == 99);
}
TEST_CASE("animation.controlRig.ikAndReparent") {
    AnimSkeleton skeleton;
    const int    root = skeleton.addBone("root");
    const int    mid = skeleton.addBone("mid", root), tip = skeleton.addBone("tip", mid);
    skeleton.setBindPosition(mid, 0, 1, 0);
    skeleton.setBindPosition(tip, 0, 1, 0);
    RigDefinition d;
    RigControl    c;
    c.id         = "hand";
    c.initial.px = 1;
    c.initial.py = 1;
    d.controls.push_back(c);
    RigNode n;
    n.id        = "ik";
    n.control   = "hand";
    n.bone      = "root";
    n.middle    = "mid";
    n.tip       = "tip";
    n.operation = RigOperation::TwoBoneIK;
    d.nodes.push_back(n);
    auto compiled = ControlRig::compile(d, skeleton);
    REQUIRE(compiled.ok());
    AnimPose input, output;
    skeleton.applyBindPose(&input);
    REQUIRE(compiled.value()->evaluate(0, input, output).ok());
    CHECK(std::abs(output.world(tip).px - 1) < 1e-4f);
    CHECK(std::abs(output.world(tip).py - 1) < 1e-4f);
    CHECK(!compiled.value()->matchFK(output).ok());
    RigControl parent;
    parent.id         = "space";
    parent.initial.px = 5;
    d.controls.push_back(parent);
    REQUIRE(reparentRigControl(d, "hand", "space").ok());
    CHECK(std::abs(d.controls[0].initial.px + 4) < 1e-5f);
    CHECK(!reparentRigControl(d, "space", "hand").ok());
    CHECK(d.controls[1].parent.empty());
    auto moved = ControlRig::compile(d, skeleton);
    REQUIRE(moved.ok());
    REQUIRE(moved.value()->evaluate(0, input, output).ok());
    CHECK(std::abs(output.world(tip).px - 1) < 1e-4f);
}

TEST_CASE("animation.controlRig.aimComposesOverInputAndFailsAtomically") {
    AnimSkeleton skeleton;
    skeleton.addBone("root");
    skeleton.addBone("unaffected");
    auto d = fkRig();
    d.controls[0].keys.clear();
    d.controls[0].initial.px = 1;
    d.nodes[0].operation     = RigOperation::Aim;
    auto compiled            = ControlRig::compile(d, skeleton);
    REQUIRE(compiled.ok());
    AnimPose input(2), output;
    input.local(1).py = 7;
    REQUIRE(compiled.value()->evaluate(0, input, output).ok());
    CHECK(std::abs(output.local(0).qy - std::sqrt(0.5f)) < 1e-4f);
    CHECK(output.local(1).py == 7);
    std::vector<TransformTRS> overrides(1);
    CHECK(!compiled.value()->evaluate(0, input, output, overrides).ok());
    CHECK(output.local(1).py == 7);
    CHECK(std::abs(output.local(0).qy - std::sqrt(0.5f)) < 1e-4f);
}

TEST_CASE("animation.controlRig.quaternionShortestPathAndBakeEndpoints") {
    AnimSkeleton skeleton;
    skeleton.addBone("root");
    auto d                             = fkRig();
    d.controls[0].keys[1].transform.qw = -1;
    auto compiled                      = ControlRig::compile(d, skeleton);
    REQUIRE(compiled.ok());
    auto sampled = compiled.value()->sampleControls(1);
    REQUIRE(sampled.ok());
    CHECK(std::abs(std::abs(sampled.value()[0].qw) - 1) < 1e-5f);
    AnimPose input(1), direct, bakedPose;
    auto     clip = compiled.value()->bake(29.5, input);
    REQUIRE(clip.ok());
    for (double t : {0.0, 1.0, 2.0}) {
        REQUIRE(compiled.value()->evaluate(t, input, direct).ok());
        clip.value()->sample(static_cast<float>(t), &bakedPose);
        CHECK(std::abs(direct.local(0).px - bakedPose.local(0).px) < 1e-4f);
    }
}
