#include "zeroerr/unittest.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_box3d)
#include <cmath>
#include <limits>
#include <vector>
#include "PrivateTestScope.h"
#include "common/SdfField3D.h"
#include "physics/DistanceField3D.h"
#include "physics/DistanceField3DSdf.h"

namespace test_distance_field_contract {
using namespace eve::physics;
namespace {
DistanceField3D plane() {
    DistanceField3D field(7, 7, 7, 1.f, -3.f, -3.f, -3.f, 100.f);
    for (int z = 0; z < 7; ++z)
        for (int y = 0; y < 7; ++y)
            for (int x = 0; x < 7; ++x) field.setDistance(x, y, z, float(y - 3));
    return field;
}
DistanceField3D wall() {
    DistanceField3D field(7, 7, 7, 1.f, -3.f, -3.f, -3.f, 100.f);
    for (int z = 0; z < 7; ++z)
        for (int y = 0; y < 7; ++y)
            for (int x = 0; x < 7; ++x) field.setDistance(x, y, z, float(x - 3));
    return field;
}
}  // namespace

TEST_CASE("Distance field samples a plane continuously and exposes normal and portable blob") {
    auto field = plane();
    SUB_CASE("interpolation and boundary gradient") {
        CHECK(std::fabs(field.sample(.25f, .5f, -.25f) - .5f) < 1e-5f);
        for (float y : {-3.f, 0.f, 3.f}) {
            field.sampleNormal(0.f, y, 0.f);
            CHECK(std::fabs(field.getNormalX()) < 1e-5f);
            CHECK(field.getNormalY() > .999f);
            CHECK(std::fabs(field.getNormalZ()) < 1e-5f);
        }
        CHECK_EQ(field.sample(0.f, 4.f, 0.f), 100.f);
    };
    SUB_CASE("shared adapter and blob preserve field scale and revision") {
        DistanceField3DSdf adapter;
        adapter.bind(field);
        CHECK(std::fabs(adapter.sample(0.f, .5f, 0.f) - .5f) < 1e-5f);
        float nx = 0, ny = 0, nz = 0;
        CHECK(adapter.gradient(0.f, 0.f, 0.f, nx, ny, nz) == eve::SdfField3DGradientStatus::Defined);
        CHECK(ny > .9f);
        CHECK_EQ(adapter.revision(), field.getRevision());
        const auto blob = sdfField3DBlobFromDistanceField(field);
        REQUIRE(blob.ok());
        const auto restored = distanceFieldFromSdfField3DBlob(blob.value());
        REQUIRE(restored.ok());
        CHECK(std::fabs(restored.value().sample(.25f, .5f, -.25f) - .5f) < 1e-5f);
    };
}

TEST_CASE("Distance field mutation validates bulk data atomically and increments revision once") {
    DistanceField3D field(3, 2, 2, .5f, -1.f, 2.f, 4.f, 99.f);
    CHECK_EQ(field.getRevision(), 0u);
    CHECK_EQ(field.getSampleCount(), 12);
    std::vector<float> samples(12);
    for (std::size_t i = 0; i < samples.size(); ++i) samples[i] = float(i);
    field.setDistances(samples);
    REQUIRE_EQ(field.getRevision(), 1u);
    CHECK_EQ(field.getDistance(2, 0, 0), 2.f);
    CHECK_EQ(field.getDistance(0, 1, 0), 3.f);
    auto invalid = samples;
    invalid[4]   = std::numeric_limits<float>::quiet_NaN();
    CHECK_THROWS((field.setDistances(invalid), false));
    CHECK_THROWS((field.setDistances(std::vector<float>(11, 0.f)), false));
    CHECK_EQ(field.getRevision(), 1u);
    CHECK_EQ(field.getDistance(1, 1, 0), 4.f);
    field.fill(-2.f);
    CHECK_EQ(field.getRevision(), 2u);
    CHECK_THROWS((field.fill(std::numeric_limits<float>::infinity()), false));
    CHECK_EQ(field.getDistance(2, 1, 1), -2.f);
    const std::vector<float> region{10, 11, 12, 13, 14, 15, 16, 17};
    field.setDistanceRegion(1, 0, 0, 2, 2, 2, region);
    CHECK_EQ(field.getRevision(), 3u);
    CHECK_EQ(field.getDistance(2, 1, 1), 17.f);
    CHECK_EQ(field.getDistance(0, 0, 0), -2.f);
    CHECK_THROWS((field.setDistanceRegion(2, 0, 0, 2, 2, 2, region), false));
    CHECK_EQ(field.getRevision(), 3u);
    field.fillRegion(0, 1, 0, 1, 1, 2, 7.f);
    CHECK_EQ(field.getRevision(), 4u);
    CHECK_EQ(field.getDistance(0, 1, 1), 7.f);
}

TEST_CASE("Distance field swept sphere and capsule report time of impact and initial overlap") {
    auto field = plane();
    SUB_CASE("sphere stops at first contact and reports a miss at full fraction") {
        REQUIRE(field.castSphere(0, 1.75f, 0, .25f, 0, -3.5f, 0));
        CHECK(!field.didCastStartInside());
        CHECK(std::fabs(field.getCastFraction() - 1.5f / 3.5f) < 1e-4f);
        CHECK(std::fabs(field.getCollisionY() - .25f) < 1e-4f);
        CHECK(!field.castSphere(0, 1.75f, 0, .25f, 3, 0, 0));
        CHECK(std::fabs(field.getCastFraction() - 1.f) < 1e-6f);
    };
    SUB_CASE("capsule detects segment interior and an overlapped cast starts at zero") {
        CHECK(field.checkCapsule(-1, .25f, 0, 1, .25f, 0, .5f));
        CHECK(field.castCapsule(-1, 2, 0, 1, 2, 0, .5f, 0, -4, 0));
        CHECK(std::fabs(field.getCastFraction() - .375f) < 1e-4f);
        CHECK(field.castCapsule(-1, .25f, 0, 1, .25f, 0, .5f, 0, 2, 0));
        CHECK(field.didCastStartInside());
        CHECK(std::fabs(field.getCastFraction()) < 1e-6f);
    };
}

TEST_CASE("Distance field capsule mover slides on wall and recovers penetration") {
    auto field = wall();
    REQUIRE(field.moveCapsule(1.5f, -1, 0, 1.5f, 1, 0, .25f, -2, 0, 1));
    CHECK(field.getMoverDeltaX() < -1.24f);
    CHECK(field.getMoverDeltaX() > -1.26f);
    CHECK(field.getMoverDeltaZ() > .99f);
    CHECK(field.getMoverNormalX() > .999f);
    CHECK(field.moveCapsule(.1f, -1, 0, .1f, 1, 0, .25f, 0, 0, 0));
    CHECK(field.getMoverDeltaX() > .15f);
    CHECK(field.getMoverDeltaX() < .152f);
}

TEST_CASE("Distance field mover slope limit and skin decide grounded state") {
    auto field = plane();
    field.setMoverSkinWidth(.02f);
    CHECK_THROWS((field.setMoverSkinWidth(-.01f), false));
    CHECK_THROWS((field.setMoverUp(0, 0, 0), false));
    field.setMoverUp(0, 1, 1);
    field.setMoverSlopeLimit(40.f);
    REQUIRE(field.moveCapsule(0, .5f, 0, 0, 1.5f, 0, .25f, 0, -1, 0));
    CHECK(!field.isMoverGrounded());
    CHECK(field.getMoverGroundDot() > .70f);
    field.setMoverSlopeLimit(50.f);
    CHECK(field.moveCapsule(0, .5f, 0, 0, 1.5f, 0, .25f, 0, -1, 0));
    CHECK(field.isMoverGrounded());
}

}  // namespace test_distance_field_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_physics_jolt_cooking)
#include <cmath>
#include <memory>
#include <vector>
#include "PrivateTestScope.h"
#include "physics/Body3D.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"

namespace test_hull_cooking_contract {
using namespace eve::physics;
#if EVENGINE_TEST_HAS_JOLT
TEST_CASE("Convex hull cooking budgets unique boundary vertices and rejects invalid replacement atomically") {
    std::vector<float> points;
    for (int x : {-1, 1})
        for (int y : {-1, 1})
            for (int z : {-1, 1}) points.insert(points.end(), {float(x), float(y), float(z)});
    for (int i = 0; i < 32; ++i) points.insert(points.end(), {0, 0, 0});
    for (const auto backend : {RigidBackend3D::Box3D, RigidBackend3D::Jolt}) {
        auto made = World3D::createWithBackend(backend, 0, 0, 0, false);
        REQUIRE(made.ok());
        auto                     world = std::move(made).takeValue();
        std::unique_ptr<Body3D>  body(world->newBody("dynamic", 0, 0, 0));
        std::unique_ptr<Shape3D> hull(body->newConvexHullShape(points, 8));
        CHECK(std::fabs(body->getMass() - 8) < .02f);
        CHECK(hull->testPoint(.9f, .9f, .9f));
        const auto id = hull->getId();
        hull->setConvexHullVertices(points, 4);
        CHECK_EQ(hull->getId(), id);
        CHECK_EQ(hull->getConvexHullMaxVertices(), 4);
        const float              mass = body->getMass();
        const std::vector<float> flat{0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 1, 0};
        CHECK_THROWS((hull->setConvexHullVertices(flat, 4), false));
        CHECK_THROWS((hull->setConvexHullVertices(points, 255), false));
        CHECK_EQ(hull->getConvexHullMaxVertices(), 4);
        CHECK_EQ(body->getMass(), mass);
        CHECK_EQ(world->getShapeCount(), 1);
    }
}
#endif
}  // namespace test_hull_cooking_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_physics_jolt_contacts)
#include <cmath>
#include <memory>
#include "PrivateTestScope.h"
#include "physics/Body3D.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"

namespace test_contact_contract {
using namespace eve::physics;
#if EVENGINE_TEST_HAS_JOLT
TEST_CASE("Solved contacts report impulse consistent with body momentum on both rigid backends") {
    for (const auto backend : {RigidBackend3D::Box3D, RigidBackend3D::Jolt}) {
        auto made = World3D::createWithBackend(backend, 0, 0, 0, false);
        REQUIRE(made.ok());
        auto                     world = std::move(made).takeValue();
        std::unique_ptr<Body3D>  ground(world->newBody("static", 0, -.5f, 0));
        std::unique_ptr<Shape3D> floor(ground->newBoxShape(10, 1, 10));
        std::unique_ptr<Body3D>  body(world->newBody("dynamic", 0, .6f, 0));
        std::unique_ptr<Shape3D> sphere(body->newSphereShape(.5f));
        body->setLinearDamping(0);
        body->setAngularDamping(0);
        body->setLinearVelocity(0, -4, 0);
        sphere->setTag(72);
        for (int frame = 0; frame < 4; ++frame) {
            const float priorVelocity = body->getLinearVelocityY();
            world->updateFull(.025f, 1);
            const auto contact = world->queryBodyContactsOwned(body->runtimeHandle(), 4096);
            REQUIRE(contact.ok());
            float impulse = 0;
            for (const auto& point : contact.value().points) {
                CHECK_EQ(point.shapeTag, 72);
                CHECK(point.normalImpulse >= 0);
                impulse += point.totalNormalImpulse;
            }
            const float momentum = body->getMass() * (body->getLinearVelocityY() - priorVelocity);
            CHECK(std::fabs(impulse - momentum) < .03f);
        }
    }
}

TEST_CASE("Sensor contact emits neither solved impulse nor hit even when hit events are enabled") {
    for (const auto backend : {RigidBackend3D::Box3D, RigidBackend3D::Jolt}) {
        auto made = World3D::createWithBackend(backend, 0, 0, 0, false);
        REQUIRE(made.ok());
        auto                     world = std::move(made).takeValue();
        std::unique_ptr<Body3D>  ground(world->newBody("static", 0, 0, 0));
        std::unique_ptr<Shape3D> sensor(ground->newBoxShape(10, 1, 10));
        sensor->setSensor(true);
        std::unique_ptr<Body3D>  body(world->newBody("dynamic", 0, 1, 0));
        std::unique_ptr<Shape3D> sphere(body->newSphereShape(.5f));
        sphere->setHitEventsEnabled(true);
        body->setLinearVelocity(0, -5, 0);
        for (int frame = 0; frame < 30; ++frame) {
            world->update(.01f);
            CHECK_EQ(world->getHitCount(), 0);
            const auto contact = world->queryBodyContactsOwned(body->runtimeHandle(), 4096);
            REQUIRE(contact.ok());
            CHECK(contact.value().points.empty());
        }
    }
}
#endif
}  // namespace test_contact_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_physics_rigid_backend)
#include <memory>
#include "PrivateTestScope.h"
#include "physics/Body3D.h"
#include "physics/World3D.h"

namespace test_rigid_backend_contract {
using namespace eve::physics;
TEST_CASE("Rigid backend selection either constructs the requested provider or reports Unsupported") {
    const auto jolt = World3D::createWithBackend(RigidBackend3D::Jolt, 0, -9.8f, 0, true);
    if (jolt.ok()) {
        CHECK(jolt.value()->rigidBackendKind() == RigidBackend3D::Jolt);
    } else {
        REQUIRE(jolt.error() != nullptr);
        CHECK(jolt.error()->code() == eve::DiagnosticCode::Unsupported);
    }
    const auto box = World3D::createWithBackend(RigidBackend3D::Box3D, 0, -9.8f, 0, true);
    REQUIRE(box.ok());
    CHECK(box.value()->rigidBackendKind() == RigidBackend3D::Box3D);
    std::unique_ptr<Body3D> body(box.value()->newBody("dynamic", 0, 10, 0));
    box.value()->update(.02f);
    CHECK(body->getY() < 10.f);
}
}  // namespace test_rigid_backend_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_water_mesh)
#include <cmath>
#include <limits>
#include "PrivateTestScope.h"
#include "graphics/Water.h"
#include "graphics/WaterPlanarReflection.h"
#include "graphics/WaterReflectionMasker.h"
#include "graphics/WaterStyleConfig.h"
#include "graphics/WaterSystem.h"
#include "graphics/WaterUnderwaterEffects.h"
#include "image/ImageData.h"

namespace test_water_contract {
using namespace eve::graphics;

TEST_CASE("Water mesh count derives from shape and density and rejects unbounded settings") {
    WaterMeshSettings settings;
    settings.sizeX    = 8;
    settings.sizeZ    = 4;
    settings.densityX = 40;
    settings.densityY = 40;
    const auto plane  = calculateWaterMeshTriangles(settings);
    REQUIRE(static_cast<bool>(plane));
    CHECK_EQ(plane.value(), 64);
    settings.type     = WaterMeshType::Circle;
    settings.sizeZ    = 8;
    const auto circle = calculateWaterMeshTriangles(settings);
    REQUIRE(static_cast<bool>(circle));
    CHECK_EQ(circle.value(), 96);
    settings.sizeX = 0;
    CHECK(!calculateWaterMeshTriangles(settings));
    settings.sizeX = 100000;
    CHECK(!calculateWaterMeshTriangles(settings));
    settings.sizeX = 8;
    settings.type  = static_cast<WaterMeshType>(9);
    CHECK(!calculateWaterMeshTriangles(settings));
}

TEST_CASE("Planar water reflection mirrors camera while retaining per-layer render limits") {
    WaterPlanarReflectionSettings settings;
    settings.textureResolution    = 600;
    settings.resolutionMultiplier = WaterReflectionResolution::Third;
    settings.clipPlaneOffset      = .2f;
    settings.reflectLayers        = 0xffffffffU;
    settings.shadows              = true;
    settings.enableRenderDistance = true;
    settings.customRenderDistance = 275;
    WaterPlanarReflectionInput input;
    input.cameraPosition = {3, 10, -4};
    input.cameraForward  = {0, -.6f, -.8f};
    input.waterPlaneY    = 2;
    input.renderScale    = .8f;
    const auto reflected = buildWaterPlanarReflectionPlan(settings, input);
    REQUIRE(static_cast<bool>(reflected));
    CHECK(reflected.value().shouldRender);
    CHECK_EQ(reflected.value().textureWidth, 158);
    CHECK(std::fabs(reflected.value().cameraPosition.y + 5.6f) < 1e-4f);
    CHECK(std::fabs(reflected.value().cameraForward.y - .6f) < 1e-4f);
    CHECK_EQ(reflected.value().reflectionMatrix[1][1], -1.f);
    CHECK_EQ(reflected.value().layerCullDistances[0], 275.f);
    input.orthographic = true;
    const auto skipped = buildWaterPlanarReflectionPlan(settings, input);
    REQUIRE(static_cast<bool>(skipped));
    CHECK(!skipped.value().shouldRender);
    settings.enablePerLayerDistances  = true;
    settings.customRenderDistances[4] = std::numeric_limits<float>::quiet_NaN();
    CHECK(!buildWaterPlanarReflectionPlan(settings, input));
}

TEST_CASE("Water reflection mask samples rounded image positions and updates enabled state") {
    eve::image::ImageData mask(2, 2, "RGBA8");
    mask.setPixel(0, 0, {.1f, .2f, .3f, .4f});
    mask.setPixel(1, 0, {.6f, .1f, .1f, .1f});
    mask.setPixel(0, 1, {.1f, .7f, .1f, .1f});
    mask.setPixel(1, 1, {.1f, .1f, .8f, .1f});
    WaterReflectionMasker         masker;
    WaterPlanarReflectionSettings settings;
    settings.enabled = false;
    REQUIRE(masker.configure(WaterReflectionMaskChannel::R, .5f, .7f, false).ok());
    REQUIRE(masker.setMask(mask).ok());
    const auto enabled = masker.evaluate(25, 10, 0, 0, 100, 100, settings);
    REQUIRE(enabled.ok());
    CHECK(enabled.value() == WaterReflectionMaskTransition::Enabled);
    CHECK(settings.enabled);
    CHECK_EQ(masker.getSampleX(), 1);
    const auto disabled = masker.evaluate(75, 75, 0, 0, 100, 100, settings);
    REQUIRE(disabled.ok());
    CHECK(disabled.value() == WaterReflectionMaskTransition::Disabled);
    CHECK(!settings.enabled);
    CHECK(!masker.evaluate(0, 0, 0, 0, -1, 1, settings).ok());
}

TEST_CASE("Water style codec round trips authored values and rejects future or unknown fields") {
    WaterStyleConfig style;
    style.deepColor             = {.01f, .08f, .18f};
    style.shallowColor          = {.18f, .72f, .64f};
    style.waveSharpness         = 2.25f;
    style.depthDistance         = 7.5f;
    style.screenSpaceReflection = true;
    const auto json             = style.toJson();
    REQUIRE(static_cast<bool>(json));
    const auto decoded = WaterStyleConfig::fromJson(json.value());
    REQUIRE(static_cast<bool>(decoded));
    CHECK(std::fabs(decoded.value().deepColor.z - .18f) < 1e-4f);
    CHECK(decoded.value().screenSpaceReflection);
    auto value = style.toValue();
    REQUIRE(static_cast<bool>(value));
    auto* root = value.value().getIf<eve::Value::Object>();
    REQUIRE(root != nullptr);
    root->at("schemaVersion") = std::int64_t(WaterStyleConfig::SchemaVersion + 1);
    const auto future         = WaterStyleConfig::fromValue(value.value());
    REQUIRE(!future);
    CHECK(future.error()->code() == eve::DiagnosticCode::UnknownVersion);
    style.depthDistance = 0;
    CHECK(!style.validate());
}

TEST_CASE("Water system refreshes on interval or scene changes and preserves state after invalid time") {
    SUB_CASE("time interval and sea level change publish revisions") {
        WaterSystemState     state;
        WaterSystemSettings  settings;
        WaterSceneConditions scene;
        REQUIRE(static_cast<bool>(initializeWaterSystem(state, settings, scene, 7)));
        const auto first = advanceWaterSystem(state, settings, scene, 4, -3, .25f, 9);
        REQUIRE(static_cast<bool>(first));
        CHECK(!first.value());
        CHECK_EQ(state.positionY, 25.f);
        const auto second = advanceWaterSystem(state, settings, scene, 8, 2, .3f, 9);
        REQUIRE(static_cast<bool>(second));
        CHECK(second.value());
        CHECK_EQ(state.refreshRevision, 1);
        const auto level = updateWaterSeaLevel(state, 31.5f, true);
        REQUIRE(static_cast<bool>(level));
        CHECK(level.value());
        CHECK_EQ(state.positionY, 31.5f);
        CHECK_EQ(state.refreshRevision, 2);
    };
    SUB_CASE("scene change refreshes while negative dt rejects atomically") {
        WaterSystemState    state;
        WaterSystemSettings settings;
        settings.autoUpdateMode        = WaterAutoUpdateMode::SceneConditions;
        settings.ignoreSceneConditions = false;
        WaterSceneConditions scene;
        REQUIRE(static_cast<bool>(initializeWaterSystem(state, settings, scene, 0)));
        auto changed         = scene;
        changed.sunIntensity = 2;
        const auto refreshed = advanceWaterSystem(state, settings, changed, 0, 0, 0, 8);
        REQUIRE(static_cast<bool>(refreshed));
        CHECK(refreshed.value());
        const auto before = state;
        CHECK(!advanceWaterSystem(state, settings, changed, 0, 0, -1, 8));
        CHECK_EQ(state.refreshRevision, before.refreshRevision);
        CHECK_EQ(state.sceneCheckFrames, before.sceneCheckFrames);
    };
}

TEST_CASE("Underwater effects enter and exit around sea level and reject invalid camera height atomically") {
    UnderwaterColorGradient depth, time;
    REQUIRE(static_cast<bool>(depth.addStop(0, .4f, .7f, 1)));
    REQUIRE(static_cast<bool>(depth.addStop(1, .03f, .08f, .18f)));
    REQUIRE(static_cast<bool>(time.addStop(0, .1f, .2f, .4f)));
    REQUIRE(static_cast<bool>(time.addStop(1, .8f, .5f, .2f)));
    UnderwaterScalarCurve exposure;
    REQUIRE(static_cast<bool>(exposure.addKey(0, -.5f)));
    REQUIRE(static_cast<bool>(exposure.addKey(1, .5f)));
    WaterUnderwaterSettings settings;
    settings.causticTextureCount = 16;
    WaterUnderwaterInput input;
    input.seaLevel = 50;
    input.cameraY  = 60;
    WaterUnderwaterState  state;
    WaterUnderwaterOutput output;
    REQUIRE(
        static_cast<bool>(advanceWaterUnderwaterEffects(state, output, settings, input, depth, time, exposure, time)));
    CHECK(!output.isUnderwater);
    input.cameraY      = 25;
    input.causticTicks = 18;
    REQUIRE(
        static_cast<bool>(advanceWaterUnderwaterEffects(state, output, settings, input, depth, time, exposure, time)));
    CHECK(output.entered);
    CHECK(output.isUnderwater);
    CHECK_EQ(output.causticFrame, 2);
    CHECK(std::fabs(output.depth01 - .25f) < 1e-4f);
    input.cameraY = 51;
    REQUIRE(
        static_cast<bool>(advanceWaterUnderwaterEffects(state, output, settings, input, depth, time, exposure, time)));
    CHECK(output.exited);
    CHECK(!output.loopAudio);
    const auto before = state;
    input.cameraY     = std::numeric_limits<float>::quiet_NaN();
    CHECK(!advanceWaterUnderwaterEffects(state, output, settings, input, depth, time, exposure, time));
    CHECK_EQ(state.isUnderwater, before.isUnderwater);
}
}  // namespace test_water_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_targeting)
#include <stdexcept>
#include <type_traits>
#include "PrivateTestScope.h"
#include "common/Capability.h"
#include "common/Identity.h"
#include "physics/TargetingLineOfSightAdapter.h"
#include "physics/World3D.h"
#include "sensing/Targeting.h"

namespace test_targeting_contract {
using namespace eve;
using namespace eve::sensing;
namespace {
SubjectRef subject(const char* text) {
    const auto id = PersistentId::parse(text);
    if (!id) throw std::runtime_error("invalid test subject id");
    return SubjectRef::fromPersistentId(*id);
}
WorldPoint world2D(float x, float y) { return std::move(WorldPoint::world2D(x, y)).expect("valid world point"); }
struct CapabilityScope {
    CapabilityScope() { cap::detail::clearAllRaw(); }
    ~CapabilityScope() { cap::detail::clearAllRaw(); }
};
}  // namespace

TEST_CASE("Targeting types reject mixed identity and coordinate domains before selection") {
    CHECK((!std::is_convertible_v<PersistentId, SubjectRef>));
    CHECK((!std::is_convertible_v<GridPoint, WorldPoint>));
    const auto center = world2D(1, 2);
    const auto circle = WorldArea::circle2D(center, 4);
    REQUIRE(circle.ok());
    CHECK(circle.value().space() == CoordinateSpace::World2D);
    const auto wrong = WorldArea::circle2D(std::move(WorldPoint::world3D(1, 2, 3)).expect("point"), 4);
    CHECK(!wrong.ok());
    CHECK(wrong.code() == StatusCode::Rejected);
    TargetSet set;
    REQUIRE(set.setPoint(center).ok());
    auto box = GridArea::box2D(GridPoint::grid2D(0, 0), GridPoint::grid2D(2, 2));
    REQUIRE(box.ok());
    CHECK(!set.setArea(std::move(box).takeValue()).ok());
}

TEST_CASE("Target resolver filters candidates by space and domain and range and zone") {
    CapabilityScope          scope;
    SensingCandidateProvider provider;
    const auto               origin = subject("00000000-0000-7000-8000-000000000001");
    const auto               enemy  = subject("00000000-0000-7000-8000-000000000002");
    const auto               far    = subject("00000000-0000-7000-8000-000000000003");
    const auto               ally   = subject("00000000-0000-7000-8000-000000000004");
    const auto               zoneId = LogicalId::parse("arena:central");
    REQUIRE(zoneId.has_value());
    const auto zone = ZoneRef::fromLogicalId(*zoneId);
    REQUIRE(zone.has_value());
    REQUIRE(
        provider.upsert(TargetCandidate{enemy, world2D(3, 4), TargetDomain::Enemy, {"unit", "visible"}, {*zone}}).ok());
    REQUIRE(provider.upsert(TargetCandidate{far, world2D(50, 0), TargetDomain::Enemy, {"unit"}, {}}).ok());
    REQUIRE(provider.upsert(TargetCandidate{ally, world2D(2, 0), TargetDomain::Ally, {"unit"}, {}}).ok());
    cap::provide<ISensingCandidateProvider>(&provider);
    TargetingSpec spec;
    spec.space          = CoordinateSpace::World2D;
    spec.domain         = TargetDomain::Enemy;
    spec.minCount       = 1;
    spec.maxCount       = 2;
    spec.maxRange       = 10;
    spec.requiredTags   = {"unit"};
    spec.zone           = *zone;
    spec.worldArea      = std::move(WorldArea::circle2D(world2D(0, 0), 10)).expect("area");
    const auto selected = TargetingResolver{}.resolve({origin, world2D(0, 0), spec});
    REQUIRE(selected.ok());
    REQUIRE_EQ(selected.value().subjects().size(), 1u);
    CHECK(selected.value().subjects()[0] == enemy);
}

TEST_CASE("Target resolver reports absent providers and coordinate mismatch explicitly") {
    CapabilityScope scope;
    TargetingQuery  query{subject("00000000-0000-7000-8000-000000000001"), world2D(0, 0), TargetingSpec{}};
    const auto      absent = TargetingResolver{}.resolve(query);
    REQUIRE(!absent.ok());
    CHECK(absent.code() == StatusCode::Unsupported);
    const auto mismatch = TargetingResolver{}.resolve({query.origin, GridPoint::grid2D(0, 0), query.spec});
    CHECK(!mismatch.ok());
    CHECK(mismatch.code() == StatusCode::Rejected);
    SensingCandidateProvider provider;
    cap::provide<ISensingCandidateProvider>(&provider);
    query.spec.lineOfSight = LineOfSightMode::Required;
    const auto noSight     = TargetingResolver{}.resolve(query);
    CHECK(!noSight.ok());
    CHECK(noSight.code() == StatusCode::Unsupported);
}
}  // namespace test_targeting_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_physics_force_models) || \
    defined(EVE_TEST_ORIGIN_physics_tire_stability)
#include <cmath>
#include <limits>
#include <stdexcept>
#include "physics/forces/ForceModels.h"

namespace test_force_models_contract {
using namespace eve::physics;
namespace {
template <class T>
T value(eve::Result<T> result) {
    if (!result.ok()) throw std::runtime_error(result.status().describe());
    return std::move(result).takeValue();
}
}  // namespace

TEST_CASE("Force models expose mass and region scaling for environmental fields") {
    SUB_CASE("directional field fades at its boundary") {
        DirectionalField field{{{}, 10, 2}, {0, 5, 0}};
        REQUIRE_EQ(value(ForceModels::directional(field, {}, 2)).y, 10);
        CHECK_EQ(value(ForceModels::directional(field, {9, 0, 0}, 2)).y, 5);
        CHECK_EQ(value(ForceModels::directional(field, {10, 0, 0}, 2)).y, 0);
    };
    SUB_CASE("radial force remains finite at the soft core and changes sign for repulsion") {
        RadialField field;
        field.strength = 10;
        CHECK_EQ(value(ForceModels::radial(field, {}, 2)).x, 0);
        const auto attractive = value(ForceModels::radial(field, {2, 0, 0}, 2));
        CHECK(attractive.x < 0);
        field.strength       = -10;
        const auto repulsive = value(ForceModels::radial(field, {0, 0, 2}, 2));
        CHECK(std::fabs(attractive.x + repulsive.z) < 1e-10);
        CHECK(std::isfinite(value(ForceModels::radial(field, {1e-12, 0, 0}, 1)).x));
    };
    SUB_CASE("buoyancy is proportional to displaced volume") {
        BuoyancyModel model;
        model.displacedVolume = .25;
        CHECK(std::fabs(value(ForceModels::buoyancy(model)).y - 2452.5) < 1e-8);
        model.displacedVolume = 0;
        CHECK_EQ(value(ForceModels::buoyancy(model)).y, 0);
    };
}

TEST_CASE("Force models dissipate velocity and respect friction contact limits") {
    SUB_CASE("damping never crosses zero even for a long time step") {
        for (double dt : {.001, .02, 10.}) {
            const auto force = value(ForceModels::damping({10, 2}, {20, -10, 5}, 2, dt));
            REQUIRE(force.x < 0);
            CHECK(force.y > 0);
            CHECK(20 + force.x * dt / 2 >= 0);
            CHECK(-10 + force.y * dt / 2 <= 0);
        }
    };
    SUB_CASE("friction sticks below static threshold and slips at dynamic threshold") {
        FrictionModel friction{.9, .7};
        CHECK_EQ(value(ForceModels::friction(friction, {}, {8, 0, 0}, 10)).x, -8);
        CHECK_EQ(value(ForceModels::friction(friction, {}, {10, 0, 0}, 10)).x, -7);
        CHECK_EQ(value(ForceModels::friction(friction, {3, 4, 0}, {}, 0)).x, 0);
    };
}

TEST_CASE("Tire model couples force budget to wheel torque and handles airborne braking") {
    TireModel model;
    TireInput input;
    input.normalLoad           = 1000;
    input.longitudinalVelocity = 10;
    input.lateralVelocity      = 3;
    input.angularVelocity      = 50;
    const auto driven          = value(ForceModels::tire(model, input, .001));
    REQUIRE(driven.longitudinalForce > 0);
    CHECK(driven.lateralForce < 0);
    CHECK(std::hypot(driven.longitudinalForce, driven.lateralForce) <= 1000.000001);
    CHECK_EQ(driven.reactionTorque, -driven.longitudinalForce * model.radius);
    CHECK(driven.angularVelocity < input.angularVelocity);
    input                 = {};
    input.angularVelocity = 10;
    input.driveTorque     = 5;
    const auto airborne   = value(ForceModels::tire(model, input, .1));
    CHECK_EQ(airborne.longitudinalForce, 0);
    CHECK_EQ(airborne.angularVelocity, 10.5);
    input.brakeTorque = 1e6;
    CHECK_EQ(value(ForceModels::tire(model, input, .1)).angularVelocity, 0);
}

}  // namespace test_force_models_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_physics_jolt)
#include <cmath>
#include <memory>
#include "PrivateTestScope.h"
#include "common/Snapshot.h"
#include "physics/Body3D.h"
#include "physics/Joint3D.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"

namespace test_jolt_world_contract {
using namespace eve::physics;
#if EVENGINE_TEST_HAS_JOLT
TEST_CASE("Jolt world integrates a falling body and invalidates borrowed objects after destruction") {
    auto created = World3D::createWithBackend(RigidBackend3D::Jolt, 0, -9.8f, 0, false);
    REQUIRE(created.ok());
    auto                     world = std::move(created).takeValue();
    std::unique_ptr<Body3D>  ground(world->newBody("static", 0, -.5f, 0));
    std::unique_ptr<Shape3D> floor(ground->newBoxShape(20, 1, 20));
    std::unique_ptr<Body3D>  ballBody(world->newBody("dynamic", 0, 4, 0));
    std::unique_ptr<Shape3D> ball(ballBody->newSphereShape(.5f));
    bool                     contacted = false;
    for (int step = 0; step < 200; ++step) {
        world->update(.01f);
        contacted |= world->getBeginContactCount() > 0;
    }
    CHECK(contacted);
    CHECK(std::fabs(ballBody->getY() - .5f) < .06f);
    const auto ray = world->rayCastOwned(0, 5, 0, 0, -2, 0, {});
    REQUIRE(ray.ok());
    CHECK(ray.value().hit);
    CHECK(ray.value().body == ballBody->runtimeHandle());
    const auto saved = ray.value();
    world->destroy();
    CHECK(!ballBody->isValid());
    CHECK(!ball->isValid());
    CHECK(saved.hit);
}

TEST_CASE("Jolt compound shapes update counts and body destruction invalidates shape handles") {
    auto created = World3D::createWithBackend(RigidBackend3D::Jolt, 0, 0, 0, false);
    REQUIRE(created.ok());
    auto                     world = std::move(created).takeValue();
    std::unique_ptr<Body3D>  body(world->newBody("dynamic", 0, 0, 0));
    std::unique_ptr<Shape3D> first(body->newBoxShape(2, 2, 2));
    std::unique_ptr<Shape3D> second(body->newSphereShape(2));
    CHECK_EQ(world->getShapeCount(), 2);
    second.reset();
    CHECK_EQ(world->getShapeCount(), 1);
    first->setHitEventsEnabled(true);
    CHECK(first->areHitEventsEnabled());
    body.reset();
    CHECK(!first->isValid());
    CHECK_EQ(world->getBodyCount(), 0);
}

TEST_CASE("Rigid backends share sensor filtering without applying collision impulse") {
    for (const auto backend : {RigidBackend3D::Box3D, RigidBackend3D::Jolt}) {
        auto created = World3D::createWithBackend(backend, 0, 0, 0, false);
        REQUIRE(created.ok());
        auto                     world = std::move(created).takeValue();
        std::unique_ptr<Body3D>  fixed(world->newBody("static", 0, 0, 0));
        std::unique_ptr<Shape3D> sensor(fixed->newBoxShape(4, 4, 4));
        sensor->setSensor(true);
        std::unique_ptr<Body3D>  moving(world->newBody("dynamic", 0, 0, 0));
        std::unique_ptr<Shape3D> shape(moving->newSphereShape(.5f));
        shape->setMaskBits(0);
        world->update(.01f);
        CHECK_EQ(world->getBeginTriggerCount(), 0);
        shape->setMaskBits(UINT64_MAX);
        world->update(.01f);
        CHECK(world->getBeginTriggerCount() > 0);
        CHECK(std::fabs(moving->getY()) < .001f);
    }
}


TEST_CASE("Rigid joint limits constrain motion and releasing an endpoint invalidates the joint") {
    SUB_CASE("distance joint keeps a dynamic body near authored length") {
        auto made = World3D::createWithBackend(RigidBackend3D::Jolt, 0, -9.8f, 0, false);
        REQUIRE(made.ok());
        auto                     world = std::move(made).takeValue();
        std::unique_ptr<Body3D>  fixed(world->newBody("static", 0, 0, 0));
        std::unique_ptr<Body3D>  body(world->newBody("dynamic", 0, -2, 0));
        std::unique_ptr<Shape3D> shape(body->newSphereShape(.25f));
        std::unique_ptr<Joint3D> joint(world->newDistanceJoint(fixed.get(), body.get(), 0, 0, 0, 0, -2, 0, 2));
        for (int frame = 0; frame < 100; ++frame) world->update(.01f);
        CHECK(std::fabs(joint->getDistanceCurrentLength() - 2.f) < .03f);
        CHECK_EQ(world->getJointCount(), 1);
        fixed.reset();
        CHECK(!joint->isValid());
        CHECK_EQ(world->getJointCount(), 0);
    };
    SUB_CASE("prismatic motor reaches its limit on both backends") {
        for (const auto backend : {RigidBackend3D::Box3D, RigidBackend3D::Jolt}) {
            auto made = World3D::createWithBackend(backend, 0, -9.8f, 0, false);
            REQUIRE(made.ok());
            auto                     world = std::move(made).takeValue();
            std::unique_ptr<Body3D>  fixed(world->newBody("static", 0, 0, 0));
            std::unique_ptr<Body3D>  body(world->newBody("dynamic", 0, 0, 0));
            std::unique_ptr<Shape3D> shape(body->newBoxShape(1, 1, 1));
            std::unique_ptr<Joint3D> joint(world->newPrismaticJoint(fixed.get(), body.get(), 0, 0, 0, 1, 0, 0));
            joint->setPrismaticLimits(true, -1, 1);
            joint->setPrismaticMotor(true, 2, 100);
            for (int frame = 0; frame < 150; ++frame) world->update(.01f);
            CHECK(body->getX() > .8f);
            CHECK(body->getX() < 1.04f);
            CHECK_THROWS((joint->setPrismaticLimits(true, 2, -2), false));
            world->destroy();
            CHECK(!joint->isValid());
        }
    };
}
#endif
}  // namespace test_jolt_world_contract
#endif
