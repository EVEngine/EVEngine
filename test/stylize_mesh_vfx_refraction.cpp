#include "zeroerr/unittest.h"
#include "Fixtures.h"

#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Texture.h"
#include "stylize/MeshEffect.h"
#include "stylize/MeshEffectRenderer.h"
#include "stylize/TrailEffect.h"
#include "window/Window.h"

#include <glm/mat4x4.hpp>

using namespace eve::graphics;
using namespace eve::stylize;

TEST_CASE("stylize.meshVfx.sceneColorRefractionSamplesBoundTexture") {
    eve::window::Window *window = nullptr;
    Graphics *graphics = nullptr;
    openGfxWindow(window, graphics, 64, 64);
    REQUIRE(graphics != nullptr);

    const uint8_t greenPixel[4] = {0, 255, 0, 255};
    Texture *sceneColor = graphics->newTexture(1, 1, greenPixel);
    Canvas *target = graphics->newCanvas(64, 64);
    REQUIRE(sceneColor != nullptr);
    REQUIRE(target != nullptr);

    MeshEffectInstance effect("slash");
    effect.style().setFloat("refractionStrength", 1.f);
    effect.style().setFloat("flowWarp", 0.f);
    effect.style().setFloat("edgeDistortion", 0.f);
    effect.play();
    effect.update(0.1f);

    TrailMeshSnapshot ribbon;
    ribbon.vertices = {
        {{-0.8f, -0.8f, 0.f}, {0.f, 0.f}, 1.f},
        {{-0.8f,  0.8f, 0.f}, {0.f, 1.f}, 1.f},
        {{ 0.8f, -0.8f, 0.f}, {1.f, 0.f}, 1.f},
        {{ 0.8f,  0.8f, 0.f}, {1.f, 1.f}, 1.f},
    };
    ribbon.indices = {0, 1, 2, 2, 1, 3};

    MeshEffectRenderer renderer(*graphics);
    graphics->setMesh3DSceneColor(sceneColor);
    graphics->setMesh3DViewProj(glm::mat4(1.f));
    graphics->begin3DFrameToCanvas(target);
    auto submitted = renderer.submitTrail(effect, ribbon);
    REQUIRE(submitted.hasValue());
    REQUIRE(static_cast<int>(submitted.value()) ==
            static_cast<int>(MeshEffectSubmitStatus::Drawn));
    graphics->end3DFrameToCanvas();
    graphics->setMesh3DSceneColor(nullptr);

    const Color center = target->getPixel(32, 32);
    REQUIRE(center.g > 0.05f);
    REQUIRE(center.g > center.r * 2.f);
    REQUIRE(center.g > center.b * 2.f);
    window->close();
}
