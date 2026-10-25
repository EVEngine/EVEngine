// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "role": "tests",
  "default": "render-window",
  "module": "physics_bake_graphics"
}
*/
// clang-format on

#include <memory>
#include "common/config.h"
#include "physics/backend/bake/BakeRecipe.h"
#include "physics/backend/bake/DeformPlayback.h"
#include "physics/bake_graphics/DeformMeshPresentation.h"
#ifdef EVENGINE_WEBGPU
#include "graphics/webgpu/Graphics.h"
using BakeGraphicsBackend = eve::graphics::webgpu::Graphics;
#else
#include "graphics/vulkan/Graphics.h"
using BakeGraphicsBackend = eve::graphics::vulkan::Graphics;
#endif
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

TEST_CASE("physics.bake.deformPresentationSurvivesGraphicsOwnerReplacement") {
    using namespace eve::physics;
    BakeRecipe recipe;
    recipe.setProducer("graphics-retirement-v1").expect("producer");
    recipe.setBackend("fixture-values").expect("backend");
    recipe.setSource("fixture", "two-frames").expect("source");
    recipe.setParameters("").expect("parameters");
    recipe.setTiming(1.f, 1).expect("timing");
    for (auto primitive : {DeformPrimitive::Triangles, DeformPrimitive::Segments}) {
        const bool surface  = primitive == DeformPrimitive::Triangles;
        auto       topology = DeformTopology::prepare(
                            primitive, 3,
                            surface ? std::vector<std::uint32_t>{0, 1, 2} : std::vector<std::uint32_t>{0, 1, 1, 2}, {})
                            .expect("topology");
        auto track = DeformTrack::prepare({{0, topology, {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}},
                                           {1, topology, {{2, 0, 0}, {3, 0, 0}, {2, 1, 0}}}},
                                          recipe)
                         .expect("track");
        auto player       = DeformPlayback::prepare(track).expect("player");
        auto presentation = DeformMeshPresentation::prepare(track, .1f).expect("presentation");
        {
            auto owner = std::make_unique<BakeGraphicsBackend>();
            owner->initHeadless(16, 16);
            auto* mesh = presentation->createMesh(player, *owner).expect("first owner mesh");
            REQUIRE(owner->describeMesh(mesh).has_value());
            REQUIRE(player.seek(1).ok());
            REQUIRE(presentation->upload(player, *owner, *mesh).ok());
            // The owner destroys its registered mesh. Never reuse the facade after this scope.
        }
        REQUIRE(player.seek(.5).ok());
        std::vector<DeformPoint> points(3);
        REQUIRE(player.sample(points).ok());
        REQUIRE(points[0].x == 1);
        {
            auto replacement = std::make_unique<BakeGraphicsBackend>();
            replacement->initHeadless(16, 16);
            auto* mesh = presentation->createMesh(player, *replacement).expect("replacement owner mesh");
            REQUIRE(replacement->describeMesh(mesh).has_value());
            REQUIRE(player.time() == .5);
            REQUIRE(player.seek(0).ok());
            REQUIRE(presentation->upload(player, *replacement, *mesh).ok());
        }
        REQUIRE(player.sample(points).ok());
        REQUIRE(points[0].x == 0);
        presentation.reset();
    }
}
