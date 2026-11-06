#include <cmath>
#include "procgen/MeshBuild.h"
#include "procgen/recursive/RecursiveInstance.h"
#include "procgen/recursive_graphics/FrontierMesh.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::procgen::recursive;
using namespace eve::procgen::recursive_graphics;

TEST_CASE("procgen.recursiveGraphics.meshWindingIdentityAndScaleIndependence") {
    ModelDefinition definition;
    definition.capacity       = 2;
    definition.lengthExponent = 900;
    auto instance             = RecursiveInstance::create(definition, {});
    REQUIRE(instance.ok());
    const std::array<NodeKey, 2> keys{{{3}, {2}}};
    auto                         built = buildFrontierMesh(instance.value(), keys, {});
    REQUIRE(built.ok());
    CHECK_EQ(built.value().lengthExponent, 900);
    CHECK_EQ(built.value().boxNodePaths[0], std::uint64_t{2});
    CHECK_EQ(built.value().boxNodePaths[1], std::uint64_t{3});
    const auto& mesh = *built.value().mesh;
    CHECK_EQ(mesh.getVertexCount(), 48);
    CHECK_EQ(mesh.getIndexCount(), 72);
    for (int triangle = 0; triangle < 24; ++triangle) {
        const int a = mesh.getIndex(triangle * 3), b = mesh.getIndex(triangle * 3 + 1),
                  c     = mesh.getIndex(triangle * 3 + 2);
        const double ux = mesh.getPositionX(b) - mesh.getPositionX(a), uy = mesh.getPositionY(b) - mesh.getPositionY(a),
                     uz = mesh.getPositionZ(b) - mesh.getPositionZ(a);
        const double vx = mesh.getPositionX(c) - mesh.getPositionX(a), vy = mesh.getPositionY(c) - mesh.getPositionY(a),
                     vz = mesh.getPositionZ(c) - mesh.getPositionZ(a);
        CHECK((uy * vz - uz * vy) * mesh.getNormalX(a) + (uz * vx - ux * vz) * mesh.getNormalY(a) +
                  (ux * vy - uy * vx) * mesh.getNormalZ(a) >
              0);
    }
    definition.lengthExponent = -900;
    auto tiny                 = RecursiveInstance::create(definition, {});
    REQUIRE(tiny.ok());
    auto tinyMesh = buildFrontierMesh(tiny.value(), keys, {});
    REQUIRE(tinyMesh.ok());
    CHECK(tinyMesh.value().mesh->positions() == mesh.positions());
}

TEST_CASE("procgen.recursiveGraphics.editsDeletionAndProjectionBudget") {
    ModelDefinition definition;
    definition.capacity = 2;
    auto instance       = RecursiveInstance::create(definition, {});
    REQUIRE(instance.ok());
    const std::array<LeafEdit, 2> edits{{{0, 0, {}, {}}, {1, 1, {10, 0, 0}, {}}}};
    auto                          candidate = instance.value().prepareEdits(edits);
    REQUIRE(candidate.ok());
    auto applied = instance.value().commit(std::move(candidate.value()));
    REQUIRE(applied.ok());
    const std::array<NodeKey, 2> keys{{{2}, {3}}};
    FrontierMeshPolicy           policy;
    policy.origin = {10, 0, 0};
    auto built    = buildFrontierMesh(instance.value(), keys, policy);
    REQUIRE(built.ok());
    CHECK_EQ(built.value().boxNodePaths.size(), std::size_t{1});
    CHECK_EQ(built.value().boxNodePaths[0], std::uint64_t{3});
    CHECK_EQ(built.value().revision.value(), std::uint64_t{2});
    for (float x : built.value().mesh->positions()) CHECK(std::abs(x) <= 1);
    policy.origin             = {0.1, 0, 0};
    policy.maxCoordinateError = 0;
    auto quantized            = buildFrontierMesh(instance.value(), keys, policy);
    CHECK(!quantized.ok());
    policy          = {};
    policy.maxNodes = 1;
    auto budget     = buildFrontierMesh(instance.value(), keys, policy);
    CHECK(!budget.ok());
    const std::array<NodeKey, 2> overlap{{{1}, {2}}};
    auto                         duplicate = buildFrontierMesh(instance.value(), overlap, {});
    CHECK(!duplicate.ok());
    policy               = {};
    policy.scaleExponent = 1800;
    auto overflow        = buildFrontierMesh(instance.value(), keys, policy);
    CHECK(!overflow.ok());
    CHECK_EQ(built.value().mesh->getVertexCount(), 24);
}
