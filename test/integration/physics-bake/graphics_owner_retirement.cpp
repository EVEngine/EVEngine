#include <memory>
#include "common/config.h"
#include "physics/bake/BakeRecipe.h"
#include "physics/bake/DeformPlayback.h"
#include "physics/bake/graphics/DeformMeshPresentation.h"
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
        auto presentation = DeformMeshPresentation::prepare(track, .1f).expect("presentation");
        {
            auto owner = std::make_unique<BakeGraphicsBackend>();
            owner->initHeadless(16, 16);
            auto* mesh = presentation->createMesh(*owner).expect("first owner mesh");
            REQUIRE(owner->describeMesh(mesh).has_value());
            REQUIRE(presentation->player().seek(1).ok());
            REQUIRE(presentation->upload(*owner, *mesh).ok());
            // The owner destroys its registered mesh. Never reuse the facade after this scope.
        }
        REQUIRE(presentation->player().seek(.5).ok());
        std::vector<DeformPoint> points(3);
        REQUIRE(presentation->player().sample(points).ok());
        REQUIRE(points[0].x == 1);
        {
            auto replacement = std::make_unique<BakeGraphicsBackend>();
            replacement->initHeadless(16, 16);
            auto* mesh = presentation->createMesh(*replacement).expect("replacement owner mesh");
            REQUIRE(replacement->describeMesh(mesh).has_value());
            REQUIRE(presentation->player().time() == .5);
            REQUIRE(presentation->player().seek(0).ok());
            REQUIRE(presentation->upload(*replacement, *mesh).ok());
        }
        REQUIRE(presentation->player().sample(points).ok());
        REQUIRE(points[0].x == 0);
        presentation.reset();
    }
}
