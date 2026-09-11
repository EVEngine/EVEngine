#include "scene/editor/ScenePhysicsPlacement.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>

using namespace eve::scene_editor;

namespace {
PhysicsPlacementObject placementBox(const char* id, double x, double y, double z, double hx, double hy, double hz,
                                    bool selected) {
    PhysicsPlacementObject object;
    object.object      = eve::editing::ObjectId(id);
    object.transform.x = x;
    object.transform.y = y;
    object.transform.z = z;
    object.halfExtentX = hx;
    object.halfExtentY = hy;
    object.halfExtentZ = hz;
    object.selected    = selected;
    return object;
}
}  // namespace

TEST_CASE("scene.physicsPlacement.rejects_invalid_admission_without_a_world") {
    PhysicsPlacementRequest request;
    request.objects.push_back(placementBox("wall", 0, 0, 0, 1, 1, 1, false));
    CHECK_EQ(static_cast<int>(ScenePhysicsPlacementBackend::create(std::move(request)).code()),
             static_cast<int>(eve::editing::Status::Rejected));
}

TEST_CASE("scene.physicsPlacement.drives_a_private_box3d_world_without_mutating_admission") {
    PhysicsPlacementRequest request;
    request.sourceRevision = 17;
    request.objects.push_back(placementBox("floor", 0, -0.5, 0, 5, 0.5, 5, false));
    request.objects.push_back(placementBox("crate", 0, 0.5, 0, 0.5, 0.5, 0.5, true));
    auto created = ScenePhysicsPlacementBackend::create(request);
    REQUIRE(created.ok());
    auto placement = std::move(created.value());
    REQUIRE_EQ(placement->sourceRevision(), 17U);
    REQUIRE(placement->setHandlePosition(3.0, 0.5, 0.0).ok());
    for (std::uint64_t tick = 1; tick <= 30; ++tick) REQUIRE(placement->step(tick, 1.0 / 60.0).ok());
    auto frame = placement->placementFrame();
    REQUIRE(frame.ok());
    REQUIRE_EQ(frame.value().objects.size(), 2U);
    CHECK(frame.value().objects[1].transform.x > 2.0);
    CHECK_EQ(request.objects[1].transform.x, 0.0);
    auto samples = placement->capture();
    REQUIRE(samples.ok());
    REQUIRE_EQ(samples.value().size(), 1U);
    CHECK_EQ(samples.value()[0].object, std::string("crate"));
}

TEST_CASE("scene.physicsPlacement.fast_drag_reaches_the_handle_in_one_preview_step") {
    PhysicsPlacementRequest request;
    request.objects.push_back(placementBox("crate", 0.0, 0.5, 0.0, 0.5, 0.5, 0.5, true));
    auto created = ScenePhysicsPlacementBackend::create(std::move(request));
    REQUIRE(created.ok());
    auto placement = std::move(created.value());
    REQUIRE(placement->setHandlePosition(20.0, 0.5, 0.0).ok());
    REQUIRE(placement->step(1, 1.0 / 60.0).ok());
    auto frame = placement->placementFrame();
    REQUIRE(frame.ok());
    CHECK(frame.value().objects[0].transform.x > 19.5);
}

TEST_CASE("scene.physicsPlacement.resolves_collision_and_clones_independently") {
    PhysicsPlacementRequest request;
    request.objects.push_back(placementBox("wall", 2.0, 0.5, 0, 0.5, 2.0, 2.0, false));
    request.objects.push_back(placementBox("crate", 0.0, 0.5, 0, 0.5, 0.5, 0.5, true));
    auto created = ScenePhysicsPlacementBackend::create(std::move(request));
    REQUIRE(created.ok());
    auto placement = std::move(created.value());
    REQUIRE(placement->setHandlePosition(4.0, 0.5, 0.0).ok());
    for (std::uint64_t tick = 1; tick <= 90; ++tick) REQUIRE(placement->step(tick, 1.0 / 60.0).ok());
    auto frame = placement->placementFrame();
    REQUIRE(frame.ok());
    CHECK(frame.value().objects[1].transform.x < 1.05);
    auto clone = placement->cloneForPreview();
    REQUIRE(clone != nullptr);
    auto clonedFrame = clone->capture();
    REQUIRE(clonedFrame.ok());
    CHECK(std::abs(clonedFrame.value()[0].positionX) < 0.001);
}

TEST_CASE("scene.physicsPlacement.drop_uses_gravity_and_reports_settled_contact") {
    PhysicsPlacementRequest request;
    request.settings.mode = PhysicsPlacementMode::Drop;
    request.objects.push_back(placementBox("floor", 0, -0.5, 0, 5, 0.5, 5, false));
    request.objects.push_back(placementBox("crate", 0, 3.0, 0, 0.5, 0.5, 0.5, true));
    auto created = ScenePhysicsPlacementBackend::create(std::move(request));
    REQUIRE(created.ok());
    auto                  placement = std::move(created.value());
    PhysicsPlacementFrame frame;
    for (std::uint64_t tick = 1; tick <= 240; ++tick) {
        REQUIRE(placement->step(tick, 1.0 / 60.0).ok());
        auto captured = placement->placementFrame();
        REQUIRE(captured.ok());
        frame = std::move(captured.value());
        if (frame.settled) break;
    }
    REQUIRE(frame.settled);
    REQUIRE(frame.colliding);
    CHECK(std::abs(frame.objects[1].transform.y - 0.5) < 0.06);
}

TEST_CASE("scene.physicsPlacement.rotates_a_multi_selection_as_one_rigid_layout") {
    PhysicsPlacementRequest request;
    request.objects.push_back(placementBox("left", -1.0, 0.5, 0, 0.25, 0.25, 0.25, true));
    request.objects.push_back(placementBox("right", 1.0, 0.5, 0, 0.25, 0.25, 0.25, true));
    auto created = ScenePhysicsPlacementBackend::create(std::move(request));
    REQUIRE(created.ok());
    auto placement = std::move(created.value());
    REQUIRE(placement->setHandleRotation(0.0, 0.0, 1.57079632679).ok());
    for (std::uint64_t tick = 1; tick <= 30; ++tick) REQUIRE(placement->step(tick, 1.0 / 60.0).ok());
    auto frame = placement->placementFrame();
    REQUIRE(frame.ok());
    CHECK(std::abs(frame.value().objects[0].transform.x) < 0.08);
    CHECK(std::abs(frame.value().objects[1].transform.x) < 0.08);
    CHECK(frame.value().objects[0].transform.y < -0.35);
    CHECK(frame.value().objects[1].transform.y > 1.35);
    CHECK(std::abs(frame.value().objects[0].transform.rotationZ - 1.57079632679) < 0.08);
}

TEST_CASE("scene.physicsPlacement.scales_layout_and_live_collision_shapes_from_the_primary_handle") {
    PhysicsPlacementRequest request;
    request.primaryObject  = eve::editing::ObjectId("right");
    auto left              = placementBox("left", -1.0, 0.5, 0, 0.25, 0.25, 0.25, true);
    auto right             = placementBox("right", 1.0, 0.5, 0, 0.25, 0.25, 0.25, true);
    right.transform.scaleX = right.transform.scaleY = right.transform.scaleZ = 2.0;
    request.objects                                                          = {left, right};
    auto created = ScenePhysicsPlacementBackend::create(std::move(request));
    REQUIRE(created.ok());
    auto placement = std::move(created.value());
    REQUIRE(placement->setHandleScale(4.0, 4.0, 4.0).ok());
    for (std::uint64_t tick = 1; tick <= 30; ++tick) REQUIRE(placement->step(tick, 1.0 / 60.0).ok());
    auto frame = placement->placementFrame();
    REQUIRE(frame.ok());
    CHECK(frame.value().objects[0].transform.x < -1.85);
    CHECK(frame.value().objects[1].transform.x > 1.85);
    CHECK(std::abs(frame.value().objects[0].transform.scaleX - 2.0) < 0.001);
    CHECK(std::abs(frame.value().objects[1].transform.scaleX - 4.0) < 0.001);
    CHECK_EQ(static_cast<int>(placement->setHandleScale(0.0, 1.0, 1.0).code()),
             static_cast<int>(eve::editing::Status::Rejected));
}

TEST_CASE("scene.physicsPlacement.supports_primitive_and_convex_preview_shapes") {
    PhysicsPlacementRequest request;
    auto                    sphere = placementBox("sphere", -3.0, 1.0, 0, 0.5, 0.5, 0.5, true);
    sphere.shape                   = PhysicsPlacementShape::Sphere;
    auto capsule                   = placementBox("capsule", 0.0, 1.0, 0, 0.4, 1.0, 0.4, false);
    capsule.shape                  = PhysicsPlacementShape::Capsule;
    auto hull                      = placementBox("hull", 3.0, 1.0, 0, 0.5, 0.5, 0.5, false);
    hull.shape                     = PhysicsPlacementShape::ConvexHull;
    hull.convexVertices            = {-0.5f, -0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.0f, 0.5f, -0.5f, 0.0f, 0.0f, 0.5f};
    request.objects                = {sphere, capsule, hull};
    auto created                   = ScenePhysicsPlacementBackend::create(std::move(request));
    REQUIRE(created.ok());
    REQUIRE(created.value()->step(1, 1.0 / 60.0).ok());
    auto frame = created.value()->placementFrame();
    REQUIRE(frame.ok());
    REQUIRE_EQ(frame.value().objects.size(), 3U);
}

TEST_CASE("scene.physicsPlacement.rejects_malformed_convex_preview_shape") {
    PhysicsPlacementRequest request;
    auto                    hull = placementBox("hull", 0.0, 0.0, 0.0, 0.5, 0.5, 0.5, true);
    hull.shape                   = PhysicsPlacementShape::ConvexHull;
    hull.convexVertices          = {0.0f, 0.0f, 0.0f};
    request.objects.push_back(std::move(hull));
    CHECK_EQ(static_cast<int>(ScenePhysicsPlacementBackend::create(std::move(request)).code()),
             static_cast<int>(eve::editing::Status::Rejected));
}

TEST_CASE("scene.physicsPlacement.aligns_selection_up_and_support_to_external_surface") {
    PhysicsPlacementRequest request;
    request.objects.push_back(placementBox("wall", 2.0, 0.5, 0.0, 0.5, 3.0, 3.0, false));
    request.objects.push_back(placementBox("crate", 0.0, 0.5, 0.0, 0.5, 0.5, 0.5, true));
    auto created = ScenePhysicsPlacementBackend::create(std::move(request));
    REQUIRE(created.ok());
    auto placement = std::move(created.value());
    REQUIRE(placement->alignHandleToSurface(5.0, 0.5, 0.0, -5.0, 0.5, 0.0, 0.02).ok());
    for (std::uint64_t tick = 1; tick <= 30; ++tick) REQUIRE(placement->step(tick, 1.0 / 60.0).ok());
    auto frame = placement->placementFrame();
    REQUIRE(frame.ok());
    REQUIRE(frame.value().surfaceAligned);
    CHECK(frame.value().surfaceNormalX > 0.99);
    CHECK(std::abs(frame.value().surfaceNormalY) < 0.01);
    CHECK(frame.value().objects[1].transform.x > 2.95);
    CHECK(std::abs(std::abs(frame.value().objects[1].transform.rotationZ) - 1.57079632679) < 0.08);
}

TEST_CASE("scene.physicsPlacement.builds_compound_colliders_and_honors_source_policy") {
    PhysicsPlacementRequest  request;
    auto                     object = placementBox("arch", 0.0, 0.0, 0.0, 1.5, 1.0, 0.5, true);
    PhysicsPlacementCollider left;
    left.source                        = PhysicsPlacementColliderSource::Existing;
    left.halfExtentX                   = 0.25;
    left.halfExtentY                   = 1.0;
    left.halfExtentZ                   = 0.5;
    left.localX                        = -1.0;
    PhysicsPlacementCollider right     = left;
    right.localX                       = 1.0;
    PhysicsPlacementCollider generated = left;
    generated.source                   = PhysicsPlacementColliderSource::Generated;
    generated.localX                   = 0.0;
    object.colliders                   = {left, right, generated};
    request.settings.colliderPolicy    = PhysicsPlacementColliderPolicy::ExistingOnly;
    request.objects.push_back(std::move(object));
    auto created = ScenePhysicsPlacementBackend::create(std::move(request));
    REQUIRE(created.ok());
    REQUIRE(created.value()->setHandleScale(2.0, 1.0, 1.0).ok());
    REQUIRE(created.value()->step(1, 1.0 / 60.0).ok());

    PhysicsPlacementRequest rejected;
    rejected.settings.colliderPolicy = PhysicsPlacementColliderPolicy::ExistingOnly;
    rejected.objects.push_back(placementBox("generated", 0, 0, 0, 1, 1, 1, true));
    CHECK_EQ(static_cast<int>(ScenePhysicsPlacementBackend::create(std::move(rejected)).code()),
             static_cast<int>(eve::editing::Status::Rejected));
}

TEST_CASE("scene.physicsPlacement.filters_background_but_rejects_filtered_selection") {
    PhysicsPlacementRequest request;
    auto                    ignored = placementBox("ignored", 5, 0, 0, 1, 1, 1, false);
    ignored.layerBits               = 2;
    request.objects.push_back(ignored);
    request.objects.push_back(placementBox("selected", 0, 0, 0, 1, 1, 1, true));
    request.admission.includedLayerBits = 1;
    auto created                        = ScenePhysicsPlacementBackend::create(request);
    REQUIRE(created.ok());
    auto frame = created.value()->placementFrame();
    REQUIRE(frame.ok());
    REQUIRE_EQ(frame.value().objects.size(), 1U);
    request.objects[1].locked = true;
    CHECK_EQ(static_cast<int>(ScenePhysicsPlacementBackend::create(std::move(request)).code()),
             static_cast<int>(eve::editing::Status::Rejected));
}

TEST_CASE("scene.physicsPlacement.rotate_and_point_are_distinct_tool_policies") {
    PhysicsPlacementRequest rotate;
    rotate.settings.mode = PhysicsPlacementMode::Rotate;
    rotate.objects.push_back(placementBox("left", -1, 0, 0, 0.25, 0.25, 0.25, true));
    rotate.objects.push_back(placementBox("right", 1, 0, 0, 0.25, 0.25, 0.25, true));
    auto rotated = ScenePhysicsPlacementBackend::create(std::move(rotate));
    REQUIRE(rotated.ok());
    REQUIRE(rotated.value()->setHandlePosition(100, 100, 100).ok());
    REQUIRE(rotated.value()->setHandleRotation(0, 0, 1.57079632679).ok());
    REQUIRE(rotated.value()->step(1, 1.0 / 60.0).ok());
    auto rotateFrame = rotated.value()->placementFrame();
    REQUIRE(rotateFrame.ok());
    CHECK(std::abs(rotateFrame.value().objects[0].transform.x) < 0.1);

    PhysicsPlacementRequest point;
    point.settings.mode = PhysicsPlacementMode::Point;
    point.objects.push_back(placementBox("pointer", 0, 0, 0, 0.25, 0.25, 0.25, true));
    auto pointed = ScenePhysicsPlacementBackend::create(std::move(point));
    REQUIRE(pointed.ok());
    REQUIRE(pointed.value()->setHandlePosition(5, 0, 0).ok());
    REQUIRE(pointed.value()->step(1, 1.0 / 60.0).ok());
    auto pointFrame = pointed.value()->placementFrame();
    REQUIRE(pointFrame.ok());
    CHECK(std::abs(pointFrame.value().objects[0].transform.x) < 0.01);
    CHECK(std::abs(pointFrame.value().objects[0].transform.rotationY) > 1.0);
}

TEST_CASE("scene.physicsPlacement_supports_vector_gravity_motion_locks_and_teleport_threshold") {
    PhysicsPlacementRequest falling;
    falling.settings.mode            = PhysicsPlacementMode::Fall;
    falling.settings.gravityX        = 4.0;
    falling.settings.gravityY        = 0.0;
    falling.settings.freezePositionX = true;
    falling.objects.push_back(placementBox("locked", 0, 0, 0, 0.25, 0.25, 0.25, true));
    auto locked = ScenePhysicsPlacementBackend::create(std::move(falling));
    REQUIRE(locked.ok());
    for (std::uint64_t tick = 1; tick <= 30; ++tick) REQUIRE(locked.value()->step(tick, 1.0 / 60.0).ok());
    auto lockedFrame = locked.value()->placementFrame();
    REQUIRE(lockedFrame.ok());
    CHECK(std::abs(lockedFrame.value().objects[0].transform.x) < 0.001);

    PhysicsPlacementRequest teleport;
    teleport.settings.teleportDistance = 2.0;
    teleport.objects.push_back(placementBox("crate", 0, 0, 0, 0.5, 0.5, 0.5, true));
    auto teleported = ScenePhysicsPlacementBackend::create(std::move(teleport));
    REQUIRE(teleported.ok());
    REQUIRE(teleported.value()->setHandlePosition(10, 0, 0).ok());
    REQUIRE(teleported.value()->step(1, 1.0 / 60.0).ok());
    auto teleportFrame = teleported.value()->placementFrame();
    REQUIRE(teleportFrame.ok());
    CHECK(std::abs(teleportFrame.value().objects[0].transform.x - 10.0) < 0.001);
}
