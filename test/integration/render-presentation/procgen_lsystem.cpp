#include "procgen/PointSet.h"
#include "procgen/Procgen.h"
#include "procgen/algorithms/LSystem.h"

#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Texture.h"
#include "image/Image.h"
#include "window/Window.h"
#include "RenderImageAudit.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <memory>
#include <set>

using namespace eve::procgen;
using namespace eve::graphics;

TEST_CASE("procgen.lsystem.renderDump") {
    const char *outputPath = std::getenv("EVENGINE_LSYSTEM_RENDER_PNG");
    if (!outputPath || !outputPath[0]) return;

    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings ws;
    ws.width = 900;
    ws.height = 700;
    ws.centered = true;
    REQUIRE(win->setWindowSettings(ws));

    Params params;
    params.setSeed(20260826);
    const char *styleOverride = std::getenv("EVENGINE_LSYSTEM_STYLE");
    params.setString("style", styleOverride ? styleOverride : "tree");
    params.setInt("iterations", 5);
    params.setString("leafMode", "cards");
    params.setFloat("tropism", 0.05f);
    params.setFloat("leafSize", 0.5f);

    Procgen generator;
    auto    paramsResult = Procgen::newParamsHandle();
    REQUIRE(paramsResult.ok());
    auto paramsHandle = std::move(paramsResult).takeValue();
    auto paramsView   = Procgen::resolve(paramsHandle);
    REQUIRE(paramsView.isBound());
    *paramsView     = params;
    auto lsMeshView = generator.generateMeshBorrowed("mesh.lsystem", paramsHandle, gfx);
    REQUIRE(lsMeshView.isBound());
    Mesh *lsMesh = lsMeshView.get();

    // 4px bark/foliage atlas; UVs are partitioned by the mesh recipe (left = bark).
    const uint8_t atlasPixels[] = {
        111, 70, 42, 255, 128, 82, 47, 255,
        64, 119, 57, 255, 82, 145, 67, 255,
    };
    Texture *atlas = gfx->newTexture(4, 1, atlasPixels);
    REQUIRE(atlas != nullptr);

    auto *plant = Renderable3D::create();
    plant->setMesh(lsMesh);
    plant->setTexture(atlas);
    plant->setTint(1.f, 1.f, 1.f, 1.f);
    plant->setRoughness(0.88f);

    auto *camera = Camera3D::createCamera();
    camera->setEye(7.5f, 5.2f, 9.5f);
    camera->setTarget(0.f, 3.2f, 0.f);
    camera->setUp(0.f, 1.f, 0.f);
    camera->setFov(36.f);
    camera->setAmbient(0.30f, 0.34f, 0.28f);
    camera->setActive(true);

    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.075f, 0.105f, 0.095f, 1.f));
    RenderSystem3D::setDirectionalLight(-0.55f, -1.f, -0.35f, 1.45f, 1.32f, 1.08f);

    for (int frame = 0; frame < 4; ++frame) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }
    std::unique_ptr<eve::image::ImageData> image(gfx->newImageData());
    REQUIRE(image.get() != nullptr);
    REQUIRE(saveImagePng(*image, outputPath));
    std::printf("lsystem render saved: %s\n", outputPath);
    REQUIRE(Procgen::release(paramsHandle).ok());
    win->close();
}
