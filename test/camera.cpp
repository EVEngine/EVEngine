#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "camera/CameraController.h"
#include "graphics/RenderSystem3D.h"

#include <cmath>

using namespace eve::camera;
using eve::graphics::Camera3D;

namespace {

bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }

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
    Camera3D *cam = Camera3D::createCamera();
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
    Camera3D *cam = Camera3D::createCamera();
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
}

TEST_CASE("camera.firstPersonSnap") {
    CameraController cc;
    Camera3D *cam = Camera3D::createCamera();
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
    Camera3D *cam = Camera3D::createCamera();
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
    Camera3D *cam = Camera3D::createCamera();
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
    Camera3D *cam = Camera3D::createCamera();
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
    Camera3D *cam = Camera3D::createCamera();
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
