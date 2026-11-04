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

#include <filesystem>
#include <random>
#include "graphics/DrawItem2D.h"
#include "graphics/Graphics.h"
#include "particles/ParticleRecording.h"
#include "particles/bake/capture/ParticleBakeCapture.h"
#include "physics/backend/bake/BakeRecipe.h"
#include "physics/backend/bake/BakedParticlePlayback.h"
#include "physics/bake_graphics/ParticleBakePresentation.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::physics;
using eve::particles::buildParticleTrack;
using eve::particles::ParticleRecordingSource;
TEST_CASE("physics.bake.particleSpritesRenderIndependentBirthIntervals") {
    auto*                       window   = eve::window::Window::create();
    auto*                       graphics = eve::graphics::Graphics::create();
    eve::window::WindowSettings settings;
    settings.width  = 160;
    settings.height = 120;
    REQUIRE(window->setWindowSettings(settings));
    graphics->setScreenReadbackEnabled(true);
    graphics->setBackgroundColor({0, 0, 0, 1});
    BakeRecipe recipe;
    recipe.setProducer("particle-render-test").expect("producer");
    recipe.setBackend("recorded-test").expect("backend");
    recipe.setParameters("").expect("parameters");
    recipe.setSource("fixture", "sprite").expect("source");
    recipe.setTiming(0.25f, 1).expect("timing");
    recipe.setCoordinates(BakeCoordinates::PlanarPixels).expect("coordinates");
    eve::PersistentId::Bytes bytes{};
    bytes.back()  = 1;
    auto* emitter = eve::particles::ParticleEmitter::createEmitter(4);
    emitter->setRandomSeed(19);
    recipe.setRandomStream("particles", 19).expect("seed");
    auto config       = emitter->config();
    config->x         = 80;
    config->y         = 60;
    config->particleW = config->particleH = 20;
    config->colorStart = config->colorEnd = eve::Color(1, 0, 0, 1);
    config->lifeMin = config->lifeMax = 1;
    config->sizeStart = config->sizeEnd = 1;
    config->speedMin = config->speedMax = 0;
    REQUIRE(eve::particles::prepareParticleRecording(*emitter, 64).ok());
    emitter->emit(1);
    for (int i = 0; i < 4; ++i) REQUIRE(eve::particles::advanceEmitterSim(*config, *emitter->sim(), 0.25f) > 0);
    auto events = eve::particles::finishParticleRecording(*emitter).expect("recorded events");
    ecs::DestroyEntity(emitter);
    struct Files {
        std::filesystem::path directory;
        Files() {
            std::random_device random;
            for (int i = 0; i < 100; ++i) {
                auto candidate =
                    std::filesystem::temp_directory_path() / ("eve-particle-render-" + std::to_string(random()));
                if (std::filesystem::create_directory(candidate)) {
                    directory = candidate;
                    return;
                }
            }
            throw std::runtime_error("cannot reserve test asset directory");
        }
        ~Files() {
            std::error_code error;
            std::filesystem::remove_all(directory, error);
        }
    } files;
    const auto path = files.directory / "recorded.eveparticle";
    {
        auto source = buildParticleTrack(eve::PersistentId(bytes), 2, events, recipe).expect("track");
        REQUIRE(source.save(path.string()).ok());
    }
    auto greenEvents = events;
    for (auto& event : greenEvents) event.appearance.color = eve::Color(0, 1, 0, 1);
    auto secondBytes   = bytes;
    secondBytes.back() = 2;
    const eve::PersistentId       redId(bytes), greenId(secondBytes);
    const ParticleRecordingSource sources[]{{redId, events}, {greenId, greenEvents}};
    auto                          combined     = buildParticleTrack(2, sources, recipe).expect("combined");
    const auto                    combinedPath = files.directory / "combined.eveparticle";
    REQUIRE(combined.save(combinedPath.string()).ok());
    auto groupTrack  = ParticleTrack::loadForRecipe(combinedPath.string(), recipe).expect("reload combined");
    auto groupPlayer = BakedParticlePlayback::prepare(groupTrack).expect("group player");
    auto group       = ParticleBakePresentation::prepare(groupTrack).expect("group");
    greenEvents.clear();
    events.clear();
    auto track        = ParticleTrack::loadForRecipe(path.string(), recipe).expect("reload after source destruction");
    auto firstPlayer  = BakedParticlePlayback::prepare(track).expect("first player");
    auto secondPlayer = BakedParticlePlayback::prepare(track).expect("second player");
    auto first        = ParticleBakePresentation::prepare(track).expect("first");
    auto second       = ParticleBakePresentation::prepare(track).expect("second");
    secondPlayer.seek(1).expect("dead");
    eve::graphics::DrawItem2D material;
    material.receiveLight = false;
    REQUIRE(!group->draw(firstPlayer, *graphics, material).ok());
    auto frame = [&](ParticleBakePresentation& presentation, BakedParticlePlayback& player) {
        graphics->clear(eve::Color(0, 0, 0, 1), std::nullopt, std::nullopt);
        auto drawn = presentation.draw(player, *graphics, material);
        REQUIRE(drawn.ok());
        graphics->present();
        return graphics->getPixel(80, 60).r;
    };
    REQUIRE(frame(*first, firstPlayer) > 0.5f);
    REQUIRE(frame(*second, secondPlayer) < 0.01f);
    REQUIRE(firstPlayer.time() == 0);
    firstPlayer.setPlacement({100, 0}).expect("move");
    REQUIRE(frame(*first, firstPlayer) < 0.01f);
    first.reset();
    secondPlayer.seek(0).expect("rewind");
    REQUIRE(frame(*second, secondPlayer) > 0.5f);
    second.reset();
    eve::graphics::DrawItem2D redMaterial, greenMaterial;
    redMaterial.receiveLight = greenMaterial.receiveLight = false;
    redMaterial.layer                                     = 1;
    greenMaterial.layer                                   = 0;
    const ParticleBakeMaterial materials[]{{redId, redMaterial}, {greenId, greenMaterial}};
    auto                       groupFrame = [&]() {
        graphics->clear(eve::Color(0, 0, 0, 1), std::nullopt, std::nullopt);
        REQUIRE(group->draw(groupPlayer, *graphics, materials).value() == 2);
        graphics->present();
        return graphics->getPixel(80, 60);
    };
    REQUIRE(groupFrame().r > 0.5f);
    redMaterial.layer   = 0;
    greenMaterial.layer = 1;
    REQUIRE(groupFrame().g > 0.5f);
    graphics->clear(eve::Color(0, 0, 0, 1), std::nullopt, std::nullopt);
    REQUIRE(!group->draw(groupPlayer, *graphics, std::span<const ParticleBakeMaterial>(materials).first(1)).ok());
    graphics->present();
    REQUIRE(graphics->getPixel(80, 60).r < 0.01f);
    REQUIRE(graphics->getPixel(80, 60).g < 0.01f);
    const ParticleBakeMaterial duplicates[]{{redId, redMaterial}, {redId, greenMaterial}};
    REQUIRE(!group->draw(groupPlayer, *graphics, duplicates).ok());
    REQUIRE(groupFrame().g > 0.5f);
    group.reset();
    window->close();
}
