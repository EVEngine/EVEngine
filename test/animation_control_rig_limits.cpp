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

TEST_CASE("animation.controlRig.limits.axesExtensionAndInvalidConfiguration") {
    RigControlLimits l;
    l.reference.py        = 2;
    l.translation[0].mode = RigAxisMode::Locked;
    l.translation[1]      = {RigAxisMode::Limited, -0.5f, 0.5f};
    l.rotation[0].mode = l.rotation[1].mode = RigAxisMode::Locked;
    l.rotation[2]                           = {RigAxisMode::Limited, -30, 45};
    l.scale                                 = {RigAxisMode::Limited, 0.8f, 1.2f};
    TransformTRS t;
    t.px = 9;
    t.py = 7;
    t.qz = 0.70710678f;
    t.qw = 0.70710678f;
    t.sx = t.sy = t.sz = 3;
    auto p             = constrainRigControl(l, t);
    REQUIRE(p.ok());
    CHECK(p.value().px == 0);
    CHECK(p.value().py == 2.5f);
    CHECK(std::abs(p.value().qz - std::sin(3.14159265f / 8)) < 1e-5f);
    CHECK(std::abs(p.value().sx - 1.2f) < 1e-5f);
    l.extension = true;
    CHECK(!constrainRigControl(l, t).ok());
    l.translation   = {};
    l.minimumLength = 0.5f;
    l.maximumLength = 1.5f;
    p               = constrainRigControl(l, t);
    REQUIRE(p.ok());
    CHECK(p.value().px == 0);
    CHECK(p.value().py == 3);
    t.py = -8;
    p    = constrainRigControl(l, t);
    REQUIRE(p.ok());
    CHECK(p.value().py == 1);
    l.reference.py = 0;
    CHECK(!constrainRigControl(l, t).ok());
    l.extension   = false;
    l.rotation[1] = {RigAxisMode::Limited, -100, 100};
    CHECK(!constrainRigControl(l, t).ok());
    l.rotation[1] = {};
    t.px          = std::numeric_limits<float>::quiet_NaN();
    CHECK(!constrainRigControl(l, t).ok());
}
TEST_CASE("animation.controlRig.limits.referenceRotationAndInterpolation") {
    RigControlLimits limits;
    limits.reference.qz = limits.reference.qw = std::sqrt(0.5f);
    limits.rotation[0]                        = {RigAxisMode::Limited, -30, 30};
    limits.rotation[1].mode = limits.rotation[2].mode = RigAxisMode::Locked;
    TransformTRS t;
    t.qx = t.qy = t.qz = t.qw = 0.5f;
    auto projected            = constrainRigControl(limits, t);
    REQUIRE(projected.ok());
    CHECK(std::abs(projected.value().qx - std::sin(3.14159265f / 12) * std::sqrt(0.5f)) < 1e-5f);
    auto again = constrainRigControl(limits, projected.value());
    REQUIRE(again.ok());
    CHECK(again.value().qx == projected.value().qx);
    RigDefinition d;
    RigControl    c;
    c.id                   = "root";
    c.limits.rotation[2]   = {RigAxisMode::Limited, -170, 170};
    c.keys                 = {{0, {}}, {2, {}}};
    c.keys[0].transform.qz = std::sin(160 * 3.14159265f / 360);
    c.keys[1].transform.qz = -c.keys[0].transform.qz;
    c.keys[0].transform.qw = c.keys[1].transform.qw = std::cos(160 * 3.14159265f / 360);
    d.controls.push_back(c);
    AnimSkeleton skeleton;
    skeleton.addBone("root");
    auto rig = ControlRig::compile(d, skeleton);
    REQUIRE(rig.ok());
    auto middle = rig.value()->sampleControls(1);
    REQUIRE(middle.ok());
    const auto& q = middle.value()[0];
    CHECK(std::abs(2 * std::atan2(q.qz, q.qw) * 180 / 3.14159265f) <= 170.001f);
}
TEST_CASE("animation.controlRig.limits.codecMigrationSamplingAndBake") {
    RigDefinition d;
    RigControl    c;
    c.id                    = "root";
    c.limits.translation[0] = {RigAxisMode::Limited, -1, 1};
    c.keys                  = {{0, {}}, {2, {}}};
    c.keys[1].transform.px  = 9;
    d.controls.push_back(c);
    RigNode n;
    n.id      = "fk";
    n.control = n.bone = "root";
    d.nodes.push_back(n);
    auto encoded = encodeControlRig(d);
    REQUIRE(encoded.ok());
    auto decoded = decodeControlRig(encoded.value());
    REQUIRE(decoded.ok());
    CHECK(decoded.value().controls[0].keys[1].transform.px == 1);
    AnimSkeleton skeleton;
    skeleton.addBone("root");
    auto rig = ControlRig::compile(decoded.value(), skeleton);
    REQUIRE(rig.ok());
    AnimPose input(1), out;
    REQUIRE(rig.value()->evaluate(1, input, out).ok());
    CHECK(std::abs(out.local(0).px - 0.5f) < 1e-5f);
    auto clip = rig.value()->bake(30, input);
    REQUIRE(clip.ok());
    AnimPose baked;
    clip.value()->sample(1, &baked);
    CHECK(std::abs(baked.local(0).px - out.local(0).px) < 1e-5f);
    auto legacy = eve::Value::fromJson(
        R"({"schema":"eve.control-rig","version":1,"duration":2,"controls":[{"id":"old","parent":"","initial":[0,2,0,0,0,0,1,1,1,1],"shapeSize":0.1,"keys":[]}],"nodes":[]})");
    REQUIRE(legacy.ok());
    auto migrated = decodeControlRig(legacy.value());
    REQUIRE(migrated.ok());
    CHECK(migrated.value().controls[0].limits.reference.py == 2);
    CHECK(migrated.value().controls[0].limits.translation[0].mode == RigAxisMode::Free);
    encoded.value().find("controls")->at(0).find("limits")->set("unknown", true);
    CHECK(!decodeControlRig(encoded.value()).ok());
}
TEST_CASE("animation.controlRig.limits.ikCannotBypassLockedJoints") {
    AnimSkeleton skeleton;
    skeleton.addBone("root");
    skeleton.addBone("mid", 0);
    skeleton.addBone("tip", 1);
    skeleton.setBindPosition(1, 1, 0, 0);
    skeleton.setBindPosition(2, 1, 0, 0);
    RigDefinition d;
    for (int i = 0; i < 3; ++i) {
        RigControl c;
        c.id               = skeleton.getBoneName(i);
        c.parent           = i ? skeleton.getBoneName(i - 1) : "";
        c.initial          = skeleton.bindLocal(i);
        c.limits.reference = c.initial;
        for (auto& axis : c.limits.translation) axis.mode = RigAxisMode::Locked;
        for (auto& axis : c.limits.rotation) axis.mode = RigAxisMode::Locked;
        d.controls.push_back(c);
        RigNode n;
        n.id      = "fk" + std::to_string(i);
        n.control = n.bone = c.id;
        if (i) n.after = {"fk" + std::to_string(i - 1)};
        d.nodes.push_back(n);
    }
    RigControl target;
    target.id         = "target";
    target.initial.py = 1.5f;
    target.initial.px = 0.5f;
    d.controls.push_back(target);
    RigNode ik;
    ik.id        = "ik";
    ik.control   = "target";
    ik.bone      = "root";
    ik.middle    = "mid";
    ik.tip       = "tip";
    ik.operation = RigOperation::TwoBoneIK;
    ik.after     = {"fk2"};
    d.nodes.push_back(ik);
    auto rig = ControlRig::compile(d, skeleton);
    REQUIRE(rig.ok());
    AnimPose input, out;
    skeleton.applyBindPose(&input);
    REQUIRE(rig.value()->evaluate(0, input, out).ok());
    CHECK(std::abs(out.world(2).px - 2) < 1e-5f);
    CHECK(std::abs(out.world(2).py) < 1e-5f);
    RigNode duplicate = d.nodes[0];
    duplicate.id      = "duplicate";
    duplicate.control = "target";
    d.nodes.push_back(duplicate);
    CHECK(!ControlRig::compile(d, skeleton).ok());
    d.nodes.pop_back();
    d.controls[1].parent = "";
    CHECK(!ControlRig::compile(d, skeleton).ok());
}
