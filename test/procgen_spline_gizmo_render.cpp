#include "GraphicsParitySupport.h"

#include "graphics/Canvas.h"
#include "graphics/ClipSpace.h"
#include "graphics/Graphics.h"
#include "graphics/editing/PrimitiveGizmoRenderer.h"
#include "image/ImageData.h"
#include "procgen/editing/SplinePathDocument.h"
#include "procgen/editing/SplinePathGizmo.h"
#include "zeroerr/unittest.h"

#include <cstdint>
#include <memory>

using namespace eve::editing;
using namespace eve::graphics;
using namespace eve::graphics::parity_test;
using namespace eve::procgen_editing;

namespace {

SplinePathControlPoint gizmoPoint(const char* id, std::int64_t order, double x, double y) {
    SplinePathControlPoint point;
    point.id    = StableId(id);
    point.order = order;
    point.x     = x;
    point.y     = y;
    point.z     = -2.0;
    return point;
}

}  // namespace

TEST_CASE("procgen.splineGizmo.rendersThroughPrimitiveBackend") {
    SplinePathDocument document("gpu-spline-gizmo");
    REQUIRE(document.applyDomainOperation(document.makeSetSettings({"bezier", false}).value()).ok());
    auto left = gizmoPoint("left", 0, -0.7, -0.35);
    left.outX = 0.45;
    left.outY = 0.8;
    auto right = gizmoPoint("right", 1, 0.7, 0.35);
    right.inX = -0.45;
    right.inY = -0.8;
    REQUIRE(document.applyDomainOperation(document.makeSetPoint(left).value()).ok());
    REQUIRE(document.applyDomainOperation(document.makeSetPoint(right).value()).ok());

    SplineGizmoStyle style;
    style.showPointLabels = false;  // Text labels belong to the viewport's 2D overlay pass.
    const auto snapshot = SplinePathGizmoBuilder().build(document, 24, 4096, style);
    REQUIRE(snapshot.status == EditorStatus::Applied);
    REQUIRE(snapshot.primitives.size() > 20u);

    Graphics* gfx = headlessGraphics();
    REQUIRE(gfx != nullptr);
    Canvas* target = gfx->newCanvas(128, 128);
    REQUIRE(target != nullptr);
    SceneDrawContext context;
    context.viewportSize = {128, 128};
    context.nearPlane    = 0.1f;
    context.farPlane     = 10.f;
    context.projection   = perspectiveVulkanRH_ZO(glm::radians(60.f), 1.f, context.nearPlane, context.farPlane);

    gfx->begin3DFrameToCanvas(target);
    auto rendered = eve::graphics_editing::PrimitiveGizmoRenderer().render(snapshot, context, *gfx);
    REQUIRE(rendered.ok());
    CHECK(rendered.value().segmentCount > 10u);
    gfx->end3DFrameToCanvas();

    std::unique_ptr<eve::image::ImageData> image(target->newImageData());
    REQUIRE(image != nullptr);
    const auto* pixels = static_cast<const std::uint8_t*>(image->getData());
    std::size_t visiblePixels = 0;
    for (std::size_t offset = 0; offset < image->getSize(); offset += 4u)
        if (pixels[offset] > 12u || pixels[offset + 1u] > 12u || pixels[offset + 2u] > 12u) ++visiblePixels;
    CHECK(visiblePixels > 80u);
}
