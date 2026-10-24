#include <glm/gtc/matrix_transform.hpp>
#include <simplesquirrel/simplesquirrel.hpp>
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "physics/bake/BakeRecipe.h"
#include "physics/bake/DeformPlayback.h"
#include "physics/bake/graphics_script/PhysicsBakeGraphics.h"
#include "physics/bake/script/PhysicsBake.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

TEST_CASE("physics.bake.scriptDeformPresentationRendersAfterSourceAndPresenterRelease") {
    using namespace eve;
    using namespace eve::physics;
    auto*                  window   = window::Window::create();
    auto*                  graphics = graphics::Graphics::create();
    window::WindowSettings settings;
    settings.width  = 160;
    settings.height = 120;
    REQUIRE(window->setWindowSettings(settings));
    graphics->setScreenReadbackEnabled(true);
    graphics->setBackgroundColor({0, 0, 0, 1});
    const glm::vec3 eye(0, 0, 3);
    const auto      view       = glm::lookAtRH(eye, glm::vec3(0), glm::vec3(0, 1, 0));
    auto            projection = glm::perspectiveRH_ZO(glm::radians(55.f), 160.f / 120.f, 0.1f, 100.f);
    projection[1][1] *= -1;
    for (auto primitive : {DeformPrimitive::Triangles, DeformPrimitive::Segments}) {
        auto source = [&] {
            BakeRecipe recipe;
            recipe.setProducer("script-deform-presentation-test").expect("producer");
            recipe.setBackend("recorded-fixture").expect("backend");
            recipe.setParameters("").expect("parameters");
            recipe.setSource("fixture", "mesh").expect("source");
            recipe.setTiming(1, 1).expect("timing");
            const bool surface  = primitive == DeformPrimitive::Triangles;
            auto       topology = DeformTopology::prepare(
                                primitive, surface ? 3 : 2,
                                surface ? std::vector<std::uint32_t>{0, 1, 2} : std::vector<std::uint32_t>{0, 1}, {})
                                .expect("topology");
            const auto points = surface ? std::vector<DeformPoint>{{-0.8f, -0.8f, 0}, {0.8f, -0.8f, 0}, {0, 0.8f, 0}}
                                        : std::vector<DeformPoint>{{0, -1, 0}, {0, 1, 0}};
            auto       moved  = points;
            for (auto& point : moved) point.x += 4;
            auto track = DeformTrack::prepare({{0, topology, points}, {1, topology, moved}}, recipe).expect("track");
            return std::make_unique<DeformPlayback>(DeformPlayback::prepare(track).expect("player"));
        }();
        // Released facades stay alive through the VM's lifetime so rejected upload
        // uses a live detached handle, never an intentionally dangling pointer.
        std::vector<std::unique_ptr<graphics::Mesh>> released;
        graphics::Mesh                               foreign;
        ssq::VM                                      vm(2048);
        auto                                         table = vm.addTable("eve");
        graphics::Graphics::expose(table);
        PhysicsBake::expose(table);
        PhysicsBakeGraphics::expose(table);
        vm.addFunc("sourcePlayer", [&] { return source.get(); });
        vm.addFunc("destroySource", [&] { source.reset(); });
        vm.addFunc("graphicsHost", [&] { return graphics; });
        vm.addFunc("foreignMesh", [&] { return &foreign; });
        vm.addFunc("isRope", [&] { return primitive == DeformPrimitive::Segments; });
        vm.addFunc("redAtCenter", [&](graphics::Mesh* mesh) {
            graphics->clear(Color(0, 0, 0, 1), std::nullopt, std::nullopt);
            graphics->begin3DFrame();
            graphics->setMesh3DViewProj(projection * view);
            graphics->setMesh3DView(view);
            graphics->setMesh3DCameraPos(eye);
            graphics->drawMesh(mesh, glm::mat4(1), nullptr, {1, 0, 0, 1});
            graphics->present();
            return graphics->getPixel(80, 60).r;
        });
        vm.addFunc("releaseMesh", [&](graphics::Mesh* mesh) {
            const bool detached = graphics->releaseMesh(mesh);
            if (detached) released.emplace_back(mesh);
            return detached;
        });
        vm.run(vm.compileSource(R"(
            local bridge = eve.PhysicsBakeGraphics();
            local source = sourcePlayer();
            assert(source.seek(1.0).ok);
            assert(!bridge.prepareDeform(null,0.2).ok);
            assert(!bridge.prepareDeform(graphicsHost(),0.2).ok);
            if (isRope()) assert(!bridge.prepareDeform(source,0.0).ok);
            local prepared = bridge.prepareDeform(source,0.2);
            assert(prepared.ok && prepared.hasValue);
            local first = prepared.value; prepared = null;
            local secondResult = bridge.prepareDeform(source,0.2);
            assert(secondResult.ok);
            local second = secondResult.value; secondResult = null;
            assert(first.time() == 0.0 && second.time() == 0.0 && source.time() == 1.0);
            source = null; collectgarbage(); destroySource();
            assert(!first.createMesh(null).ok);
            assert(!first.createMesh(foreignMesh()).ok);
            local created = first.createMesh(graphicsHost());
            assert(created.ok && created.hasValue && created.ownership == "graphics-owned");
            local mesh = created.value; created = null;
            assert(redAtCenter(mesh) > 0.01);
            assert(!first.upload(null,mesh).ok);
            assert(!first.upload(graphicsHost(),null).ok);
            assert(!first.upload(graphicsHost(),graphicsHost()).ok);
            assert(!first.upload(graphicsHost(),foreignMesh()).ok);
            assert(first.seek(1.0).ok && first.upload(graphicsHost(),mesh).ok);
            assert(first.time() == 1.0 && second.time() == 0.0);
            assert(redAtCenter(mesh) < 0.01);
            assert(first.setRate(-1.0).ok);
            first.setPaused(true); assert(first.paused());
            assert(first.advance(1.0).ok && first.time() == 1.0);
            first.setPaused(false);
            assert(first.advance(1.0).ok && first.time() == 0.0);
            assert(first.upload(graphicsHost(),mesh).ok && redAtCenter(mesh) > 0.01);
            assert(first.setPlacement(4.0,0.0,0.0,0.0,0.0,0.0,1.0).ok);
            assert(first.upload(graphicsHost(),mesh).ok && redAtCenter(mesh) < 0.01);
            assert(second.upload(graphicsHost(),mesh).ok && redAtCenter(mesh) > 0.01);
            first = null; collectgarbage();
            assert(redAtCenter(mesh) > 0.01);
            assert(releaseMesh(mesh));
            assert(!second.upload(graphicsHost(),mesh).ok);
            mesh = null; second = null; collectgarbage();
        )"));
    }
    window->close();
}
