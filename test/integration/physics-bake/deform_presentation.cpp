#include <glm/gtc/matrix_transform.hpp>
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "physics/bake/BakeRecipe.h"
#include "physics/bake/DeformPlayback.h"
#include "physics/bake/graphics/DeformMeshPresentation.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::physics;
TEST_CASE("physics.bake.deformMeshesRenderAndUpdateWithoutSimulation") {
    auto*                       window   = eve::window::Window::create();
    auto*                       graphics = eve::graphics::Graphics::create();
    eve::window::WindowSettings settings;
    settings.width  = 160;
    settings.height = 120;
    REQUIRE(window->setWindowSettings(settings));
    graphics->setScreenReadbackEnabled(true);
    graphics->setBackgroundColor({0, 0, 0, 1});
    BakeRecipe recipe;
    recipe.setProducer("deform-render-test").expect("producer");
    recipe.setBackend("recorded-test").expect("backend");
    recipe.setParameters("").expect("parameters");
    recipe.setSource("fixture", "mesh").expect("source");
    recipe.setTiming(1, 1).expect("timing");
    const glm::vec3 eye(0, 0, 3);
    const auto      view       = glm::lookAtRH(eye, glm::vec3(0), glm::vec3(0, 1, 0));
    auto            projection = glm::perspectiveRH_ZO(glm::radians(55.f), 160.f / 120.f, 0.1f, 100.f);
    projection[1][1] *= -1;
    for (auto primitive : {DeformPrimitive::Triangles, DeformPrimitive::Segments}) {
        const bool surface  = primitive == DeformPrimitive::Triangles;
        auto       topology = DeformTopology::prepare(
                            primitive, surface ? 3 : 2,
                            surface ? std::vector<std::uint32_t>{0, 1, 2} : std::vector<std::uint32_t>{0, 1}, {})
                            .expect("topology");
        const std::vector<DeformPoint> positions =
            surface ? std::vector<DeformPoint>{{-0.8f, -0.8f, 0}, {0.8f, -0.8f, 0}, {0, 0.8f, 0}}
                    : std::vector<DeformPoint>{{0, -1, 0}, {0, 1, 0}};
        auto moved = positions;
        for (auto& point : moved) point.x += 4;
        auto  track = DeformTrack::prepare({{0, topology, positions}, {1, topology, moved}}, recipe).expect("track");
        auto  presentation = DeformMeshPresentation::prepare(track, 0.2f).expect("presentation");
        auto* mesh         = presentation->createMesh(*graphics).expect("GPU mesh");
        auto  draw         = [&]() {
            graphics->begin3DFrame();
            graphics->setMesh3DViewProj(projection * view);
            graphics->setMesh3DView(view);
            graphics->setMesh3DCameraPos(eye);
            graphics->drawMesh(mesh, glm::mat4(1), nullptr, {1, 0, 0, 1});
            graphics->present();
            return graphics->getPixel(80, 60);
        };
        REQUIRE(draw().r > 0.01f);
        REQUIRE(presentation->player().seek(1).ok());
        REQUIRE(presentation->upload(*graphics, *mesh).ok());
        REQUIRE(draw().r < 0.01f);
        REQUIRE(presentation->player().seek(0).ok());
        REQUIRE(presentation->upload(*graphics, *mesh).ok());
        // Keep the detached facade alive while testing rejection; never pass a dangling reference.
        REQUIRE(graphics->releaseMesh(mesh));
        REQUIRE(!presentation->upload(*graphics, *mesh).ok());
        REQUIRE(presentation->player().time() == 0);
        delete mesh;
        mesh = presentation->createMesh(*graphics).expect("replacement mesh after target retirement");
        REQUIRE(draw().r > 0.01f);
        REQUIRE(presentation->player().seek(1).ok());
        REQUIRE(presentation->upload(*graphics, *mesh).ok());
        REQUIRE(draw().r < 0.01f);
        REQUIRE(presentation->player().seek(0).ok());
        REQUIRE(presentation->upload(*graphics, *mesh).ok());
        presentation.reset();
        REQUIRE(draw().r > 0.01f);
        REQUIRE(graphics->releaseMesh(mesh));
        delete mesh;
    }
    window->close();
}
