#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "camera/CameraController.h"
#include "common/CameraObstruction.h"
#include "common/Capability.h"
#include "graphics/RenderSystem3D.h"

#include <cmath>

using namespace eve::camera;
using eve::graphics::Camera3D;

namespace {

bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }

class ObstructionMock final : public eve::ICameraObstructionQuery {
public:
    bool sphereCast(float, float, float, float, float, float, float, uint64_t maskBits,
                    int ignoredBodyId, eve::CameraObstructionHit* out) override {
        seenMask = maskBits; seenIgnored = ignoredBodyId;
        *out = {true, 42, 0.3f, 0.f, 0.f, 3.f, 0.f, 0.f, -1.f};
        return true;
    }
    uint64_t seenMask = 0;
    int seenIgnored = -1;
};

}  // namespace

TEST_CASE("camera.modeRoundTrip") {
    CameraController cc;
    CHECK_EQ(cc.getMode(), std::string("follow"));
    cc.setMode("orbit");
    CHECK_EQ(cc.getMode(), std::string("orbit"));
    cc.setMode("topdown");
    CHECK_EQ(cc.getMode(), std::string("topdown"));
    cc.setMode("firstperson");
    CHECK_EQ(cc.getMode(), std::string("firstperson"));
    cc.setMode("cinematic");
    CHECK_EQ(cc.getMode(), std::string("cinematic"));
    cc.setMode("not-a-mode");
    CHECK_EQ(cc.getMode(), std::string("cinematic"));
}

TEST_CASE("camera.paramsRoundTrip") {
    CameraController cc;
    cc.setTarget(1.f, 2.f, 3.f);
    CHECK_EQ(cc.getTargetX(), 1.f);
    CHECK_EQ(cc.getTargetY(), 2.f);
    CHECK_EQ(cc.getTargetZ(), 3.f);
    cc.setOffset(0.5f, 1.5f, 2.5f);
    cc.setLookAhead(0.1f, 0.2f, 0.3f);
    cc.setRadius(7.f);
    cc.setAzimuth(90.f);
    cc.setElevation(45.f);
    cc.setOrbitSpeed(30.f);
    cc.setYaw(10.f);
    cc.setPitch(-5.f);
    cc.setSmooth(9.f);
    cc.setMaxSpeed(12.f);
    // Negative damping / max-speed are clamped to 0, never crash.
    cc.setSmooth(-1.f);
    cc.setMaxSpeed(-1.f);
    CHECK_EQ(cc.getCamera(), nullptr);
}

TEST_CASE("camera.followSnapPlacesEye") {
    CameraController cc;
    Camera3D*        cam = Camera3D::createCamera();
    cc.setCamera(cam);
    CHECK_EQ(cc.getCamera(), cam);
    cc.setTarget(0.f, 0.f, 0.f);
    cc.setOffset(0.f, 2.f, 6.f);
    cc.snap();
    CHECK(near(cam->data()->eyeX, 0.f));
    CHECK(near(cam->data()->eyeY, 2.f));
    CHECK(near(cam->data()->eyeZ, 6.f));
    CHECK(near(cam->data()->targetX, 0.f));
    CHECK(near(cam->data()->targetY, 0.f));
    CHECK(near(cam->data()->targetZ, 0.f));
}

TEST_CASE("camera.orbitAndTopdownSnap") {
    CameraController cc;
    Camera3D*        cam = Camera3D::createCamera();
    cc.setCamera(cam);
    cc.setTarget(4.f, 1.f, -2.f);
    cc.setRadius(10.f);
    cc.setAzimuth(0.f);
    cc.setElevation(0.f);
    cc.setMode("orbit");
    cc.snap();
    // az=0, el=0 -> dir = (0, 0, 1); eye = target + dir*radius.
    CHECK(near(cam->data()->eyeX, 4.f));
    CHECK(near(cam->data()->eyeY, 1.f));
    CHECK(near(cam->data()->eyeZ, 8.f));
    CHECK(near(cam->data()->targetX, 4.f));
    CHECK(near(cam->data()->targetY, 1.f));
    CHECK(near(cam->data()->targetZ, -2.f));

    cc.setMode("topdown");
    cc.snap();
    CHECK(near(cam->data()->eyeX, 4.f));
    CHECK(near(cam->data()->eyeY, 11.f));
    CHECK(near(cam->data()->eyeZ, -2.f));
    CHECK(near(cam->data()->upX, 0.f));
    CHECK(near(cam->data()->upY, 0.f));
    CHECK(near(cam->data()->upZ, -1.f));
}

TEST_CASE("camera.lensAndComposition") {
    CameraController cc;
    Camera3D*        cam = Camera3D::createCamera();
    cc.setCamera(cam);
    cc.setFov(200.f);
    CHECK(near(cc.getFov(), 179.f));
    cc.setComposition(0.25f, 0.f);
    cc.setTarget(0.f, 0.f, 0.f);
    cc.setOffset(0.f, 0.f, 10.f);
    cc.snap();
    CHECK(near(cam->data()->fovYDeg, 179.f));
    CHECK(std::fabs(cam->data()->targetX) > 1.f);
}

TEST_CASE("camera.inputAndFovModifier") {
    CameraController cc;
    Camera3D*        cam = Camera3D::createCamera();
    cc.setCamera(cam);
    cc.setMode("orbit");
    cc.setRadius(10.f);
    cc.addInput(90.f, 10.f, -2.f);
    cc.snap();
    CHECK(glm::length(glm::vec3(cam->data()->eyeX, cam->data()->eyeY, cam->data()->eyeZ)) < 8.1f);
    cc.setPositionSmooth(20.f);
    cc.setTargetSmooth(5.f);
    cc.addFovImpulse(20.f, 1.f);
    cc.update(0.1f);
    CHECK(cam->data()->fovYDeg > cc.getFov());
}

TEST_CASE("camera.collisionRetractsAndRecovers") {
    CameraController cc;
    Camera3D*        cam = Camera3D::createCamera();
    cc.setCamera(cam);
    cc.setTarget(0.f, 0.f, 0.f);
    cc.setOffset(0.f, 0.f, 10.f);
    cc.setSmooth(100.f);
    cc.setCollisionEnabled(true);
    cc.setCollisionRadius(0.25f);
    cc.addCollisionBox(-1.f, -1.f, 4.f, 1.f, 1.f, 5.f);
    cc.update(1.f / 60.f);
    CHECK(cc.isObstructed());
    CHECK(cam->data()->eyeZ < 4.f);
    cc.clearCollisionBoxes();
    cc.update(1.f / 60.f);
    CHECK(!cc.isObstructed());
}

TEST_CASE("camera.dynamicObstructionUsesCapability") {
    ObstructionMock mock;
    auto* previous = eve::cap::query<eve::ICameraObstructionQuery>();
    eve::cap::provide<eve::ICameraObstructionQuery>(&mock);
    CameraController cc;
    Camera3D* cam = Camera3D::createCamera();
    cc.setCamera(cam); cc.setTarget(0.f, 0.f, 0.f); cc.setOffset(0.f, 0.f, 10.f);
    cc.setCollisionEnabled(true); cc.setCollisionMask(7); cc.setCollisionIgnoredBody(9); cc.update(0.f);
    CHECK(cc.isObstructed()); CHECK_EQ(cc.getCollisionBodyId(), 42); CHECK(cam->data()->eyeZ < 3.f);
    CHECK_EQ(mock.seenMask, uint64_t(7)); CHECK_EQ(mock.seenIgnored, 9);
    if (previous) eve::cap::provide<eve::ICameraObstructionQuery>(previous);
    else eve::cap::revoke<eve::ICameraObstructionQuery>(&mock);
}

TEST_CASE("camera.directorSelectsPriorityAndBlends") {
    CameraController cc;
    Camera3D*        cam = Camera3D::createCamera();
    cc.setCamera(cam);
    CHECK(cc.addRig("gameplay", "follow", 10));
    CHECK(cc.addRig("boss", "orbit", 20));
    CHECK(!cc.addRig("bad", "unknown", 30));
    cc.update(0.f);
    CHECK_EQ(cc.getActiveRig(), std::string("boss"));
    CHECK_EQ(cc.getMode(), std::string("orbit"));
    CHECK(cc.setRigEnabled("boss", false));
    cc.update(0.3f);
    CHECK_EQ(cc.getActiveRig(), std::string("gameplay"));
    CHECK(cc.removeRig("boss"));
}

TEST_CASE("camera.impulseExpires") {
    CameraController cc;
    Camera3D*        cam = Camera3D::createCamera();
    cc.setCamera(cam);
    cc.setTarget(0.f, 0.f, 0.f);
    cc.setOffset(0.f, 0.f, 5.f);
    cc.snap();
    cc.addImpulse(1.f, 1.f, 0.1f, 7);
    cc.update(0.05f);
    CHECK(std::fabs(cam->data()->eyeX) > 1e-3f);
    cc.update(0.1f);
    for (int i = 0; i < 120; ++i) cc.update(1.f / 60.f);
    CHECK(near(cam->data()->eyeX, 0.f, 0.05f));
}

TEST_CASE("camera.timelineCutsAndEmitsMarkers") {
    CameraController cc;
    Camera3D*        cam = Camera3D::createCamera();
    cc.setCamera(cam);
    REQUIRE(cc.addRig("wide", "follow", 0));
    REQUIRE(cc.addRig("hero", "orbit", 0));
    CHECK(cc.addTimelineCut(0.f, "wide", 0.f));
    CHECK(cc.addTimelineCut(1.f, "hero", 0.25f));
    CHECK(cc.addTimelineEvent(0.5f, "camera.beat", "intro"));
    cc.playTimeline(false);
    cc.update(0.6f);
    CHECK_EQ(cc.getActiveRig(), std::string("wide"));
    CHECK_EQ(cc.consumeTimelineEvent(), std::string("camera.beat"));
    CHECK_EQ(cc.getTimelineEventData(), std::string("intro"));
    cc.update(0.5f);
    CHECK_EQ(cc.getActiveRig(), std::string("hero"));
    CHECK(!cc.isTimelinePlaying());
    cc.seekTimeline(0.f, false);
    CHECK(near(cc.getTimelineTime(), 0.f));
}

TEST_CASE("camera.timelineQueuesMarkersInterpolatesAndSerializes") {
    CameraController cc;
    Camera3D* cam = Camera3D::createCamera();
    cc.setCamera(cam);
    REQUIRE(cc.addRig("base", "orbit", 1));
    CHECK(cc.addTimelineEvent(0.1f, "one", "1"));
    CHECK(cc.addTimelineEvent(0.2f, "two", "2"));
    CHECK(cc.addTimelineFloat(0.f, "fov", 40.f));
    CHECK(cc.addTimelineFloat(1.f, "fov", 80.f));
    CHECK(!cc.addTimelineFloat(0.f, "unknown", 1.f));
    cc.playTimeline(false); cc.update(0.5f);
    CHECK_EQ(cc.getPendingTimelineEventCount(), 2);
    const std::string first = cc.consumeTimelineEvent();
    const std::string firstData = cc.getTimelineEventData();
    const std::string second = cc.consumeTimelineEvent();
    const std::string secondData = cc.getTimelineEventData();
    CHECK_EQ(first, std::string("one")); CHECK_EQ(firstData, std::string("1"));
    CHECK_EQ(second, std::string("two")); CHECK_EQ(secondData, std::string("2"));
    CHECK(near(cc.getFov(), 60.f));
    const std::string asset = cc.serializeAsset();
    CameraController loaded;
    CHECK(loaded.deserializeAsset(asset)); CHECK_EQ(loaded.getRigCount(), 1);
    CHECK(near(loaded.getTimelineDuration(), 1.f)); CHECK(!loaded.deserializeAsset("{}"));
}

TEST_CASE("camera.firstPersonSnap") {
    CameraController cc;
    Camera3D*        cam = Camera3D::createCamera();
    cc.setCamera(cam);
    cc.setTarget(0.f, 0.f, 0.f);
    cc.setYaw(0.f);
    cc.setPitch(0.f);
    cc.setMode("firstperson");
    cc.snap();
    CHECK(near(cam->data()->eyeX, 0.f));
    CHECK(near(cam->data()->eyeY, 0.f));
    CHECK(near(cam->data()->eyeZ, 0.f));
    CHECK(near(cam->data()->targetX, 0.f));
    CHECK(near(cam->data()->targetY, 0.f));
    CHECK(near(cam->data()->targetZ, 100.f));
}

TEST_CASE("camera.updateSmoothsTowardDesired") {
    CameraController cc;
    Camera3D*        cam = Camera3D::createCamera();
    cc.setCamera(cam);
    cc.setTarget(0.f, 0.f, 0.f);
    cc.setOffset(0.f, 2.f, 6.f);
    cc.setSmooth(10.f);
    cc.update(1.f / 60.f);  // first update snaps cur to desired
    CHECK(near(cam->data()->eyeY, 2.f, 1e-2f));
    CHECK(near(cam->data()->eyeZ, 6.f, 1e-2f));
    // Moving the target drags the camera along (after enough frames).
    cc.setTarget(10.f, 10.f, 10.f);
    for (int i = 0; i < 600; ++i) cc.update(1.f / 60.f);
    CHECK(near(cam->data()->eyeX, 10.f, 0.1f));
    CHECK(near(cam->data()->eyeY, 12.f, 0.1f));
    CHECK(near(cam->data()->eyeZ, 16.f, 0.1f));
}

TEST_CASE("camera.maxSpeedLimitsStep") {
    CameraController cc;
    Camera3D*        cam = Camera3D::createCamera();
    cc.setCamera(cam);
    cc.setTarget(100.f, 0.f, 0.f);
    cc.setOffset(0.f, 0.f, 0.f);
    cc.setSmooth(100.f);
    cc.setMaxSpeed(1.f);
    cc.update(1.f / 60.f);  // first frame snaps to (100,0,0)
    CHECK(near(cam->data()->eyeX, 100.f, 1e-2f));
    // Retarget far away: one frame may move at most maxSpeed*dt.
    cc.setTarget(0.f, 0.f, 0.f);
    cc.update(1.f / 60.f);
    CHECK(cam->data()->eyeX > 99.f);
    CHECK(cam->data()->eyeX < 100.f);
}

TEST_CASE("camera.cinematicViewsAndSwitch") {
    CameraController cc;
    Camera3D*        cam = Camera3D::createCamera();
    cc.setCamera(cam);
    cc.setMode("cinematic");
    cc.addView("a", 0.f, 1.f, 2.f, 3.f, 4.f, 5.f);
    cc.addView("b", 6.f, 7.f, 8.f, 9.f, 10.f, 11.f);
    CHECK(!cc.switchTo("nope", 1.f));
    CHECK(cc.switchTo("a", 0.f));
    cc.update(0.f);
    CHECK(near(cam->data()->eyeX, 0.f));
    CHECK(near(cam->data()->eyeZ, 2.f));

    CHECK(cc.switchTo("b", 2.f));
    cc.update(1.f);  // t=0.5, smoothstep(0.5)=0.5 -> halfway
    CHECK(near(cam->data()->eyeX, 3.f, 0.05f));
    cc.update(1.f);  // blend done -> view b
    CHECK(near(cam->data()->eyeX, 6.f, 0.05f));
    CHECK(near(cam->data()->eyeZ, 8.f, 0.05f));
}

TEST_CASE("camera.playSequenceCyclesViews") {
    CameraController cc;
    Camera3D*        cam = Camera3D::createCamera();
    cc.setCamera(cam);
    cc.setMode("cinematic");
    cc.addView("a", 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
    cc.addView("b", 10.f, 0.f, 0.f, 0.f, 0.f, 0.f);
    cc.playSequence(0.5f);
    CHECK(cc.isPlaying());
    cc.update(0.6f);  // timer >= stepTime -> switch to b, blend starts
    CHECK(cc.isPlaying());
    CHECK(cam->data()->eyeX > 0.01f);
    CHECK(cam->data()->eyeX < 10.f);
    cc.stopSequence();
    CHECK(!cc.isPlaying());
    // Playback stopped: the blend still finishes at view b and stays there.
    for (int i = 0; i < 120; ++i) cc.update(1.f / 60.f);
    CHECK(near(cam->data()->eyeX, 10.f, 0.05f));
}

TEST_CASE("camera.noCameraNoOp") {
    CameraController cc;
    cc.setMode("orbit");
    cc.snap();
    cc.update(0.1f);
    cc.playSequence(1.f);
    cc.update(1.f);
    cc.stopSequence();
    CHECK(!cc.isPlaying());
}
