// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "default": "render-offscreen",
  "module": "graphics"
}
*/
// clang-format on

#include "zeroerr/unittest.h"

#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/ShadowScheme.h"
#include "image/ImageData.h"

#include <cmath>
#include <memory>

TEST_CASE("editor.material.preview_shadows_affect_pixels_and_capture_path_stays_unshadowed") {
    using namespace eve::graphics;
    auto* gfx = Graphics::create();
    gfx->initHeadless(160, 160);
    auto* camera = Camera3D::createCamera();
    camera->setEye(0.f, 3.f, 7.f);
    camera->setTarget(0.f, -.2f, 0.f);
    camera->setUp(0.f, 1.f, 0.f);
    camera->setClipPlanes(.1f, 30.f);
    camera->setAmbient(.05f, .05f, .05f);
    auto* sun = Light3D::createLight("dir");
    sun->setDirection(-.6f, 1.f, .2f);
    sun->setColor(1.f, 1.f, 1.f, 1.f);
    sun->setCastShadow(true);
    sun->setShadowMethod("csm");
    ShadowSchemeSettings::current().enableDirectionalCsm = true;
    auto* model                                          = Renderable3D::create();
    model->meshRenderer()->mesh                          = gfx->newMeshSphere(48, 24);
    model->setCastShadow(true);
    // Isolate the ground's received shadow from any sphere self-shadowing.
    model->setReceiveShadow(false);
    auto* ground                 = Renderable3D::create();
    ground->meshRenderer()->mesh = gfx->newMeshCube(1.f);
    ground->setScale(8.f, .1f, 8.f);
    ground->setPosition(0.f, -1.05f, 0.f);
    ground->setCastShadow(false);
    ground->setReceiveShadow(true);
    auto* target = gfx->newCanvas(160, 160);
    gfx->renderScene3DToCanvas(target, camera);
    std::unique_ptr<eve::image::ImageData> baseline(target->newImageData());
    REQUIRE(baseline != nullptr);
    auto changedPixels = [&](const eve::image::ImageData& image) {
        int changed = 0;
        for (int y = 0; y < 160; ++y)
            for (int x = 0; x < 160; ++x)
                if (baseline->getPixel(x, y).r - image.getPixel(x, y).r > .025f) ++changed;
        return changed;
    };
    auto captureShadowed = [&] {
        gfx->renderScene3DWithShadowsToCanvas(target, camera);
        return std::unique_ptr<eve::image::ImageData>(target->newImageData());
    };
    auto shadowed = captureShadowed();
    REQUIRE(shadowed != nullptr);
    REQUIRE_GT(changedPixels(*shadowed), 30);

    ground->setReceiveShadow(false);
    auto noReceiver = captureShadowed();
    CHECK_EQ(changedPixels(*noReceiver), 0);
    ground->setReceiveShadow(true);
    model->setCastShadow(false);
    auto noCaster = captureShadowed();
    CHECK_EQ(changedPixels(*noCaster), 0);
    model->setCastShadow(true);
    sun->setCastShadow(false);
    auto noLightShadow = captureShadowed();
    CHECK_EQ(changedPixels(*noLightShadow), 0);
    sun->setCastShadow(true);
    gfx->renderScene3DToCanvas(target, camera);
    std::unique_ptr<eve::image::ImageData> capture(target->newImageData());
    CHECK_EQ(changedPixels(*capture), 0);
    model->setVisible(false);
    ground->setVisible(false);
    sun->setEnabled(false);
    camera->setActive(false);
}
