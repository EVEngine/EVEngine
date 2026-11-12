// Authored property presence follows pristine core/act.js poseAt, not effective rig defaults.
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include "stylize/pixelfx/Math.h"
#include "stylize/pixelfx/Rig.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
namespace test_pixelfx_act_pose_unit {
using namespace eve;
using namespace eve::stylize::pixelfx::rig;
PoseTrack prepare(const char* json) {
    auto document = Value::fromJson(json);
    if (!document) throw std::runtime_error(document.status().describe());
    auto track = PoseTrack::prepare(document.value());
    if (!track) throw std::runtime_error(track.status().describe());
    return std::move(track).takeValue();
}
TEST_CASE("stylize.pixelfx.actPose.authoredEndpointsSnapAndDuplicateKeys") {
    auto track = prepare(
        R"({"schema":"eve.pixelfx.pose-track","version":1,"keys":[[0,{"x":2,"weapon":"bow"}],[1,{"ffx":12,"two":true},"snap"],[1,{"fbx":-8},"snap"],[2,{}]]})");
    for (double t : {-1., 0., .5, .999}) {
        auto sample = track.sampleAuthored(t);
        REQUIRE(sample);
        REQUIRE(sample.value().contains(PoseField::X));
        REQUIRE(sample.value().contains(PoseField::Weapon));
        REQUIRE(!sample.value().contains(PoseField::Ffx));
        REQUIRE(!sample.value().contains(PoseField::Fbx));
        REQUIRE(!sample.value().contains(PoseField::Lean));
        REQUIRE(sample.value().pose.ffx == -2);  // Effective value remains available to the rig.
    }
    auto duplicate = track.sampleAuthored(1);
    REQUIRE(duplicate);
    REQUIRE(duplicate.value().contains(PoseField::Ffx));  // Interpolated samples merge STAND.
    REQUIRE(duplicate.value().contains(PoseField::Fbx));
    REQUIRE(duplicate.value().pose.fbx == -8);
    REQUIRE(!duplicate.value().contains(PoseField::Weapon));
    REQUIRE(!duplicate.value().contains(PoseField::Two));
    for (double t : {2., 3.}) {
        auto end = track.sampleAuthored(t);
        REQUIRE(end);
        REQUIRE(end.value().presence == 0);
    }
}
TEST_CASE("stylize.pixelfx.actPose.interpolatedNumbersAndHeldMetadata") {
    auto track = prepare(
        R"({"schema":"eve.pixelfx.pose-track","version":1,"keys":[[0,{"hx":4,"weapon":"sword","pull":true}],[1,{"ghost":1,"weapon":"greatsword","two":true,"turn":0.3},"line"]]})");
    for (double t : {.001, .25, .5, .999}) {
        auto sampled = track.sampleAuthored(t);
        auto legacy  = track.sample(t);
        REQUIRE(sampled);
        REQUIRE(legacy);
        const auto& q = sampled.value();
        REQUIRE(q.contains(PoseField::Ffx));
        REQUIRE(q.contains(PoseField::Fbx));
        REQUIRE(q.contains(PoseField::X));
        REQUIRE(q.contains(PoseField::Y));
        REQUIRE(q.contains(PoseField::Hx));
        REQUIRE(q.contains(PoseField::Ghost));
        REQUIRE(q.contains(PoseField::Turn));
        REQUIRE(q.contains(PoseField::Weapon));
        REQUIRE(q.contains(PoseField::Pull));
        REQUIRE(!q.contains(PoseField::Two));
        REQUIRE(q.pose.weapon == Weapon::Sword);
        REQUIRE(q.pose.ghost == 1);
        REQUIRE(q.pose.turn == .3);
        REQUIRE(q.pose.hx == legacy.value().hx);
        REQUIRE(q.pose.ffx == legacy.value().ffx);
    }
    auto end = track.sampleAuthored(1);
    REQUIRE(end);
    REQUIRE(!end.value().contains(PoseField::Hx));
    REQUIRE(!end.value().contains(PoseField::Pull));
    REQUIRE(!end.value().contains(PoseField::Ffx));
    REQUIRE(end.value().contains(PoseField::Two));
    REQUIRE(end.value().pose.weapon == Weapon::Greatsword);
}
TEST_CASE("stylize.pixelfx.actPose.snapshotLifetimeInvalidTimeAndTypedNames") {
    auto snapshot = [&] {
        auto track =
            prepare(R"({"schema":"eve.pixelfx.pose-track","version":1,"keys":[[0,{"ffx":11}],[1,{"ffx":15},"line"]]})");
        auto value = track.sampleAuthored(.5);
        if (!value) throw std::runtime_error(value.status().describe());
        auto moved   = std::move(track);
        auto invalid = moved.sampleAuthored(std::numeric_limits<double>::infinity());
        REQUIRE(!invalid);
        REQUIRE(invalid.status().diagnostics().front().path() == "/track/time");
        return std::move(value).takeValue();
    }();
    REQUIRE(snapshot.contains(PoseField::Ffx));
    REQUIRE(snapshot.pose.ffx == 13);
    REQUIRE(!snapshot.contains(PoseField::Count));
    REQUIRE(!snapshot.contains(static_cast<PoseField>(1000)));
    const auto names = poseFieldNames();
    REQUIRE(names.size() == size_t(PoseField::Count));
    REQUIRE(names[size_t(PoseField::Ffx)] == "ffx");
    REQUIRE(names[size_t(PoseField::Fbx)] == "fbx");
}
TEST_CASE("stylize.pixelfx.geometry.sourceNormOrderingAndExceptionalValues") {
    using namespace eve::stylize::pixelfx;
    // Expected binary64 values come from the pinned source runtime, not std::hypot.
    const auto scalar = [](uint64_t bits) { return std::bit_cast<double>(bits); };
    REQUIRE(std::bit_cast<uint64_t>(jsHypot2(scalar(0x4074699e42da1139ULL), scalar(0xc089db52fb4932b4ULL))) ==
            0x408bcc55c15e623bULL);
    REQUIRE(std::bit_cast<uint64_t>(jsHypot2(scalar(0x4010883e705fb3dfULL), scalar(0xc0306b442328d022ULL))) ==
            0x4030ee63ec0ef069ULL);
    REQUIRE(std::bit_cast<uint64_t>(jsHypot3(scalar(0x4010883e705fb3dfULL), scalar(0xc0306b442328d022ULL),
                                             scalar(0x40555148fab2382bULL))) == 0x4055bbd318920a9eULL);
    REQUIRE(std::bit_cast<uint64_t>(jsHypot3(scalar(0x40aa357289f91e45ULL), scalar(0x40aade89e2af1d6bULL),
                                             scalar(0x4093448c1a2df467ULL))) == 0x40b3602db3766684ULL);
    const double inf = std::numeric_limits<double>::infinity();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    REQUIRE(jsHypot2(inf, nan) == inf);
    REQUIRE(jsHypot2(nan, -inf) == inf);
    REQUIRE(jsHypot3(nan, 1, -inf) == inf);
    REQUIRE(jsHypot3(-inf, nan, 0) == inf);
    REQUIRE(std::isnan(jsHypot2(nan, 1)));
    REQUIRE(std::isnan(jsHypot3(1, nan, 2)));
    REQUIRE(std::bit_cast<uint64_t>(jsHypot2(-0., 0.)) == 0);
    REQUIRE(std::bit_cast<uint64_t>(jsHypot3(-0., -0., -0.)) == 0);
    REQUIRE(jsHypot2(3e-300, 4e-300) == 5e-300);
    REQUIRE(jsHypot3(0, 3e300, 4e300) == 5e300);
}
}  // namespace test_pixelfx_act_pose_unit
