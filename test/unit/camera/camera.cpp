#include "camera/CameraController.h"
#include "common/CameraObstruction.h"
#include "common/Capability.h"
#include "graphics/RenderSystem3D.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <cstdint>
#include <string>

using namespace eve::camera;
using eve::graphics::Camera3D;

namespace {
bool near(float actual, float expected, float tolerance = 1e-3f) { return std::fabs(actual - expected) < tolerance; }

struct CameraRig {
    CameraController controller;
    Camera3D*        camera = Camera3D::createCamera();
    CameraRig() { controller.setCamera(camera); }
};

class ObstructionProbe final : public eve::ICameraObstructionQuery {
public:
    bool sphereCast(float, float, float, float, float, float, float, std::uint64_t mask, int ignored,
                    eve::CameraObstructionHit* hit) override {
        lastMask    = mask;
        lastIgnored = ignored;
        *hit        = {true, 42, .3f, 0.f, 0.f, 3.f, 0.f, 0.f, -1.f};
        return true;
    }
    std::uint64_t lastMask    = 0;
    int           lastIgnored = -1;
};

struct ScopedObstruction {
    explicit ScopedObstruction(ObstructionProbe& probe)
        : probe(probe), previous(eve::cap::query<eve::ICameraObstructionQuery>()) {
        eve::cap::provide<eve::ICameraObstructionQuery>(&probe);
    }
    ~ScopedObstruction() {
        if (previous)
            eve::cap::provide<eve::ICameraObstructionQuery>(previous);
        else
            eve::cap::revoke<eve::ICameraObstructionQuery>(&probe);
    }
    ObstructionProbe&             probe;
    eve::ICameraObstructionQuery* previous;
};
}  // namespace

TEST_CASE("camera.spec.modeAndPose") {
    SUB_CASE("mode parser accepts supported modes and preserves last valid mode on unknown input") {
        CameraController controller;
        REQUIRE(controller.getMode() == "follow");
        for (const char* mode : {"orbit", "topdown", "firstperson", "cinematic", "lockon"}) {
            controller.setMode(mode);
            REQUIRE(controller.getMode() == mode);
        }
        controller.setMode("unsupported");
        REQUIRE(controller.getMode() == "lockon");
    };
    SUB_CASE("follow snap applies target and offset exactly") {
        CameraRig rig;
        rig.controller.setTarget(0.f, 0.f, 0.f);
        rig.controller.setOffset(0.f, 2.f, 6.f);
        rig.controller.snap();
        REQUIRE(near(rig.camera->data()->eyeX, 0.f));
        REQUIRE(near(rig.camera->data()->eyeY, 2.f));
        REQUIRE(near(rig.camera->data()->eyeZ, 6.f));
        REQUIRE(near(rig.camera->data()->targetX, 0.f));
    };
    SUB_CASE("orbit and topdown use their distinct eye and up vectors") {
        CameraRig rig;
        rig.controller.setTarget(4.f, 1.f, -2.f);
        rig.controller.setRadius(10.f);
        rig.controller.setAzimuth(0.f);
        rig.controller.setElevation(0.f);
        rig.controller.setMode("orbit");
        rig.controller.snap();
        REQUIRE(near(rig.camera->data()->eyeX, 4.f));
        REQUIRE(near(rig.camera->data()->eyeY, 1.f));
        REQUIRE(near(rig.camera->data()->eyeZ, 8.f));
        rig.controller.setMode("topdown");
        rig.controller.snap();
        REQUIRE(near(rig.camera->data()->eyeX, 4.f));
        REQUIRE(near(rig.camera->data()->eyeY, 11.f));
        REQUIRE(near(rig.camera->data()->eyeZ, -2.f));
        REQUIRE(near(rig.camera->data()->upZ, -1.f));
    };
    SUB_CASE("lockon looks at midpoint and first-person looks forward from the target") {
        CameraRig rig;
        rig.controller.setTarget(0.f, 0.f, 0.f);
        rig.controller.setOffset(0.f, 2.f, 6.f);
        rig.controller.setLookAhead(0.f, 1.2f, 0.f);
        rig.controller.setSecondaryTarget(4.f, 0.f, 0.f);
        rig.controller.setMode("lockon");
        rig.controller.snap();
        REQUIRE(rig.controller.hasSecondaryTarget());
        REQUIRE(near(rig.camera->data()->eyeX, -6.f));
        REQUIRE(near(rig.camera->data()->targetX, 2.f));
        REQUIRE(near(rig.camera->data()->targetY, 1.2f));
        rig.controller.clearSecondaryTarget();
        REQUIRE(!rig.controller.hasSecondaryTarget());
        rig.controller.setYaw(0.f);
        rig.controller.setPitch(0.f);
        rig.controller.setMode("firstperson");
        rig.controller.snap();
        REQUIRE(near(rig.camera->data()->eyeX, 0.f));
        REQUIRE(near(rig.camera->data()->targetZ, 100.f));
    };
}

TEST_CASE("camera.spec.lensMotionAndObstruction") {
    SUB_CASE("lens clamps FOV and composes an off-center target") {
        CameraRig rig;
        rig.controller.setFov(200.f);
        REQUIRE(near(rig.controller.getFov(), 179.f));
        rig.controller.setComposition(.25f, 0.f);
        rig.controller.setTarget(0.f, 0.f, 0.f);
        rig.controller.setOffset(0.f, 0.f, 10.f);
        rig.controller.snap();
        REQUIRE(near(rig.camera->data()->fovYDeg, 179.f));
        REQUIRE(std::fabs(rig.camera->data()->targetX) > 1.f);
    };
    SUB_CASE("input and FOV impulse alter orbit and presentation temporarily") {
        CameraRig rig;
        rig.controller.setMode("orbit");
        rig.controller.setRadius(10.f);
        rig.controller.addInput(90.f, 10.f, -2.f);
        rig.controller.snap();
        REQUIRE(glm::length(glm::vec3(rig.camera->data()->eyeX, rig.camera->data()->eyeY, rig.camera->data()->eyeZ)) <
                8.1f);
        rig.controller.addFovImpulse(20.f, 1.f);
        rig.controller.update(.1f);
        REQUIRE(rig.camera->data()->fovYDeg > rig.controller.getFov());
    };
    SUB_CASE("smooth follow converges while maximum speed limits a single subsequent step") {
        CameraRig rig;
        rig.controller.setTarget(0.f, 0.f, 0.f);
        rig.controller.setOffset(0.f, 2.f, 6.f);
        rig.controller.setSmooth(10.f);
        rig.controller.update(1.f / 60.f);
        REQUIRE(near(rig.camera->data()->eyeY, 2.f, .01f));
        rig.controller.setTarget(10.f, 10.f, 10.f);
        for (int i = 0; i < 600; ++i) rig.controller.update(1.f / 60.f);
        REQUIRE(near(rig.camera->data()->eyeX, 10.f, .1f));
        REQUIRE(near(rig.camera->data()->eyeY, 12.f, .1f));
        rig.controller.setMaxSpeed(1.f);
        rig.controller.setTarget(100.f, 0.f, 0.f);
        const float before = rig.camera->data()->eyeX;
        rig.controller.update(1.f / 60.f);
        REQUIRE(std::fabs(rig.camera->data()->eyeX - before) <= 1.f / 60.f + .001f);
    };
    SUB_CASE("static collision retracts and recovers after clearing boxes") {
        CameraRig rig;
        rig.controller.setTarget(0.f, 0.f, 0.f);
        rig.controller.setOffset(0.f, 0.f, 10.f);
        rig.controller.setSmooth(100.f);
        rig.controller.setCollisionEnabled(true);
        rig.controller.setCollisionRadius(.25f);
        rig.controller.addCollisionBox(-1.f, -1.f, 4.f, 1.f, 1.f, 5.f);
        rig.controller.update(1.f / 60.f);
        REQUIRE(rig.controller.isObstructed());
        REQUIRE(rig.camera->data()->eyeZ < 4.f);
        rig.controller.clearCollisionBoxes();
        rig.controller.update(1.f / 60.f);
        REQUIRE(!rig.controller.isObstructed());
    };
    SUB_CASE("capability query carries mask and ignored body into obstruction cast") {
        ObstructionProbe  probe;
        ScopedObstruction registration(probe);
        CameraRig         rig;
        rig.controller.setTarget(0.f, 0.f, 0.f);
        rig.controller.setOffset(0.f, 0.f, 10.f);
        rig.controller.setCollisionEnabled(true);
        rig.controller.setCollisionMask(7);
        rig.controller.setCollisionIgnoredBody(9);
        rig.controller.update(0.f);
        REQUIRE(rig.controller.isObstructed());
        REQUIRE(rig.controller.getCollisionBodyId() == 42);
        REQUIRE(rig.camera->data()->eyeZ < 3.f);
        REQUIRE(probe.lastMask == 7);
        REQUIRE(probe.lastIgnored == 9);
    };
}

TEST_CASE("camera.spec.rigsTimelineAndPlayback") {
    SUB_CASE("priority selects an enabled rig and rejects an invalid mode") {
        CameraRig rig;
        REQUIRE(rig.controller.addRig("gameplay", "follow", 10));
        REQUIRE(rig.controller.addRig("boss", "orbit", 20));
        REQUIRE(!rig.controller.addRig("bad", "unknown", 30));
        rig.controller.update(0.f);
        REQUIRE(rig.controller.getActiveRig() == "boss");
        REQUIRE(rig.controller.getMode() == "orbit");
        REQUIRE(rig.controller.setRigEnabled("boss", false));
        rig.controller.update(.3f);
        REQUIRE(rig.controller.getActiveRig() == "gameplay");
        REQUIRE(rig.controller.removeRig("boss"));
    };
    SUB_CASE("timeline cuts, queues markers, interpolates FOV, and serializes") {
        CameraRig rig;
        REQUIRE(rig.controller.addRig("wide", "follow", 0));
        REQUIRE(rig.controller.addRig("hero", "orbit", 0));
        REQUIRE(rig.controller.addTimelineCut(0.f, "wide", 0.f));
        REQUIRE(rig.controller.addTimelineCut(1.f, "hero", .25f));
        REQUIRE(rig.controller.addTimelineEvent(.1f, "one", "1"));
        REQUIRE(rig.controller.addTimelineEvent(.2f, "two", "2"));
        REQUIRE(rig.controller.addTimelineFloat(0.f, "fov", 40.f));
        REQUIRE(rig.controller.addTimelineFloat(1.f, "fov", 80.f));
        REQUIRE(!rig.controller.addTimelineFloat(0.f, "unknown", 1.f));
        rig.controller.playTimeline(false);
        rig.controller.update(.5f);
        REQUIRE(rig.controller.getActiveRig() == "wide");
        REQUIRE(rig.controller.getPendingTimelineEventCount() == 2);
        REQUIRE(rig.controller.consumeTimelineEvent() == "one");
        REQUIRE(rig.controller.getTimelineEventData() == "1");
        REQUIRE(rig.controller.consumeTimelineEvent() == "two");
        REQUIRE(rig.controller.getTimelineEventData() == "2");
        REQUIRE(near(rig.controller.getFov(), 60.f));
        rig.controller.update(.6f);
        REQUIRE(rig.controller.getActiveRig() == "hero");
        REQUIRE(!rig.controller.isTimelinePlaying());
        auto             serialized = rig.controller.serializeAsset();
        CameraController loaded;
        REQUIRE(loaded.deserializeAsset(serialized));
        REQUIRE(loaded.getRigCount() == 2);
        REQUIRE(near(loaded.getTimelineDuration(), 1.f));
        REQUIRE(!loaded.deserializeAsset("{}"));
    };
    SUB_CASE("cinematic switch blends between views and sequence stops without losing target") {
        CameraRig rig;
        rig.controller.setMode("cinematic");
        rig.controller.addView("a", 0.f, 1.f, 2.f, 3.f, 4.f, 5.f);
        rig.controller.addView("b", 6.f, 7.f, 8.f, 9.f, 10.f, 11.f);
        REQUIRE(!rig.controller.switchTo("absent", 1.f));
        REQUIRE(rig.controller.switchTo("a", 0.f));
        rig.controller.update(0.f);
        REQUIRE(near(rig.camera->data()->eyeX, 0.f));
        REQUIRE(rig.controller.switchTo("b", 2.f));
        rig.controller.update(1.f);
        REQUIRE(near(rig.camera->data()->eyeX, 3.f, .05f));
        rig.controller.update(1.f);
        REQUIRE(near(rig.camera->data()->eyeX, 6.f, .05f));
        rig.controller.playSequence(.5f);
        REQUIRE(rig.controller.isPlaying());
        rig.controller.stopSequence();
        REQUIRE(!rig.controller.isPlaying());
    };
    SUB_CASE("registered combat cue produces a transient position and FOV impulse") {
        CameraRig rig;
        rig.controller.setTarget(0.f, 0.f, 0.f);
        rig.controller.setOffset(0.f, 0.f, 5.f);
        rig.controller.snap();
        REQUIRE(!rig.controller.getActionCuesEnabled());
        rig.controller.setActionCuesEnabled(true);
        auto registry = eve::action::ActionNotifyRegistry::withBuiltins();
        REQUIRE(registry.ok());
        eve::action::ActionTimelineEvent event{eve::action::ActionTimelineEventKind::Notify,
                                               *eve::LogicalId::parse("camera-track:main"),
                                               *eve::LogicalId::parse("camera-cue:impact"),
                                               *eve::LogicalId::parse("presentation:camera"),
                                               eve::Duration::zero(),
                                               {{"cue", "combat:impact"},
                                                {"positionAmplitude", 1.0},
                                                {"rotationAmplitude", 0.0},
                                                {"fovAmplitude", 5.0},
                                                {"durationSeconds", .2},
                                                {"seed", 9}}};
        eve::action::ActionNotifyContext context;
        context.executionId = eve::action::ActionExecutionId{303};
        REQUIRE(registry.value().dispatch(event, context).ok());
        rig.controller.update(.05f);
        REQUIRE(std::fabs(rig.camera->data()->eyeX) > 1e-3f);
        REQUIRE(rig.camera->data()->fovYDeg > rig.controller.getFov());
        rig.controller.setActionCuesEnabled(false);
        REQUIRE(!rig.controller.getActionCuesEnabled());
    };
    SUB_CASE("no-camera updates and snaps are safe no-ops") {
        CameraController controller;
        controller.setMode("orbit");
        controller.snap();
        controller.update(.1f);
        controller.playSequence(1.f);
        controller.update(1.f);
        controller.stopSequence();
        REQUIRE(!controller.isPlaying());
    };
}

TEST_CASE("graphics.camera3d.spec.renderParameters") {
    Camera3D* camera = Camera3D::createCamera();
    REQUIRE(camera->getLayerCullDistance(7) == 0.f);
    camera->setLayerCullDistance(7, 125.f);
    camera->setShadowLayerCullDistance(7, 80.f);
    REQUIRE(camera->getLayerCullDistance(7) == 125.f);
    REQUIRE(camera->getShadowLayerCullDistance(7) == 80.f);
    auto* renderable = eve::graphics::Renderable3D::create();
    renderable->setLayer(7);
    REQUIRE(renderable->getLayer() == 7);
    camera->setDepthOfField(20.f, 5.f, 12.f);
    REQUIRE(camera->data()->dofFocusDistance == 20.f);
    REQUIRE(camera->data()->dofMaxBlurPx == 5.f);
    camera->clearDepthOfField();
    REQUIRE(camera->data()->dofMaxBlurPx == 0.f);
}
