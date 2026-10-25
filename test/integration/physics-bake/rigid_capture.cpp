#include <array>
#include <cmath>
#include <filesystem>
#include <random>
#include "graphics/RenderSystem.h"
#include "graphics/RenderTransformProjection.h"
#include "physics/Body.h"
#include "physics/Body3D.h"
#include "physics/World.h"
#include "physics/World3D.h"
#include "physics/backend/bake/BakeRecipe.h"
#include "physics/backend/bake/RigidPlayback.h"
#include "physics/backend/bake/RigidPoseTrack.h"
#include "physics/backend/bake/RigidRecording.h"
#include "physics/bake_capture/RigidBakeCapture.h"
#include "physics/bake_graphics/RigidBakePresentation.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve;
using namespace eve::physics;
namespace {
struct CaptureFiles {
    std::filesystem::path directory;
    CaptureFiles() {
        std::random_device random;
        for (int attempt = 0; attempt < 100; ++attempt) {
            auto candidate = std::filesystem::temp_directory_path() / ("eve-rigid-capture-" + std::to_string(random()));
            if (std::filesystem::create_directory(candidate)) {
                directory = std::move(candidate);
                return;
            }
        }
        throw std::runtime_error("cannot reserve capture test directory");
    }
    ~CaptureFiles() {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }
};
PersistentId trackId() {
    PersistentId::Bytes bytes{};
    bytes.back() = 17;
    return PersistentId(bytes);
}
BakeRecipe captureRecipe(bool planar) {
    BakeRecipe recipe;
    recipe.setCoordinates(planar ? BakeCoordinates::PlanarPixels : BakeCoordinates::MetresRightHandedYUp)
        .expect("coordinates");
    recipe.setProducer("rigid-capture-test").expect("producer");
    recipe.setBackend(planar ? "box2d-test" : "box3d-test").expect("backend");
    recipe.setSource("body", planar ? "rectangle" : "box").expect("source");
    recipe.setParameters("zero-gravity-linear-velocity").expect("parameters");
    recipe.setTiming(1.f / 60.f, 30).expect("timing");
    return recipe;
}
}  // namespace
TEST_CASE("physics.bake.capture2DPreservesPixelsAfterWorldDestruction") {
    const auto recipe   = captureRecipe(true);
    auto       recorder = RigidRecording::prepare(recipe, 8).expect("recording");
    {
        World world(0, 0, false, 100);
        auto* body = world.newBody("dynamic", 20, 30);
        REQUIRE(body != nullptr);
        REQUIRE(body->newRectangleFixture(10, 10) != nullptr);
        body->setLinearVelocity(40, 0);
        std::array bindings{RigidBakeBinding{trackId(), world.runtimeHandle(), body->runtimeHandle()}};
        REQUIRE(recorder.append(captureRigidFrame(world, bindings, 0).expect("initial capture")).ok());
        for (int i = 0; i < 30; ++i) world.update(1.f / 60.f);
        REQUIRE(recorder.append(captureRigidFrame(world, bindings, 0.5f).expect("moving capture")).ok());
        World foreign(0, 0, false, 100);
        REQUIRE(!captureRigidFrame(foreign, bindings, 0).ok());
        std::array duplicate{bindings[0], bindings[0]};
        REQUIRE(!captureRigidFrame(world, duplicate, 0).ok());
        body->destroy();
        auto stale = captureRigidFrame(world, bindings, 1);
        REQUIRE(!stale.ok());
        REQUIRE(stale.status().primaryDiagnostic()->code() == DiagnosticCode::StaleHandle);
        REQUIRE(recorder.frameCount() == 2);
    }
    auto         track = recorder.freeze().expect("freeze");
    CaptureFiles files;
    const auto   path = (files.directory / "captured.rigid").string();
    REQUIRE(track.save(path).ok());
    auto       loaded = RigidPoseTrack::loadForRecipe(path, recipe).expect("reload captured track");
    const auto pose   = loaded.sample(0.25f, trackId()).expect("after world destruction and reload");
    REQUIRE(std::abs(pose.x - 30.f) < 0.1f);
    REQUIRE(std::abs(pose.y - 30.f) < 0.1f);
    REQUIRE(pose.z == 0);
    auto* target = graphics::Renderable2D::create();
    REQUIRE(target != nullptr);
    RigidPlayback playback(loaded);
    REQUIRE(playback.seek(0.25).ok());
    REQUIRE(submitRigidPlayback(playback, trackId(), *target).ok());
    REQUIRE(std::abs(target->getX() - 30.f) < 0.1f);
    REQUIRE(std::abs(target->getY() - 30.f) < 0.1f);
    ecs::DestroyEntity(target);
}
TEST_CASE("physics.bake.capture3DPreservesMetresAfterWorldDestruction") {
    const auto recipe   = captureRecipe(false);
    auto       recorder = RigidRecording::prepare(recipe, 8).expect("recording");
    {
        World3D world(0, 0, 0, false);
        auto*   body = world.newBody("dynamic", 2, 3, 4);
        REQUIRE(body != nullptr);
        REQUIRE(body->newBoxShape(1, 1, 1) != nullptr);
        body->setLinearVelocity(2, 0, 0);
        std::array bindings{RigidBakeBinding{trackId(), world.runtimeHandle(), body->runtimeHandle()}};
        REQUIRE(recorder.append(captureRigidFrame(world, bindings, 0).expect("initial capture")).ok());
        for (int i = 0; i < 30; ++i) world.update(1.f / 60.f);
        REQUIRE(recorder.append(captureRigidFrame(world, bindings, 0.5f).expect("moving capture")).ok());
        body->destroy();
        auto stale = captureRigidFrame(world, bindings, 1);
        REQUIRE(!stale.ok());
        REQUIRE(stale.status().primaryDiagnostic()->code() == DiagnosticCode::StaleHandle);
        REQUIRE(recorder.frameCount() == 2);
    }
    auto         track = recorder.freeze().expect("freeze");
    CaptureFiles files;
    const auto   path = (files.directory / "captured.rigid").string();
    REQUIRE(track.save(path).ok());
    auto       loaded = RigidPoseTrack::loadForRecipe(path, recipe).expect("reload captured track");
    const auto pose   = loaded.sample(0.25f, trackId()).expect("after world destruction and reload");
    REQUIRE(std::abs(pose.x - 2.5f) < 0.1f);
    REQUIRE(std::abs(pose.y - 3.f) < 0.1f);
    REQUIRE(std::abs(pose.z - 4.f) < 0.1f);
    auto* target = graphics::Renderable3D::create();
    REQUIRE(target != nullptr);
    RigidPlayback playback(loaded);
    REQUIRE(playback.seek(0.25).ok());
    REQUIRE(submitRigidPlayback(playback, trackId(), *target).ok());
    const auto matrix = graphics::detail::modelFromTransform(*target->transform());
    REQUIRE(std::abs(matrix[3].x - 2.5f) < 0.1f);
    REQUIRE(std::abs(matrix[3].y - 3.f) < 0.1f);
    REQUIRE(std::abs(matrix[3].z - 4.f) < 0.1f);
    ecs::DestroyEntity(target);
}
