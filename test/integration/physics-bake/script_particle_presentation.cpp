#include <simplesquirrel/simplesquirrel.hpp>
#include "graphics/Graphics.h"
#include "graphics/RenderSystem.h"
#include "graphics/Texture.h"
#include "physics/backend/bake/BakeRecipe.h"
#include "physics/backend/bake/BakedParticlePlayback.h"
#include "physics/backend/bake/script/PhysicsBake.h"
#include "physics/bake_graphics/script/PhysicsBakeGraphics.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

TEST_CASE("physics.bake.scriptParticlePresentationBatchesMaterialsAndRejectsPartialDraw") {
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
    PersistentId::Bytes bytes{};
    bytes.back() = 1;
    const PersistentId firstId(bytes);
    bytes.back() = 2;
    const PersistentId secondId(bytes);
    auto               source = [&] {
        BakeRecipe recipe;
        recipe.setProducer("script-particle-presentation-test").expect("producer");
        recipe.setBackend("recorded-fixture").expect("backend");
        recipe.setParameters("").expect("parameters");
        recipe.setSource("fixture", "particles").expect("source");
        recipe.setTiming(1, 1).expect("timing");
        recipe.setCoordinates(BakeCoordinates::PlanarPixels).expect("coordinates");
        BakedParticlePose red, green;
        red.x = green.x = 80;
        red.y = green.y = 60;
        red.w = green.w = 20;
        red.h = green.h = 20;
        red.r           = 1;
        red.g = red.b = 0;
        green.g       = 1;
        green.r = green.b = 0;
        auto track        = ParticleTrack::prepare(
                         1, {{firstId, 1, {{0, red}, {1, red}}}, {secondId, 1, {{0, green}, {1, green}}}}, recipe)
                         .expect("track");
        return std::make_unique<BakedParticlePlayback>(BakedParticlePlayback::prepare(track).expect("player"));
    }();
    struct Materials {
        graphics::Renderable2D* first  = graphics::Renderable2D::create();
        graphics::Renderable2D* second = graphics::Renderable2D::create();
        ~Materials() {
            ecs::DestroyEntity(first);
            ecs::DestroyEntity(second);
        }
    } materials;
    materials.first->setReceiveLight(false);
    materials.second->setReceiveLight(false);
    materials.first->setVisible(false);
    materials.second->setVisible(false);
    ssq::VM vm(2048);
    auto    table = vm.addTable("eve");
    graphics::Graphics::expose(table);
    PhysicsBake::expose(table);
    PhysicsBakeGraphics::expose(table);
    vm.addFunc("sourcePlayer", [&] { return source.get(); });
    vm.addFunc("destroySource", [&] { source.reset(); });
    vm.addFunc("graphicsHost", [&] { return graphics; });
    vm.addFunc("firstId", [&] { return firstId.format(); });
    vm.addFunc("secondId", [&] { return secondId.format(); });
    vm.addFunc("firstMaterial", [&] { return materials.first; });
    vm.addFunc("secondMaterial", [&] { return materials.second; });
    graphics::Texture* texture        = nullptr;
    auto               releaseTexture = [&] {
        materials.first->setTexture(nullptr);
        materials.second->setTexture(nullptr);
        if (texture) {
            REQUIRE(graphics->releaseTexture(texture));
            REQUIRE(texture->gpuHandle == nullptr);
            delete texture;
            texture = nullptr;
        }
    };
    vm.addFunc("replaceTexture", [&](bool white) {
        releaseTexture();  // Backend drains in-flight users before transferring the facade.
        const unsigned char value    = white ? 255 : 0;
        const unsigned char pixels[] = {value, value, value, 255};
        texture                      = graphics->newTexture(1, 1, pixels);
        REQUIRE(texture != nullptr);
        materials.first->setTexture(texture);
        materials.second->setTexture(texture);
    });
    vm.addFunc("replaceMaterials", [&] {
        ecs::DestroyEntity(materials.first);
        ecs::DestroyEntity(materials.second);
        materials.first  = graphics::Renderable2D::create();
        materials.second = graphics::Renderable2D::create();
        for (auto* material : {materials.first, materials.second}) {
            material->setReceiveLight(false);
            material->setVisible(false);
        }
    });
    vm.addFunc("clearFrame", [&] { graphics->clear(Color(0, 0, 0, 1), std::nullopt, std::nullopt); });
    vm.addFunc("present", [&] { graphics->present(); });
    vm.addFunc("red", [&] { return graphics->getPixel(80, 60).r; });
    vm.addFunc("green", [&] { return graphics->getPixel(80, 60).g; });
    vm.run(vm.compileSource(R"(
        local bridge = eve.PhysicsBakeGraphics();
        local source = sourcePlayer(); assert(source.seek(1.0).ok);
        local ids = [firstId(),secondId()];
        assert(!bridge.prepareParticles(null,ids).ok);
        assert(!bridge.prepareParticles(source,null).ok);
        assert(!bridge.prepareParticles(source,[firstId(),firstId()]).ok);
        assert(!bridge.prepareParticles(source,[secondId(),firstId()]).ok);
        assert(!bridge.prepareParticles(source,[12]).ok);
        assert(!bridge.prepareParticles(source,["invalid"]).ok);
        assert(!bridge.prepareParticles(source,ids).ok);
            local ownedSource = source.prepareInstance().value; assert(ownedSource.seek(1.0).ok);
            local bound = bridge.prepareParticles(ownedSource,ids);
        assert(bound.ok && bound.value.time() == ownedSource.time());
        assert(bound.value.seek(0.5).ok && ownedSource.time() == 0.5);
        bound = null; assert(source.seek(1.0).ok);
        local ready = bridge.prepareParticles(source.prepareInstance().value,ids); assert(ready.ok);
        local first = ready.value; ready = null;
        local other = bridge.prepareParticles(source.prepareInstance().value,ids); assert(other.ok);
        local second = other.value; other = null;
        local partialResult = bridge.prepareParticles(source.prepareInstance().value,[firstId()]); assert(partialResult.ok);
        local partial = partialResult.value; partialResult = null;
        source = null; collectgarbage(); destroySource();
        ids.clear(); // Prepared identities are owning values, not a retained VM array.
        assert(first.time() == 0.0 && second.time() == 0.0);
        local a = firstMaterial(); local b = secondMaterial();
        a.setLayer(0); b.setLayer(1);
        clearFrame();
        assert(!first.draw(null,[a,b]).ok);
        assert(!first.draw(graphicsHost(),null).ok);
        assert(!first.draw(graphicsHost(),[a]).ok);
        assert(!first.draw(graphicsHost(),[a,null]).ok);
        assert(!first.draw(graphicsHost(),[a,graphicsHost()]).ok);
        assert(!partial.draw(graphicsHost(),[a]).ok);
        present(); assert(red() < 0.01 && green() < 0.01);
        clearFrame(); local drawn = first.draw(graphicsHost(),[a,b]);
        assert(drawn.ok && drawn.value == 2 && drawn.hasValue);
        present(); assert(green() > 0.5 && red() < 0.01);
        a.setLayer(2); // Material changes are read afresh, not frozen into the clip.
        clearFrame(); assert(first.draw(graphicsHost(),[a,b]).ok);
        present(); assert(red() > 0.5 && green() < 0.01);
        assert(first.seek(1.0).ok);
        clearFrame(); drawn = first.draw(graphicsHost(),[a,b]);
        assert(drawn.ok && drawn.value == 0); present(); assert(red() < 0.01);
        assert(second.time() == 0.0);
        assert(second.setPlacement(100.0,0.0,0.0,0.0,0.0,0.0,1.0).ok);
        clearFrame(); assert(second.draw(graphicsHost(),[a,b]).ok);
        present(); assert(red() < 0.01 && green() < 0.01);
        first = null; partial = null; collectgarbage();
        assert(second.setPlacement(0.0,0.0,0.0,0.0,0.0,0.0,1.0).ok);
        clearFrame(); assert(second.draw(graphicsHost(),[a,b]).ok);
        present(); assert(red() > 0.5);
        // Drop every script alias before retiring the caller-owned ECS materials.
        // The presentation remains live and must use fresh bindings on its next draw.
        a = null; b = null; collectgarbage();
        replaceMaterials();
        assert(second.seek(0.5).ok && second.time() == 0.5);
        a = firstMaterial(); b = secondMaterial();
        a.setLayer(0); b.setLayer(1);
        clearFrame(); drawn = second.draw(graphicsHost(),[a,b]);
        assert(drawn.ok && drawn.value == 2);
        present(); assert(green() > 0.5 && red() < 0.01);
        replaceTexture(true);
        clearFrame(); assert(second.draw(graphicsHost(),[a,b]).ok);
        present(); assert(green() > 0.5 && red() < 0.01);
        replaceTexture(false); // Retire the old GPU allocation while the player stays live.
        clearFrame(); assert(second.draw(graphicsHost(),[a,b]).ok);
        present(); assert(green() < 0.01 && red() < 0.01);
        replaceTexture(true);
        clearFrame(); assert(second.draw(graphicsHost(),[a,b]).ok);
        present(); assert(green() > 0.5 && red() < 0.01);
        second = null; collectgarbage();
        // The reverse destruction order must leave caller-owned materials usable.
        a.setLayer(3); b.setLayer(2);
    )"));
    releaseTexture();
    window->close();
}
