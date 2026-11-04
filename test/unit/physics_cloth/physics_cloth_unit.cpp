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

#include "LayeredClothGeometryOracle.h"
#include "ScriptTest.h"
#include "physics/cloth/Cloth3D.h"
#include "physics/cloth/ClothModel.h"
#include "physics/cloth/LayeredCloth3D.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <array>
#include <cmath>
#include <exception>
#include <limits>
#include <utility>

extern const char* physics_cloth_content;
UnitSciptTest(PhysicsClothScriptTest, physics_cloth_content);

namespace {
using eve::physics::Cloth3D;
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

void isolateInterlayerContact(LayeredCloth3D& group) {
    group.setSolver(1, 12);
    for (int layer = 0; layer < group.getLayerCount(); ++layer) {
        auto& cloth = group.layer(layer);
        cloth.setGravity(0, 0, 0);
        cloth.setStiffness(0);
        cloth.setFoldStiffness(0);
        cloth.setSelfCollision(false);
        cloth.setParticleSize(0.04f);
        cloth.setCollisionMaterial(0, 0);
        for (int p = 0; p < cloth.getParticleCount(); ++p) cloth.setParticleInverseMass(p, 1.f);
    }
}

float meanY(LayeredCloth3D& group, int layer) {
    float sum = 0.f;
    for (int p = 0; p < 3; ++p) sum += group.layer(layer).getParticleY(p);
    return sum / 3.f;
}
}  // namespace

TEST_CASE("ClothModel validation serialization and reuse contract") {
    SUB_CASE("ClothModel grid output has fixed top row and reusable rest state") {
        auto model = ClothModel::grid(4, 3, 0.5f, -1.f, 2.f, 3.f);
        REQUIRE(model.hasValue());
        CHECK_EQ(model.value().schemaVersion(), 1u);
        CHECK_EQ(model.value().gridCols(), 4);
        CHECK_EQ(model.value().gridRows(), 3);
        CHECK_EQ(model.value().particles().size(), 12u);
        CHECK_EQ(model.value().triangles().size(), 12u);
        CHECK_EQ(model.value().particles()[0].inverseMass, 0.f);
        CHECK(model.value().particles()[4].inverseMass > 0.f);
        Cloth3D first(model.value()), second(model.value());
        first.setParticlePosition(8, 9.f, 8.f, 7.f);
        CHECK(second.getParticleX(8) != 9.f);
        first.reset();
        CHECK_EQ(first.getParticleX(8), -1.f);
        CHECK_EQ(first.getParticleY(8), 2.f);
        CHECK_EQ(first.getParticleZ(8), 4.f);
    };

    SUB_CASE("ClothModel rejects malformed dimensions and topology before publishing") {
        CHECK(!ClothModel::grid(1, 3, 0.5f, 0, 0, 0).hasValue());
        CHECK(!ClothModel::grid(3, 1, 0.5f, 0, 0, 0).hasValue());
        CHECK(!ClothModel::grid(3, 3, 0.f, 0, 0, 0).hasValue());
        const std::array<float, 9> positions = {0, 0, 0, 1, 0, 0, 0, 0, 1};
        const std::array<int, 3>   invalid   = {0, 1, 3};
        CHECK(!ClothModel::fromTriangles(positions, invalid).hasValue());
        const std::array<float, 2> malformed = {0, 0};
        const std::array<int, 3>   valid     = {0, 1, 2};
        CHECK(!ClothModel::fromTriangles(malformed, valid).hasValue());
    };

    SUB_CASE("ClothModel arbitrary triangles preserve pins and bake shared-edge folds") {
        const std::array<float, 12> positions = {0, 0, 0, 1, 0, 0, 1, 0.25f, 1, 0, 0, 1};
        const std::array<int, 6>    indices   = {0, 3, 1, 3, 2, 1};
        const std::array<float, 4>  masses    = {0, 0, 1, 1};
        auto                        model     = ClothModel::fromTriangles(positions, indices, masses);
        REQUIRE(model.hasValue());
        CHECK_EQ(model.value().gridCols(), 0);
        CHECK_EQ(model.value().triangles().size(), 2u);
        CHECK_EQ(model.value().foldConstraints().size(), 1u);
        Cloth3D cloth(model.value());
        CHECK(cloth.isPinned(0));
        CHECK(cloth.isPinned(1));
        cloth.setGravity(0, -9.8f, 0);
        cloth.update(1.f / 60.f);
        CHECK(cloth.getParticleY(2) < 0.25f);
    };

    SUB_CASE("ClothModel JSON round trip is canonical and rejects unknown versions and fields") {
        auto model = ClothModel::grid(3, 3, 0.25f, -2, 4, 1);
        REQUIRE(model.hasValue());
        auto json = model.value().toJson();
        REQUIRE(json.hasValue());
        auto restored = ClothModel::fromJson(json.value());
        REQUIRE(restored.hasValue());
        CHECK_EQ(restored.value().gridCols(), 3);
        CHECK_EQ(restored.value().particles().size(), model.value().particles().size());
        CHECK_EQ(restored.value().distanceConstraints().size(), model.value().distanceConstraints().size());
        auto unknown = model.value().toValue();
        unknown.set("futureField", eve::Value(1));
        auto rejected = ClothModel::fromValue(unknown);
        CHECK(!rejected.hasValue());
        REQUIRE(rejected.error() != nullptr);
        CHECK(rejected.error()->code() == eve::DiagnosticCode::ParseError);
        auto future = model.value().toValue();
        future.set("schemaVersion", eve::Value(2));
        rejected = ClothModel::fromValue(future);
        CHECK(!rejected.hasValue());
        REQUIRE(rejected.error() != nullptr);
        CHECK(rejected.error()->code() == eve::DiagnosticCode::UnknownVersion);
    };
}

TEST_CASE("Cloth3D runtime constraint contract") {
    SUB_CASE("Cloth3D compliance permits greater gravity stretch than rigid distance constraints") {
        auto model = ClothModel::grid(3, 4, 0.5f, 0, 0, 0);
        REQUIRE(model.hasValue());
        Cloth3D rigid(model.value()), compliant(model.value());
        for (Cloth3D* cloth : {&rigid, &compliant}) {
            cloth->setSelfCollision(false);
            cloth->setFoldStiffness(0);
            cloth->setGravity(0, -9.8f, 0);
            cloth->setIterations(8);
        }
        compliant.setStretchCompliance(0.005f);
        compliant.setShearCompliance(0.005f);
        compliant.setBendCompliance(0.02f);
        // Compare settled stretch, after the initial swinging/drooping transient.
        for (int frame = 0; frame < 120; ++frame) {
            rigid.update(1.f / 60.f);
            compliant.update(1.f / 60.f);
        }
        CHECK(compliant.getParticleY(10) < rigid.getParticleY(10) - 0.05f);
        CHECK(std::isfinite(compliant.getParticleY(10)));
    };

    SUB_CASE("Cloth3D tethers bound an otherwise unconstrained displacement") {
        auto model = ClothModel::grid(3, 4, 0.5f, 0, 0, 0);
        REQUIRE(model.hasValue());
        CHECK_EQ(model.value().tetherConstraints().size(), 9u);
        Cloth3D cloth(model.value());
        cloth.setGravity(0, 0, 0);
        cloth.setStiffness(0);
        cloth.setFoldStiffness(0);
        cloth.setSelfCollision(false);
        cloth.setTetherScale(1.f);
        cloth.setTetherCompliance(0);
        cloth.setParticlePosition(10, 0.5f, -10.f, 1.5f);
        cloth.update(1.f / 60.f);
        CHECK(cloth.getParticleY(10) > -2.f);
    };

    SUB_CASE("Cloth3D closed volume responds to pressure while open surfaces report zero") {
        const std::array<float, 12> positions = {0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1};
        const std::array<int, 12>   faces     = {0, 2, 1, 0, 1, 3, 0, 3, 2, 1, 2, 3};
        auto                        model     = ClothModel::fromTriangles(positions, faces);
        REQUIRE(model.hasValue());
        CHECK(model.value().isClosed());
        CHECK(std::abs(model.value().restVolume() - 1.f / 6.f) < 1e-5f);
        Cloth3D cloth(model.value());
        cloth.setGravity(0, 0, 0);
        cloth.setStiffness(0);
        cloth.setFoldStiffness(0);
        cloth.setSelfCollision(false);
        cloth.setParticlePosition(3, 0, 0, 0.25f);
        const float collapsed = cloth.getCurrentVolume();
        cloth.setPressure(1.f);
        cloth.setVolumeCompliance(0);
        cloth.update(1.f / 60.f);
        CHECK(std::abs(cloth.getCurrentVolume() - model.value().restVolume()) <
              std::abs(collapsed - model.value().restVolume()));
        const float beforeInflation = cloth.getCurrentVolume();
        cloth.setPressure(1.25f);
        cloth.update(1.f / 60.f);
        CHECK(cloth.getCurrentVolume() > beforeInflation);
    };

    SUB_CASE("Cloth3D skin reference and attachment follow their explicit targets") {
        auto model = ClothModel::grid(2, 2, 0.5f, 0, 0, 0);
        REQUIRE(model.hasValue());
        Cloth3D cloth(model.value());
        cloth.setGravity(0, 0, 0);
        cloth.setStiffness(0);
        cloth.setFoldStiffness(0);
        cloth.setSelfCollision(false);
        cloth.setSkinConstraint(2, 0, 0, 0.5f, 0, 1, 0, 0.25f, 0, 0.1f, 0);
        CHECK(cloth.hasSkinConstraint(2));
        cloth.setParticlePosition(2, 0, 0, 2);
        cloth.update(1.f / 60.f);
        CHECK(std::abs(cloth.getParticleZ(2) - 0.5f) <= 0.251f);
        cloth.clearSkinConstraint(2);
        CHECK(!cloth.hasSkinConstraint(2));
        cloth.attachParticle(2, 2, 3, 4, 0);
        cloth.update(1.f / 60.f);
        CHECK(cloth.isAttached(2));
        CHECK(std::abs(cloth.getParticleX(2) - 2.f) < 1e-5f);
        cloth.updateAttachment(2, 3, 4, 5);
        cloth.update(1.f / 60.f);
        CHECK(std::abs(cloth.getParticleZ(2) - 5.f) < 1e-5f);
        cloth.detachParticle(2);
        CHECK(!cloth.isAttached(2));
    };

    SUB_CASE("Cloth3D tearing removes a runtime constraint and its affected face") {
        auto model = ClothModel::grid(2, 2, 0.5f, 0, 0, 0);
        REQUIRE(model.hasValue());
        Cloth3D   cloth(model.value());
        const int links = cloth.getDistanceConstraintCount();
        CHECK_EQ(cloth.getTriangleCount(), 2);
        cloth.tearConstraint(0, 1);
        CHECK_EQ(cloth.getTornConstraintCount(), 1);
        CHECK_EQ(cloth.getDistanceConstraintCount(), links - 1);
        CHECK_EQ(cloth.getTriangleCount(), 1);
    };
}

TEST_CASE("LayeredCloth contact and replay contract") {
    SUB_CASE("LayeredCloth rejects invalid steps without mutating unaffected layers") {
        LayeredCloth3D group(3, 3, 3, 0.3f, 0.1f, 0, 2, 0);
        const float    initial = group.layer(0).getParticleY(4);
        for (float dt : {-1.f, 0.1f, std::numeric_limits<float>::quiet_NaN()}) {
            bool rejected = false;
            try {
                group.update(dt);
            } catch (const std::exception&) {
                rejected = true;
            }
            CHECK(rejected);
            CHECK_EQ(group.layer(0).getParticleY(4), initial);
        }
        group.layer(2).destroy();
        bool rejected = false;
        try {
            group.update(0.01f);
        } catch (const std::exception&) {
            rejected = true;
        }
        CHECK(rejected);
        CHECK_EQ(group.layer(0).getParticleY(4), initial);
    };

    SUB_CASE("LayeredCloth separates penetrating faces using inverse mass weighting") {
        auto           models = sheets(0.02f);
        LayeredCloth3D group(models);
        isolateInterlayerContact(group);
        for (int i = 0; i < 3; ++i) group.layer(1).setParticleInverseMass(i, 0.25f);
        group.update(1.f / 120.f);
        CHECK(group.getContactCount() > 0);
        CHECK(std::abs((meanY(group, 0) + 4.f * meanY(group, 1)) / 5.f - 0.016f) < 1e-5f);
        CHECK(meanY(group, 1) - meanY(group, 0) > 0.079f);
    };

    SUB_CASE("LayeredCloth contact requires symmetric filters and respects pinned sheet") {
        auto           models = sheets(0.02f);
        LayeredCloth3D group(models);
        isolateInterlayerContact(group);
        for (int p = 0; p < 3; ++p) group.layer(0).pin(p);
        group.layer(0).setCollisionFilter(1, 2);
        group.layer(1).setCollisionFilter(2, 0);
        group.update(1.f / 120.f);
        CHECK_EQ(group.getContactCount(), 0);
        CHECK(std::abs(meanY(group, 1) - 0.02f) < 1e-6f);
        group.layer(1).setCollisionFilter(2, 1);
        group.update(1.f / 120.f);
        CHECK(group.getContactCount() > 0);
        CHECK_EQ(meanY(group, 0), 0.f);
        CHECK(meanY(group, 1) > 0.079f);
    };

    SUB_CASE("LayeredCloth zero step and disabled contact preserve separation") {
        auto           models = sheets(0.02f);
        LayeredCloth3D group(models);
        isolateInterlayerContact(group);
        group.update(0);
        CHECK_EQ(group.getContactCount(), 0);
        group.setInterCollision(false);
        group.update(1.f / 120.f);
        CHECK_EQ(group.getContactCount(), 0);
        CHECK(std::abs(meanY(group, 1) - 0.02f) < 1e-6f);
    };

    SUB_CASE("LayeredCloth identical wind replay is finite and reset restores authored offsets") {
        // Two small sheets exercise the same pairwise contact and deterministic replay
        // path without spending the process budget on dense face-pair searches.
        LayeredCloth3D a(2, 2, 2, 0.25f, 0.08f, 0, 2, 0), b(2, 2, 2, 0.25f, 0.08f, 0, 2, 0);
        a.setSolver(1, 3);
        b.setSolver(1, 3);
        a.setWindVelocity(0.5f, 0.2f, 0.1f);
        b.setWindVelocity(0.5f, 0.2f, 0.1f);
        for (int frame = 0; frame < 32; ++frame) {
            a.update(1.f / 120.f);
            b.update(1.f / 120.f);
        }
        for (int layer = 0; layer < 2; ++layer)
            for (int p = 0; p < 4; ++p) {
                CHECK(std::isfinite(a.layer(layer).getParticleY(p)));
                CHECK(std::abs(a.layer(layer).getParticleY(p) - b.layer(layer).getParticleY(p)) < 1e-5f);
            }
        a.reset();
        CHECK_EQ(a.getContactCount(), 0);
        CHECK(std::abs(a.layer(1).getParticleY(2) - 2.08f) < 1e-5f);
        CHECK(a.layer(0).isPinned(0));
    };
}

TEST_CASE("LayeredCloth geometric oracle contract") {
    SUB_CASE("LayeredCloth geometric distance oracle covers separated crossing and degenerate faces") {
        using namespace cloth_test_geometry;
        const Triangle flat     = {glm::dvec3(-1, 0, -1), glm::dvec3(1, 0, -1), glm::dvec3(0, 0, 1)};
        Triangle       parallel = flat;
        for (auto& point : parallel) point.y += 0.08;
        REQUIRE(std::abs(faceDistance(flat, parallel) - 0.08) < 1e-12);
        const Triangle crossing = {glm::dvec3(0, -1, 0), glm::dvec3(0, 1, 0), glm::dvec3(2, 0, 0)};
        CHECK(faceDistance(flat, crossing) < 1e-12);
        const Triangle collapsed = {glm::dvec3(0, 0.3, 0), glm::dvec3(0, 0.3, 0), glm::dvec3(0, 0.3, 0)};
        CHECK(std::abs(faceDistance(flat, collapsed) - 0.3) < 1e-12);
    };
}
