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
    "procgen_road_recipe_snapshot.cpp"
  ],
  "module": "procgen"
}
*/
// clang-format on

#include "editing/EditingProperty.h"
#include "procgen/editing/RoadNetworkEditTarget.h"
#include "procgen/road/RoadNetworkCodec.h"
#include "procgen/road/RoadRecipes.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::procgen;
using namespace eve::procgen::road;

TEST_CASE("procgen.road.authoring.recipeSnapshotConsumesEditorAuthorDataAndLOD") {
    auto source = RoadNetwork::makeStraight(40.f, 2);
    REQUIRE(source.ok());
    auto network       = std::move(source).takeValue();
    auto style         = network.edges()[0].style;
    style.structures   = {{.1f, .3f, RoadStructureKind::Bridge}, {.6f, .9f, RoadStructureKind::Tunnel}};
    style.crossSection = {{-4.f, 0.f, RoadMaterial::Asphalt}, {4.f, 0.f, RoadMaterial::Asphalt}};
    REQUIRE(network.setEdgeStyle(1, style).ok());
    auto points           = network.edges()[0].controlPoints;
    points[0].widthScale  = .8f;
    points[1].widthScale  = 1.3f;
    points[1].rollDegrees = 12.f;
    REQUIRE(network.setEdgeControlPoints(1, points).ok());
    eve::procgen_editing::RoadNetworkEditTarget editor("author", network);
    auto                                        encoded = captureRoadNetwork(network);
    REQUIRE(encoded.ok());
    REQUIRE(encoded.value() == eve::editing::toPresentationValue(editor.snapshotValue()));
    auto restored = restoreRoadNetwork(encoded.value());
    REQUIRE(restored.ok());
    REQUIRE(captureRoadNetwork(restored.value()).value() == encoded.value());
    Params params;
    params.setString("scene", "this-scene-must-not-be-used");
    params.setInt("pathSegments", 32);
    REQUIRE(setRoadRecipeNetwork(params, network).ok());
    auto before = params.canonicalString();
    auto bake   = bakeRoadNetworkRecipe(params);
    REQUIRE(bake.ok());
    RoadBakeOptions options;
    options.pathSegmentsPerEdge = 32;
    auto direct                 = bakeRoadNetwork(network, options);
    REQUIRE(direct.ok());
    REQUIRE(bake.value().mesh.positions() == direct.value().mesh.positions());
    REQUIRE(bake.value().mesh.indices() == direct.value().mesh.indices());
    for (int lod = 1; lod < 4; ++lod) {
        Params coarse = params;
        coarse.setInt("pathSegments", 32 >> lod);
        coarse.setBool("navigation", false);
        auto lodBake = bakeRoadNetworkRecipe(coarse);
        REQUIRE(lodBake.ok());
        auto author = resolveRoadRecipeNetwork(coarse);
        REQUIRE(author.ok());
        REQUIRE(captureRoadNetwork(author.value()).value() == encoded.value());
    }
    REQUIRE(params.canonicalString() == before);
}
