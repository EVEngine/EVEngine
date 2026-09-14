#include "ScriptTest.h"
#include "physics/cloth/Cloth3D.h"
#include "physics/cloth/ClothModel.h"

#include <array>
#include <cmath>

extern const char* physics_cloth_content;
UnitSciptTest(PhysicsClothScriptTest, physics_cloth_content);

TEST_CASE_FIXTURE(PhysicsClothScriptTest, "PhysicsCloth.scriptBindings") {
    CHECK(vm.callFunc(vm.findFunc("basic"), vm).toBool());
}

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
    const int originalLinks = cloth.getDistanceConstraintCount();
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
