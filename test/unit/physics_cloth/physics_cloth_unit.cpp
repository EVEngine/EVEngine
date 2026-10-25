// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "physics_cloth.cpp",
    "physics_layered_cloth.cpp",
    "physics_layered_cloth_gpu.cpp"
  ]
}
*/
// clang-format on

#include <limits>
#include <type_traits>
#include "LayeredClothGeometryOracle.h"
#include "physics/cloth/LayeredCloth3D.h"
#include "zeroerr/assert.h"

#include "ScriptTest.h"
#include "physics/cloth/Cloth3D.h"
#include "physics/cloth/ClothModel.h"

#include <array>
#include <cmath>

extern const char* physics_cloth_content;
UnitSciptTest(PhysicsClothScriptTest, physics_cloth_content);


TEST_CASE("PhysicsCloth.modelGridCreatesReusableRuntime") {
    auto baked = eve::physics::ClothModel::grid(4, 3, 0.5f, -1.f, 2.f, 3.f);
    REQUIRE(baked.hasValue());
    CHECK(baked.value().schemaVersion() == 1);
    CHECK(baked.value().particles().size() == 12);
    CHECK(baked.value().triangles().size() == 12);
    CHECK(baked.value().gridCols() == 4);
    CHECK(baked.value().particles()[0].inverseMass == 0.f);
    CHECK(baked.value().particles()[4].inverseMass > 0.f);

    eve::physics::Cloth3D first(baked.value());
    eve::physics::Cloth3D second(baked.value());
    first.setParticlePosition(8, 9.f, 8.f, 7.f);
    CHECK(second.getParticleX(8) != 9.f);
    first.reset();
    CHECK(first.getParticleX(8) == -1.f);
    CHECK(first.getParticleY(8) == 2.f);
    CHECK(first.getParticleZ(8) == 4.f);
}

TEST_CASE("PhysicsCloth.modelBakesArbitraryTriangleTopologyTransactionally") {
    const std::array<float, 12> positions     = {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.25f, 1.f, 0.f, 0.f, 1.f};
    const std::array<int, 6>    indices       = {0, 3, 1, 3, 2, 1};
    const std::array<float, 4>  inverseMasses = {0.f, 0.f, 1.f, 1.f};
    auto                        baked = eve::physics::ClothModel::fromTriangles(positions, indices, inverseMasses);
    REQUIRE(baked.hasValue());
    CHECK(baked.value().gridCols() == 0);
    CHECK(baked.value().particles().size() == 4);
    CHECK(baked.value().triangles().size() == 2);
    CHECK(baked.value().foldConstraints().size() == 1);

    eve::physics::Cloth3D cloth(baked.value());
    CHECK(cloth.getParticleCount() == 4);
    CHECK(cloth.isPinned(0));
    CHECK(cloth.isPinned(1));
    cloth.setGravity(0.f, -9.8f, 0.f);
    cloth.update(1.f / 60.f);
    CHECK(cloth.getParticleY(2) < 0.25f);

    const std::array<int, 3> invalidIndices = {0, 1, 9};
    auto                     invalid        = eve::physics::ClothModel::fromTriangles(positions, invalidIndices);
    CHECK(!invalid.hasValue());
}

TEST_CASE("PhysicsCloth.modelJsonRoundTripIsStrictAndVersioned") {
    auto original = eve::physics::ClothModel::grid(3, 3, 0.25f, -2.f, 4.f, 1.f);
    REQUIRE(original.hasValue());
    auto json = original.value().toJson();
    REQUIRE(json.hasValue());
    auto restored = eve::physics::ClothModel::fromJson(json.value());
    REQUIRE(restored.hasValue());
    CHECK(restored.value().gridCols() == 3);
    CHECK(restored.value().gridRows() == 3);
    CHECK(restored.value().particles().size() == original.value().particles().size());
    CHECK(restored.value().distanceConstraints().size() == original.value().distanceConstraints().size());

    eve::Value unknownField = original.value().toValue();
    unknownField.set("futureField", eve::Value(1));
    auto rejectedUnknown = eve::physics::ClothModel::fromValue(unknownField);
    CHECK(!rejectedUnknown.hasValue());
    REQUIRE(rejectedUnknown.error() != nullptr);
    CHECK(rejectedUnknown.error()->code() == eve::DiagnosticCode::ParseError);

    eve::Value futureVersion = original.value().toValue();
    futureVersion.set("schemaVersion", eve::Value(2));
    auto rejectedVersion = eve::physics::ClothModel::fromValue(futureVersion);
    CHECK(!rejectedVersion.hasValue());
    REQUIRE(rejectedVersion.error() != nullptr);
    CHECK(rejectedVersion.error()->code() == eve::DiagnosticCode::UnknownVersion);
}

TEST_CASE("PhysicsCloth.xpbdComplianceProducesTimeStepAwareMaterialResponse") {
    auto model = eve::physics::ClothModel::grid(3, 4, 0.5f, 0.f, 0.f, 0.f);
    REQUIRE(model.hasValue());
    eve::physics::Cloth3D rigid(model.value());
    eve::physics::Cloth3D compliant(model.value());
    for (auto* cloth : {&rigid, &compliant}) {
        cloth->setSelfCollision(false);
        cloth->setFoldStiffness(0.f);
        cloth->setGravity(0.f, -9.8f, 0.f);
        cloth->setIterations(8);
    }
    compliant.setStretchCompliance(0.005f);
    compliant.setShearCompliance(0.005f);
    compliant.setBendCompliance(0.02f);
    for (int frame = 0; frame < 30; ++frame) {
        rigid.update(1.f / 60.f);
        compliant.update(1.f / 60.f);
    }
    CHECK(compliant.getParticleY(10) < rigid.getParticleY(10) - 0.05f);
    CHECK(std::isfinite(compliant.getParticleY(10)));
}

TEST_CASE("PhysicsCloth.modelBuildsGeodesicTethersAndRuntimeLimitsExtension") {
    auto model = eve::physics::ClothModel::grid(3, 4, 0.5f, 0.f, 0.f, 0.f);
    REQUIRE(model.hasValue());
    CHECK(model.value().tetherConstraints().size() == 9);
    eve::physics::Cloth3D cloth(model.value());
    cloth.setGravity(0.f, 0.f, 0.f);
    cloth.setStiffness(0.f);
    cloth.setFoldStiffness(0.f);
    cloth.setSelfCollision(false);
    cloth.setTetherScale(1.f);
    cloth.setTetherCompliance(0.f);
    cloth.setParticlePosition(10, 0.5f, -10.f, 1.5f);
    cloth.update(1.f / 60.f);
    CHECK(cloth.getParticleY(10) > -2.f);
}

TEST_CASE("PhysicsCloth.closedModelPreservesAndInflatesVolume") {
    const std::array<float, 12> positions = {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f};
    const std::array<int, 12>   triangles = {0, 2, 1, 0, 1, 3, 0, 3, 2, 1, 2, 3};
    auto                        model     = eve::physics::ClothModel::fromTriangles(positions, triangles);
    REQUIRE(model.hasValue());
    CHECK(model.value().isClosed());
    CHECK(std::abs(model.value().restVolume() - 1.f / 6.f) < 1e-5f);

    eve::physics::Cloth3D cloth(model.value());
    cloth.setGravity(0.f, 0.f, 0.f);
    cloth.setStiffness(0.f);
    cloth.setFoldStiffness(0.f);
    cloth.setSelfCollision(false);
    cloth.setParticlePosition(3, 0.f, 0.f, 0.25f);
    const float collapsed = cloth.getCurrentVolume();
    cloth.setPressure(1.f);
    cloth.setVolumeCompliance(0.f);
    cloth.update(1.f / 60.f);
    CHECK(std::abs(cloth.getCurrentVolume() - model.value().restVolume()) <
          std::abs(collapsed - model.value().restVolume()));

    const float preserved = cloth.getCurrentVolume();
    cloth.setPressure(1.25f);
    cloth.update(1.f / 60.f);
    CHECK(cloth.getCurrentVolume() > preserved);
}

TEST_CASE("PhysicsCloth.skinBackstopAndAnimatedReferenceConstrainParticles") {
    auto model = eve::physics::ClothModel::grid(2, 2, 0.5f, 0.f, 0.f, 0.f);
    REQUIRE(model.hasValue());
    eve::physics::Cloth3D cloth(model.value());
    cloth.setGravity(0.f, 0.f, 0.f);
    cloth.setStiffness(0.f);
    cloth.setFoldStiffness(0.f);
    cloth.setSelfCollision(false);
    cloth.setSkinConstraint(2, 0.f, 0.f, 0.5f, 0.f, 1.f, 0.f, 0.25f, 0.f, 0.1f, 0.f);
    CHECK(cloth.hasSkinConstraint(2));
    cloth.setParticlePosition(2, 0.f, 0.f, 2.f);
    cloth.update(1.f / 60.f);
    const float dz = cloth.getParticleZ(2) - 0.5f;
    CHECK(std::abs(dz) <= 0.251f);

    cloth.updateSkinReference(2, 1.f, 0.f, 0.5f, 1.f, 0.f, 0.f);
    cloth.setParticlePosition(2, 1.f, 0.f, 0.5f);
    cloth.update(1.f / 60.f);
    CHECK(cloth.getParticleX(2) >= 1.099f);
    cloth.clearSkinConstraint(2);
    CHECK(!cloth.hasSkinConstraint(2));
}

TEST_CASE("PhysicsCloth.attachmentTracksTargetAndDetachesWithVelocity") {
    auto model = eve::physics::ClothModel::grid(2, 2, 0.5f, 0.f, 0.f, 0.f);
    REQUIRE(model.hasValue());
    eve::physics::Cloth3D cloth(model.value());
    cloth.setGravity(0.f, 0.f, 0.f);
    cloth.setStiffness(0.f);
    cloth.setFoldStiffness(0.f);
    cloth.setSelfCollision(false);
    cloth.attachParticle(2, 2.f, 3.f, 4.f, 0.f);
    CHECK(cloth.isAttached(2));
    cloth.update(1.f / 60.f);
    CHECK(std::abs(cloth.getParticleX(2) - 2.f) < 1e-5f);
    CHECK(std::abs(cloth.getParticleY(2) - 3.f) < 1e-5f);
    cloth.updateAttachment(2, 3.f, 4.f, 5.f);
    cloth.update(1.f / 60.f);
    CHECK(std::abs(cloth.getParticleZ(2) - 5.f) < 1e-5f);
    cloth.detachParticle(2);
    CHECK(!cloth.isAttached(2));
}

TEST_CASE("PhysicsCloth.runtimeTearingChangesAuthoritativeConstraintAndSurfaceTopology") {
    auto model = eve::physics::ClothModel::grid(2, 2, 0.5f, 0.f, 0.f, 0.f);
    REQUIRE(model.hasValue());
    eve::physics::Cloth3D cloth(model.value());
    const int             originalLinks = cloth.getDistanceConstraintCount();
    CHECK(cloth.getTriangleCount() == 2);
    cloth.tearConstraint(0, 1);
    CHECK(cloth.getTornConstraintCount() == 1);
    CHECK(cloth.getDistanceConstraintCount() == originalLinks - 1);
    CHECK(cloth.getTriangleCount() == 1);

    eve::physics::Cloth3D automatic(model.value());
    automatic.setGravity(0.f, 0.f, 0.f);
    automatic.setSelfCollision(false);
    automatic.setFoldStiffness(0.f);
    automatic.setTearThreshold(1.2f);
    automatic.setMaxTearsPerStep(1);
    automatic.setParticlePosition(1, 4.f, 0.f, 0.f);
    automatic.update(1.f / 60.f);
    CHECK(automatic.getTornConstraintCount() >= 1);
}

namespace test_layered_cloth_reference {

static_assert(!std::is_copy_constructible_v<eve::physics::LayeredCloth3D>);
static_assert(!std::is_copy_assignable_v<eve::physics::LayeredCloth3D>);

namespace {
using eve::physics::ClothModel;
using eve::physics::LayeredCloth3D;
std::array<ClothModel, 2> sheets(float gap) {
    const std::array<int, 3>   indices = {0, 1, 2};
    const std::array<float, 9> lower   = {0, 0, 0, 1, 0, 0, 0, 0, 1};
    const std::array<float, 9> upper   = {0, gap, 0, 1, gap, 0, 0, gap, 1};
    auto                       a       = ClothModel::fromTriangles(lower, indices);
    auto                       b       = ClothModel::fromTriangles(upper, indices);
    REQUIRE(a.hasValue());
    REQUIRE(b.hasValue());
    return {std::move(a).takeValue(), std::move(b).takeValue()};
}
void configure(LayeredCloth3D& group) {
    group.setSolver(1, 12);
    for (int i = 0; i < group.getLayerCount(); ++i) {
        auto& c = group.layer(i);
        c.setGravity(0, 0, 0);
        c.setStiffness(0);
        c.setFoldStiffness(0);
        c.setSelfCollision(false);
        c.setParticleSize(0.04f);
        c.setCollisionMaterial(0, 0);
        for (int p = 0; p < c.getParticleCount(); ++p) c.setParticleInverseMass(p, 1.f);
    }
}
float meanY(LayeredCloth3D& g, int layer) {
    float sum = 0;
    for (int i = 0; i < 3; ++i) sum += g.layer(layer).getParticleY(i);
    return sum / 3.f;
}
}  // namespace
TEST_CASE("PhysicsLayeredCloth.separatesFacesAndConservesMassWeightedCentre") {
    auto           models = sheets(0.02f);
    LayeredCloth3D group(models);
    configure(group);
    for (int i = 0; i < 3; ++i) group.layer(1).setParticleInverseMass(i, 0.25f);
    group.update(1.f / 120.f);
    CHECK(group.getContactCount() > 0);
    CHECK(std::abs((meanY(group, 0) + 4.f * meanY(group, 1)) / 5.f - 0.016f) < 1e-5f);
    for (int i = 0; i < 3; ++i) CHECK(group.layer(1).getParticleY(i) - group.layer(0).getParticleY(i) > 0.079f);
}
TEST_CASE("PhysicsLayeredCloth.respectsPinnedParticlesAndSymmetricFilter") {
    auto           models = sheets(0.02f);
    LayeredCloth3D group(models);
    configure(group);
    for (int i = 0; i < 3; ++i) group.layer(0).pin(i);
    group.layer(0).setCollisionFilter(1, 2);
    group.layer(1).setCollisionFilter(2, 0);
    group.update(1.f / 120.f);
    CHECK(group.getContactCount() == 0);
    CHECK(std::abs(meanY(group, 1) - 0.02f) < 1e-6f);
    group.layer(1).setCollisionFilter(2, 1);
    group.update(1.f / 120.f);
    CHECK(group.getContactCount() > 0);
    CHECK(meanY(group, 0) == 0.f);
    CHECK(meanY(group, 1) > 0.079f);
}
TEST_CASE("PhysicsLayeredCloth.disabledAndZeroStepDoNotSeparate") {
    auto           models = sheets(0.02f);
    LayeredCloth3D group(models);
    configure(group);
    group.update(0.f);
    CHECK(group.getContactCount() == 0);
    group.setInterCollision(false);
    group.update(1.f / 120.f);
    CHECK(group.getContactCount() == 0);
    CHECK(std::abs(meanY(group, 1) - 0.02f) < 1e-6f);
}
TEST_CASE("PhysicsLayeredCloth.rejectsInvalidInputBeforeMutation") {
    LayeredCloth3D group(3, 3, 3, 0.3f, 0.1f, 0, 2, 0);
    const float    initial  = group.layer(0).getParticleY(4);
    int            rejected = 0;
    for (float dt : {-1.f, 0.1f, std::numeric_limits<float>::quiet_NaN()}) {
        try {
            group.update(dt);
        } catch (const std::exception&) {
            ++rejected;
        }
    }
    REQUIRE(rejected == 3);
    REQUIRE(group.layer(0).getParticleY(4) == initial);
    group.layer(2).destroy();
    try {
        group.update(0.01f);
    } catch (const std::exception&) {
        ++rejected;
    }
    REQUIRE(rejected == 4);
    REQUIRE(group.layer(0).getParticleY(4) == initial);
}
TEST_CASE("PhysicsLayeredCloth.threeLayerReplayRemainsFiniteAndResettable") {
    LayeredCloth3D a(3, 4, 4, 0.25f, 0.08f, 0, 2, 0), b(3, 4, 4, 0.25f, 0.08f, 0, 2, 0);
    a.setWindVelocity(0.5f, 0.2f, 0.1f);
    b.setWindVelocity(0.5f, 0.2f, 0.1f);
    for (int frame = 0; frame < 240; ++frame) {
        a.update(1.f / 120.f);
        b.update(1.f / 120.f);
    }
    for (int l = 0; l < 3; ++l)
        for (int p = 0; p < 16; ++p) {
            REQUIRE(std::isfinite(a.layer(l).getParticleY(p)));
            REQUIRE(std::abs(a.layer(l).getParticleY(p) - b.layer(l).getParticleY(p)) < 1e-5f);
            REQUIRE(std::abs(a.layer(l).getParticleX(p) - b.layer(l).getParticleX(p)) < 1e-5f);
            REQUIRE(std::abs(a.layer(l).getParticleZ(p) - b.layer(l).getParticleZ(p)) < 1e-5f);
        }
    a.reset();
    REQUIRE(a.getContactCount() == 0);
    REQUIRE(std::abs(a.layer(2).getParticleY(8) - 2.16f) < 1e-5f);
    REQUIRE(a.layer(0).isPinned(0));
}

TEST_CASE("PhysicsLayeredCloth.interiorFaceContactAndFriction") {
    const std::array<int, 3>   indices = {0, 1, 2};
    const std::array<float, 9> floor   = {-3, 0, -3, 3, 0, -3, 0, 0, 3};
    const std::array<float, 9> patch   = {-0.2f, 0.02f, -0.2f, 0.2f, 0.02f, -0.2f, 0, 0.02f, 0.2f};
    auto                       base    = ClothModel::fromTriangles(floor, indices);
    auto                       top     = ClothModel::fromTriangles(patch, indices);
    REQUIRE(base.hasValue());
    REQUIRE(top.hasValue());
    const std::array<ClothModel, 2> models = {base.value(), top.value()};
    LayeredCloth3D                  smooth(models), rough(models);
    configure(smooth);
    configure(rough);
    for (LayeredCloth3D* group : {&smooth, &rough}) {
        for (int i = 0; i < 3; ++i) group->layer(0).pin(i);
        group->layer(1).applyForce(100.f, 0, 0);
    }
    rough.layer(0).setCollisionMaterial(1, 0);
    rough.layer(1).setCollisionMaterial(1, 0);
    smooth.update(0.01f);
    rough.update(0.01f);
    CHECK(meanY(smooth, 1) > 0.079f);
    CHECK(meanY(rough, 1) > 0.079f);
    smooth.update(0.01f);
    rough.update(0.01f);
    CHECK(rough.layer(1).getParticleX(0) < smooth.layer(1).getParticleX(0) - 0.001f);
}

TEST_CASE("PhysicsLayeredCloth.coincidentAndFullyPinnedContactsStayFinite") {
    auto           models = sheets(0.f);
    LayeredCloth3D group(models);
    configure(group);
    group.update(0.01f);
    CHECK(group.getContactCount() > 0);
    for (int l = 0; l < 2; ++l)
        for (int i = 0; i < 3; ++i) {
            CHECK(std::isfinite(group.layer(l).getParticleY(i)));
            group.layer(l).pin(i);
        }
    const float initial = meanY(group, 0);
    group.update(0.01f);
    CHECK(group.getContactCount() == 0);
    CHECK(meanY(group, 0) == initial);
}

}  // namespace test_layered_cloth_reference

namespace test_layered_cloth_geometry {

using namespace cloth_test_geometry;
TEST_CASE("PhysicsLayeredClothGPU.geometricDistanceOracleHandlesCrossingsAndDegeneracy") {
    const Triangle flat     = {glm::dvec3(-1, 0, -1), glm::dvec3(1, 0, -1), glm::dvec3(0, 0, 1)};
    Triangle       parallel = flat;
    for (auto& point : parallel) point.y += 0.08;
    REQUIRE(std::abs(faceDistance(flat, parallel) - 0.08) < 1e-12);
    const Triangle crossing = {glm::dvec3(0, -1, 0), glm::dvec3(0, 1, 0), glm::dvec3(2, 0, 0)};
    REQUIRE(faceDistance(flat, crossing) < 1e-12);
    const Triangle collapsed = {glm::dvec3(0, 0.3, 0), glm::dvec3(0, 0.3, 0), glm::dvec3(0, 0.3, 0)};
    REQUIRE(std::abs(faceDistance(flat, collapsed) - 0.3) < 1e-12);
    const Triangle collinear = {glm::dvec3(-2, 0, 0), glm::dvec3(2, 0, 0), glm::dvec3(0, 0, 0)};
    REQUIRE(faceDistance(flat, collinear) < 1e-12);
}

}  // namespace test_layered_cloth_geometry
