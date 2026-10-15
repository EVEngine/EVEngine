#include <zeroerr/unittest.h>
#include <cmath>
#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>
#include "common/SquirrelBinding.h"
#include "scene/FollowPlayer.h"
#include "scene/LocationProfile.h"
#include "scene/LocationProfileBindings.h"
#include "scene/editor/ScenePhysicsPlacement.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// scene_follow_player.cpp
using namespace eve::scene;

TEST_CASE("scene.followPlayer.matchesPcgOffsetsAndWaterThreshold") {
    FollowPlayerSettings settings;
    FollowPlayerInput    input;
    FollowPlayerOutput   output;
    input.hasPlayer = true;
    input.playerX   = 10.f;
    input.playerY   = 0.5f;
    input.playerZ   = -20.f;
    REQUIRE(static_cast<bool>(evaluateFollowPlayer(&output, &settings, &input)));
    CHECK(output.applyPosition);
    CHECK_EQ(output.x, 10.f);
    CHECK_EQ(output.y, 0.5f);
    CHECK_EQ(output.z, -20.f);

    settings.useOffset = true;
    settings.offsetX   = 1250.f;
    settings.offsetY   = 200.f;
    settings.offsetZ   = 300.f;
    REQUIRE(static_cast<bool>(evaluateFollowPlayer(&output, &settings, &input)));
    CHECK_EQ(output.x, 1260.f);
    CHECK_EQ(output.y, -199.5f);
    CHECK_EQ(output.z, -320.f);

    settings.waterObject = true;
    REQUIRE(static_cast<bool>(evaluateFollowPlayer(&output, &settings, &input)));
    CHECK_EQ(output.y, -129.5f);
    input.playerY = 1.f;
    REQUIRE(static_cast<bool>(evaluateFollowPlayer(&output, &settings, &input)));
    CHECK_EQ(output.y, -189.f);
}

TEST_CASE("scene.followPlayer.scaleNoPlayerAndAtomicValidation") {
    FollowPlayerSettings settings;
    settings.followPlayer = false;
    settings.useScale     = true;
    settings.scaleX       = 2.f;
    settings.scaleY       = 3.f;
    settings.scaleZ       = 4.f;
    FollowPlayerInput  input;
    FollowPlayerOutput output;
    output.x = 77.f;
    REQUIRE(static_cast<bool>(evaluateFollowPlayer(&output, &settings, &input)));
    CHECK(!output.applyPosition);
    CHECK(output.applyScale);
    CHECK_EQ(output.scaleX, 2.f);
    CHECK_EQ(output.scaleY, 3.f);
    CHECK_EQ(output.scaleZ, 4.f);

    const auto unchanged = output;
    settings.offsetX     = std::numeric_limits<float>::quiet_NaN();
    CHECK(!evaluateFollowPlayer(&output, &settings, &input));
    CHECK_EQ(output.applyScale, unchanged.applyScale);
    CHECK_EQ(output.scaleX, unchanged.scaleX);
    CHECK(!evaluateFollowPlayer(nullptr, &settings, &input));
}

// scene_location_profile.cpp
namespace {
eve::scene::LocationPose pose(float x) {
    eve::scene::LocationPose value;
    value.x = x;
    return value;
}

eve::scene::LocationBookmark bookmark(std::string name, float x) {
    eve::scene::LocationBookmark value;
    value.name       = std::move(name);
    value.camera     = pose(x);
    value.player     = pose(x + 10);
    value.controller = "FlyingCamera";
    value.scene      = "World";
    return value;
}
}  // namespace

TEST_CASE("scene.locationProfile.oneShotLocationAndFailureAtomicity") {
    eve::scene::LocationProfile profile;
    REQUIRE(profile.saveLocation(pose(2), pose(12)).ok());
    CHECK(profile.hasSavedLocation());
    auto loaded = profile.loadLocation();
    REQUIRE(loaded.ok());
    CHECK(loaded.value().camera.x == 2);
    CHECK(loaded.value().player->x == 12);
    CHECK(!profile.hasSavedLocation());
    CHECK(!profile.loadLocation().ok());

    auto invalid = pose(3);
    invalid.qw   = std::numeric_limits<float>::quiet_NaN();
    CHECK(!profile.saveLocation(invalid).ok());
    CHECK(!profile.hasSavedLocation());
}

TEST_CASE("scene.locationProfile.bookmarksMatchPcgSelectionRules") {
    eve::scene::LocationProfile profile;
    REQUIRE(profile.addBookmark(bookmark("A", 1)).ok());
    REQUIRE(profile.addBookmark(bookmark("B", 2)).ok());
    REQUIRE(profile.addBookmark(bookmark("C", 3)).ok());
    CHECK(!profile.addBookmark(bookmark("B", 9)).ok());
    CHECK(profile.getBookmarkCount() == 3);
    CHECK(profile.previousBookmark(0).value() == 2);
    CHECK(profile.nextBookmark(2).value() == 0);

    auto snapshot = profile.loadBookmark(1);
    REQUIRE(snapshot.ok());
    snapshot.value().camera.x = 22;
    REQUIRE(profile.overrideBookmark(1, snapshot.value(), "FirstPerson", "Second").ok());
    CHECK(profile.bookmarkAt(1)->name == "B");
    CHECK(profile.bookmarkAt(1)->camera.x == 22);
    CHECK(profile.bookmarkAt(1)->controller == "FirstPerson");
    CHECK(profile.getBookmarkName(1).value() == "B");
    CHECK(profile.getBookmarkController(1).value() == "FirstPerson");
    CHECK(profile.getBookmarkScene(1).value() == "Second");
    CHECK(!profile.getBookmarkName(3).ok());

    auto selected = profile.removeBookmark(2, 2);
    REQUIRE(selected.ok());
    CHECK(selected.value() == 1);
    CHECK(profile.getBookmarkCount() == 2);
    selected = profile.removeBookmark(0, 1);
    REQUIRE(selected.ok());
    CHECK(selected.value() == 0);
    profile.clearBookmarks();
    CHECK(profile.getBookmarkCount() == 0);
    CHECK(!profile.nextBookmark(0).ok());
}

TEST_CASE("scene.locationProfile.squirrelContract") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    auto    table = vm.addTable("eve");
    eve::script::exposeResultBindings(table);
    eve::scene::exposeLocationProfileBindings(table);
    vm.run(vm.compileSource(R"(
        local profile = eve.LocationProfile();
        local camera = eve.LocationPose(); camera.x = 4.5;
        local player = eve.LocationPose(); player.x = 8.0;
        assert(profile.saveLocationWithPlayer(camera, player).ok);
        local saved = profile.loadLocation();
        assert(saved.ok && saved.value.hasPlayer && saved.value.camera.x == 4.5);
        local mark = eve.LocationBookmark(); mark.name = "Vista"; mark.controller = "Fly";
        mark.scene = "World"; mark.setCamera(camera); mark.setPlayer(player);
        assert(profile.addBookmark(mark).value == 0);
        assert(profile.getBookmarkName(0).value == "Vista");
        camera.x = 9.0; mark.setCamera(camera); mark.controller = "FirstPerson";
        assert(profile.overrideBookmark(0, mark).ok);
        assert(profile.loadBookmark(0).value.camera.x == 9);
        assert(profile.getBookmarkController(0).value == "FirstPerson");
        assert(profile.removeBookmark(0, 0).value == -1);
    )"));
}

TEST_CASE("scene.locationProfile.versionedJsonRoundTripAndAtomicRestore") {
    eve::scene::LocationProfile source;
    REQUIRE(source.saveLocation(pose(5), pose(15)).ok());
    REQUIRE(source.addBookmark(bookmark("Summit", 7)).ok());
    auto json = source.serializeJson();
    REQUIRE(json.ok());
    CHECK(json.value().find("eve.scene.location-profile") != std::string::npos);

    eve::scene::LocationProfile restored;
    REQUIRE(restored.restoreJson(json.value()).ok());
    CHECK(restored.hasSavedLocation());
    CHECK(restored.getBookmarkCount() == 1);
    CHECK(restored.getBookmarkName(0).value() == "Summit");
    CHECK(restored.loadBookmark(0).value().player->x == 17);

    CHECK(!restored
               .restoreJson(R"({"schemaId":"eve.scene.location-profile","schemaVersion":1,
        "saved":null,"bookmarks":[],"future":true})")
               .ok());
    CHECK(restored.getBookmarkCount() == 1);
    CHECK(!restored
               .restoreJson(R"({"schemaId":"eve.scene.location-profile","schemaVersion":2,
        "saved":null,"bookmarks":[]})")
               .ok());
    CHECK(restored.getBookmarkName(0).value() == "Summit");
}

// scene_physics_placement.cpp
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
