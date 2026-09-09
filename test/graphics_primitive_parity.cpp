#include "GraphicsParitySupport.h"
#include "graphics/Canvas.h"
#include "graphics/ClipSpace.h"
#include "graphics/PrimitiveDrawList.h"

#include <array>

using namespace eve::graphics;
using namespace eve::graphics::parity_test;

TEST_CASE("graphics.backendParity.primitive2DAnd3DReadback") {
    Graphics *gfx = headlessGraphics();
    REQUIRE(gfx != nullptr);

    Canvas *flatTarget = gfx->newCanvas(64, 64);
    REQUIRE(flatTarget != nullptr);
    gfx->setCanvas(flatTarget);
    gfx->clear(Color(0.f, 0.f, 0.f, 1.f), std::nullopt, std::nullopt);
    PrimitiveCanvas2D flat;
    PrimitivePaint    flatPaint;
    flatPaint.mode         = PaintMode::FillAndStroke;
    flatPaint.color        = Color(1.f, 0.1f, 0.05f, 1.f);
    flatPaint.stroke.width = 3.f;
    flatPaint.stroke.cap   = LineCap::Round;
    flatPaint.stroke.join  = LineJoin::Round;
    flat.drawRoundedRect({10.f, 12.f}, {54.f, 50.f}, {8.f, 8.f}, flatPaint);
    PrimitivePaint dashedPaint;
    dashedPaint.mode         = PaintMode::Stroke;
    dashedPaint.color        = Color(0.1f, 0.65f, 1.f, 0.8f);
    dashedPaint.stroke.width = 4.f;
    dashedPaint.stroke.cap   = LineCap::Round;
    dashedPaint.stroke.dash  = DashPattern{{6.f, 3.f}, 1.5f, DashSpace::ScreenPixels};
    const std::array<glm::vec2, 4> dashedPoints{{{4.f, 8.f}, {20.f, 4.f}, {42.f, 9.f}, {60.f, 5.f}}};
    flat.drawPolyline(dashedPoints, false, dashedPaint);
    gfx->drawPrimitiveCanvas(flat);
    gfx->setCanvas();
    std::unique_ptr<eve::image::ImageData> flatImage(flatTarget->newImageData());
    REQUIRE(flatImage.get() != nullptr);
    REQUIRE(pixel(*flatImage, 32, 32)[0] > 160);
    writeParityArtifact(*flatImage, "primitive_2d_fill_stroke", gfx->getBackendName());

    Canvas *spatialTarget = gfx->newCanvas(64, 64);
    REQUIRE(spatialTarget != nullptr);
    SceneDrawContext context;
    context.viewportSize = {64, 64};
    context.nearPlane    = 0.1f;
    context.farPlane     = 10.f;
    context.projection   = perspectiveVulkanRH_ZO(glm::radians(60.f), 1.f, context.nearPlane, context.farPlane);
    PrimitiveSceneCanvas3D spatial(context);
    ScenePrimitivePaint    spatialPaint;
    spatialPaint.mode         = PaintMode::FillAndStroke;
    spatialPaint.color        = Color(0.05f, 0.9f, 0.2f, 1.f);
    spatialPaint.stroke.width = 3.f;
    spatialPaint.depth        = PrimitiveDepthMode::TestOnly;
    spatialPaint.cull         = PrimitiveCullMode::Back;
    spatial.drawDisk({0.f, 0.f, -2.f}, {0.f, 0.f, 1.f}, 0.55f, spatialPaint, 32);
    ScenePrimitivePaint spatialDash;
    spatialDash.mode         = PaintMode::Stroke;
    spatialDash.color        = Color(0.2f, 0.55f, 1.f, 0.75f);
    spatialDash.stroke.width = 4.f;
    spatialDash.stroke.cap   = LineCap::Round;
    spatialDash.stroke.join  = LineJoin::Bevel;
    spatialDash.stroke.dash  = DashPattern{{7.f, 4.f}, 2.f, DashSpace::ScreenPixels};
    spatialDash.depth        = PrimitiveDepthMode::Ignore;
    const std::array<glm::vec3, 3> spatialPoints{{{-0.9f, 0.65f, -2.4f}, {0.f, 0.85f, -2.f}, {0.9f, 0.65f, -2.4f}}};
    spatial.drawPolyline(spatialPoints, false, spatialDash);
    ScenePrimitivePaint clipped = spatialDash;
    clipped.color               = Color(1.f, 0.75f, 0.1f, 1.f);
    clipped.depth               = PrimitiveDepthMode::TestOnly;
    clipped.stroke.dash         = DashPattern{{0.12f, 0.08f}, 0.f, DashSpace::WorldUnits};
    clipped.stroke.widthSpace   = WidthSpace::WorldUnits;
    clipped.stroke.width        = 0.025f;
    spatial.drawLine({-0.75f, -0.7f, -0.05f}, {0.75f, -0.7f, -2.5f}, clipped);
    gfx->begin3DFrameToCanvas(spatialTarget);
    gfx->drawPrimitiveScene(spatial);
    PrimitiveSceneCanvas3D secondSubmission(context);
    ScenePrimitivePaint    secondPaint;
    secondPaint.mode         = PaintMode::Stroke;
    secondPaint.color        = Color(1.f, 0.f, 1.f, 1.f);
    secondPaint.depth        = PrimitiveDepthMode::Ignore;
    secondPaint.stroke.width = 3.f;
    secondSubmission.drawLine({-0.95f, 0.25f, -2.f}, {-0.65f, 0.25f, -2.f}, secondPaint);
    gfx->drawPrimitiveScene(secondSubmission);
    gfx->end3DFrameToCanvas();
    std::unique_ptr<eve::image::ImageData> spatialImage(spatialTarget->newImageData());
    REQUIRE(spatialImage.get() != nullptr);
    const uint8_t *center = pixel(*spatialImage, 32, 32);
    REQUIRE(center[1] > 140);
    REQUIRE(center[1] > center[0] + 60);
    // An asymmetric fixture catches clip-Y inversions that a centered disk hides.
    std::size_t blueAbove = 0;
    std::size_t blueBelow = 0;
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            const auto *sample = pixel(*spatialImage, x, y);
            if (sample[2] > 100 && sample[2] > sample[0] + 60) {
                if (y < 32)
                    ++blueAbove;
                else
                    ++blueBelow;
            }
        }
    }
    REQUIRE(blueAbove > 20u);
    REQUIRE_EQ(blueBelow, 0u);
    writeParityArtifact(*spatialImage, "primitive_3d_depth_fill_stroke", gfx->getBackendName());
}
