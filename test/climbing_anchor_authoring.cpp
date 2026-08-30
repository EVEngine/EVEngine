#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "climbing/ClimbingAnchorAuthoring.h"

#include <algorithm>
#include <string>
#include <utility>

namespace {

eve::climbing::ClimbingAnchorBakeRequest request() {
    eve::climbing::ClimbingAnchorBakeRequest value;
    value.graphId                  = "playground.route";
    value.sourceGeometryContentId  = "sha256:playground-geometry";
    value.settings.handSpacing     = 0.4f;
    value.settings.hangingFeetDrop = 1.4f;
    eve::climbing::ClimbingLedgeBakeSource ledge;
    ledge.id           = "wall";
    ledge.points       = {{0.f, 2.f, 0.f}, {1.f, 2.f, 0.f}, {1.f, 2.f, 1.f}};
    ledge.localNormals = {{0.f, 0.f, -1.f}, {-1.f, 0.f, 0.f}, {-1.f, 0.f, 0.f}};
    ledge.tags         = {"stone"};
    value.ledges.push_back(std::move(ledge));
    value.ladders.push_back({"ladder", {3.f, 0.3f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 0.f, -1.f}, 0.3f, 0.5f, 4, {"metal"}});
    return value;
}

}  // namespace

TEST_CASE("climbing.anchorAuthoring.bakeIsCanonicalAndExecutable") {
    auto first  = eve::climbing::bakeClimbingAnchorGraph(request());
    auto second = eve::climbing::bakeClimbingAnchorGraph(request());
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    CHECK_EQ(first.value().ledgeNodeCount, std::uint32_t(3));
    CHECK_EQ(first.value().ladderNodeCount, std::uint32_t(4));
    CHECK_EQ(first.value().graph.buildSettingsHash, second.value().graph.buildSettingsHash);

    auto firstEncoded  = eve::climbing::encodeClimbingAnchorGraphDefinition(first.value().graph);
    auto secondEncoded = eve::climbing::encodeClimbingAnchorGraphDefinition(second.value().graph);
    REQUIRE(firstEncoded.ok());
    REQUIRE(secondEncoded.ok());
    CHECK(firstEncoded.value() == secondEncoded.value());

    bool hasCorner   = false;
    bool hasMount    = false;
    bool hasClimb    = false;
    bool hasDismount = false;
    for (const auto& node : first.value().graph.nodes)
        hasCorner = hasCorner || node.kind == eve::climbing::ClimbingAnchorKind::CornerOuter ||
                    node.kind == eve::climbing::ClimbingAnchorKind::CornerInner;
    for (const auto& edge : first.value().graph.edges) {
        hasMount    = hasMount || edge.kind == eve::climbing::ClimbingAnchorEdgeKind::Mount;
        hasClimb    = hasClimb || edge.kind == eve::climbing::ClimbingAnchorEdgeKind::Climb;
        hasDismount = hasDismount || edge.kind == eve::climbing::ClimbingAnchorEdgeKind::Dismount;
    }
    CHECK(hasCorner);
    CHECK(hasMount);
    CHECK(hasClimb);
    CHECK(hasDismount);
}

TEST_CASE("climbing.anchorAuthoring.hashIgnoresSourceAndTagOrdering") {
    auto                                   canonical = request();
    eve::climbing::ClimbingLedgeBakeSource secondLedge;
    secondLedge.id          = "balcony";
    secondLedge.points      = {{4.f, 1.f, 0.f}, {5.f, 1.f, 0.f}};
    secondLedge.localNormal = {0.f, 0.f, -1.f};
    secondLedge.tags        = {"outdoor", "stone"};
    canonical.ledges.push_back(secondLedge);
    canonical.ladders.front().tags = {"metal", "outdoor"};

    auto permuted = canonical;
    std::reverse(permuted.ledges.begin(), permuted.ledges.end());
    std::reverse(permuted.ledges.front().tags.begin(), permuted.ledges.front().tags.end());
    std::reverse(permuted.ledges.back().tags.begin(), permuted.ledges.back().tags.end());
    std::reverse(permuted.ladders.front().tags.begin(), permuted.ladders.front().tags.end());

    auto first  = eve::climbing::bakeClimbingAnchorGraph(canonical);
    auto second = eve::climbing::bakeClimbingAnchorGraph(permuted);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    CHECK_EQ(first.value().graph.buildSettingsHash, second.value().graph.buildSettingsHash);
    auto firstEncoded  = eve::climbing::encodeClimbingAnchorGraphDefinition(first.value().graph);
    auto secondEncoded = eve::climbing::encodeClimbingAnchorGraphDefinition(second.value().graph);
    REQUIRE(firstEncoded.ok());
    REQUIRE(secondEncoded.ok());
    CHECK(firstEncoded.value() == secondEncoded.value());
}

TEST_CASE("climbing.anchorAuthoring.requestCodecRoundTripsAndRejectsFuture") {
    auto encoded = eve::climbing::encodeClimbingAnchorBakeRequest(request());
    REQUIRE(encoded.ok());
    auto decoded = eve::climbing::decodeClimbingAnchorBakeRequest(encoded.value());
    REQUIRE(decoded.ok());
    auto reencoded = eve::climbing::encodeClimbingAnchorBakeRequest(decoded.value());
    REQUIRE(reencoded.ok());
    CHECK(reencoded.value() == encoded.value());

    eve::Value future = encoded.value();
    future.set("schemaVersion", eve::Value(eve::climbing::ClimbingAnchorBakeRequest::SchemaVersion + 1));
    auto rejected = eve::climbing::decodeClimbingAnchorBakeRequest(future);
    CHECK(!rejected.ok());
    CHECK_EQ(static_cast<int>(rejected.error()->code()), static_cast<int>(eve::DiagnosticCode::UnknownVersion));
}

TEST_CASE("climbing.anchorAuthoring.overlayOwnsCanonicalPrimitives") {
    auto baked = eve::climbing::bakeClimbingAnchorGraph(request());
    REQUIRE(baked.ok());
    auto overlay = eve::climbing::inspectClimbingAnchorGraphAuthoring(baked.value().graph);
    REQUIRE(overlay.ok());
    CHECK_EQ(overlay.value().graphId, std::string("playground.route"));
    CHECK_EQ(overlay.value().nodes.size(), baked.value().graph.nodes.size());
    CHECK_EQ(overlay.value().edges.size(), baked.value().graph.edges.size());
    CHECK_EQ(overlay.value().nodes.front().id, baked.value().graph.nodes.front().id);

    auto invalid                      = baked.value().graph;
    invalid.nodes.front().localNormal = {};
    auto rejected                     = eve::climbing::inspectClimbingAnchorGraphAuthoring(invalid);
    CHECK(!rejected.ok());
    CHECK_EQ(static_cast<int>(rejected.error()->code()), static_cast<int>(eve::DiagnosticCode::InvalidArgument));
}

TEST_CASE("climbing.anchorAuthoring.rejectsInvalidSourceWithoutPartialGraph") {
    auto candidate                        = request();
    candidate.ladders.front().rungSpacing = 0.f;
    auto rejected                         = eve::climbing::bakeClimbingAnchorGraph(candidate);
    CHECK(!rejected.ok());
    CHECK_EQ(static_cast<int>(rejected.error()->code()), static_cast<int>(eve::DiagnosticCode::InvalidArgument));
}
