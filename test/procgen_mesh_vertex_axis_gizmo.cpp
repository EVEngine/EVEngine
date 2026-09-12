#include "GraphicsParitySupport.h"

#include "graphics/Canvas.h"
#include "graphics/ClipSpace.h"
#include "graphics/Graphics.h"
#include "graphics/editing/PrimitiveGizmoRenderer.h"
#include "image/ImageData.h"
#include "procgen/editing/MeshVertexAxisGizmo.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <cstdint>
#include <memory>

using namespace eve::editing;
using namespace eve::graphics;
using namespace eve::graphics::parity_test;
using namespace eve::procgen;
using namespace eve::procgen_editing;

TEST_CASE("procgen.meshVertexAxisGizmo.picksMovesAndRendersThroughPrimitiveBackend") {
    MeshBuild mesh;
    mesh.addVertex(-0.2f, -0.2f, -2.f, 0.f, 0.f, 1.f, 0.f, 0.f);
    mesh.addVertex(0.2f, -0.2f, -2.f, 0.f, 0.f, 1.f, 1.f, 0.f);
    mesh.addVertex(0.f, 0.2f, -2.f, 0.f, 0.f, 1.f, 0.5f, 1.f);
    mesh.addTriangle(0, 1, 2);
    MeshDeformationSession session;
    REQUIRE(session.initializeResult(mesh).ok());
    REQUIRE(session.selectVerticesSphereResult(0.f, 0.f, -2.f, 1.f).ok());

    MeshVertexAxisGizmoBuilder builder;
    const auto                 snapshot = builder.build(session, 0.8);
    REQUIRE(snapshot.status == eve::editing::Status::Applied);
    CHECK_EQ(snapshot.primitives.size(), std::size_t(3));
    auto picked = builder.pickAxisResult(snapshot, 0.5, 0.0, 0.0, 0.0, 0.0, -1.0);
    REQUIRE(picked.ok());
    CHECK_EQ(picked.value(), "x");
    REQUIRE(session.moveSelectedVerticesResult(0.25f, 0.f, 0.f).ok());
    auto movedCenter = session.selectedVertexCenterResult();
    REQUIRE(movedCenter.ok());
    CHECK(std::abs(movedCenter.value().x - 0.25f) < 1e-6f);

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
    CHECK(rendered.value().segmentCount > 6u);
    gfx->end3DFrameToCanvas();
    std::unique_ptr<eve::image::ImageData> image(target->newImageData());
    REQUIRE(image != nullptr);
    const auto* pixels        = static_cast<const std::uint8_t*>(image->getData());
    std::size_t visiblePixels = 0;
    for (std::size_t offset = 0; offset < image->getSize(); offset += 4u)
        if (pixels[offset] > 12u || pixels[offset + 1u] > 12u || pixels[offset + 2u] > 12u) ++visiblePixels;
    CHECK(visiblePixels > 40u);
}
