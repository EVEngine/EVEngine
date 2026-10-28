// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "modules": [
    "procgen",
    "procgen_editing"
  ],
  "origins": [
    "procgen_road_junction_style.cpp"
  ],
  "module": "procgen"
}
*/
// clang-format on

#include "editing/EditingAuthority.h"
#include "procgen/editing/RoadNetworkEditTarget.h"
#include "procgen/road/RoadBake.h"
#include "procgen/road/RoadGeometrySources.h"
#include "procgen/road/RoadNetworkCodec.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

using namespace eve::procgen;
using namespace eve::procgen::road;

namespace {
template <class T>
T checked(eve::Result<T> result) {
    if (!result.ok()) throw std::runtime_error(result.status().describe());
    return std::move(result).takeValue();
}
RoadNetwork cross(RoadStyle style, bool varied = false) {
    RoadNetwork network;
    const auto  hub = checked(network.addNode(0, 0, 0, 6));
    for (int i = 0; i < 4; ++i) {
        const float x   = i == 0 ? 20.f : i == 1 ? -20.f : 0.f;
        const float z   = i == 2 ? 20.f : i == 3 ? -20.f : 0.f;
        const auto  end = checked(network.addNode(x, 0, z));
        auto        arm = style;
        if (varied && i == 0) {
            arm.curbWidth  = .2f;
            arm.curbHeight = .4f;
        }
        (void)checked(network.addEdge(hub, end, {{0, 0, 0}, {x, 0, z}}, 1, 1, arm));
    }
    return network;
}
RoadBakeResult bake(const RoadNetwork& network) {
    RoadBakeOptions options;
    options.includeMarkings = options.includeNavigation = options.includePiers = options.includePlacements = false;
    return checked(bakeRoadNetwork(network, options));
}
int triangles(const MeshBuild& mesh, const std::string& group, bool junctionOnly = false) {
    auto sources = checked(PreparedRoadGeometrySources::prepare(mesh));
    int  count   = 0;
    for (int t = 0; t < mesh.getIndexCount() / 3; ++t)
        if (mesh.getGroupName(mesh.getTriangleGroup(t)) == group) {
            if (!junctionOnly || std::any_of(sources.spans().begin(), sources.spans().end(), [&](const auto& span) {
                    return span.kind == RoadGeometrySourceKind::Junction && t >= static_cast<int>(span.firstTriangle) &&
                           t < static_cast<int>(span.firstTriangle + span.triangleCount);
                }))
                ++count;
        }
    return count;
}
}  // namespace

TEST_CASE("procgen.road.junctionStyle.saveRestoreAndUndoReproduceGeometry") {
    RoadStyle style;
    style.laneWidth                                     = 3;
    style.automaticStructures                           = false;
    auto                                        network = cross(style);
    const auto                                  before  = bake(network);
    eve::procgen_editing::RoadNetworkEditTarget target("junction-style", network);
    eve::editing::LocalWorldAuthority           authority(&target);
    std::vector<eve::editing::DomainOperation>  operations;
    for (const auto& edge : network.edges()) {
        auto bare         = edge.style;
        bare.crossSection = {{-3, 0, RoadMaterial::Asphalt}, {3, 0, RoadMaterial::Asphalt}};
        auto operation    = target.makeSetEdgeStyle(edge.id, bare);
        REQUIRE(operation.ok());
        operations.push_back(std::move(operation).takeValue());
    }
    eve::editing::TransactionSpec spec;
    spec.id           = eve::editing::TransactionId("junction-style-bare");
    spec.label        = "Remove junction bands";
    spec.target       = target.targetId();
    spec.baseRevision = target.revision();
    auto plan         = authority.preflight(spec, operations);
    REQUIRE(plan.ok());
    auto committed = authority.commit(plan.value());
    REQUIRE(committed.ok());
    const auto changed = bake(network);
    REQUIRE_EQ(triangles(changed.mesh, "curb"), 0);
    REQUIRE_EQ(triangles(changed.mesh, "sidewalk"), 0);
    const auto restored = checked(restoreRoadNetwork(checked(captureRoadNetwork(network))));
    const auto reloaded = bake(restored);
    REQUIRE(reloaded.mesh.positions() == changed.mesh.positions());
    REQUIRE(reloaded.mesh.indices() == changed.mesh.indices());
    REQUIRE(authority.compensate(committed.value()).ok());
    const auto undone = bake(network);
    REQUIRE(undone.mesh.positions() == before.mesh.positions());
    REQUIRE(undone.mesh.indices() == before.mesh.indices());
    REQUIRE_GT(triangles(undone.mesh, "curb"), 0);
}
