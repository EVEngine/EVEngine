#include "graphics/Batcher.h"
#include "graphics/PrimitiveDrawList.h"
#include "graphics/PrimitivePath.h"
#include "graphics/PrimitiveScene.h"
#include "graphics/PrimitiveTessellator.h"
#include "graphics/PrimitiveTypes.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>

using namespace eve::graphics;

TEST_CASE("GraphicsPrimitives.dashPatternOwnsAndValidatesIntervals") {
    DashPattern dash{{4.f, 2.f, 1.f, 3.f}, 0.5f, DashSpace::WorldUnits};
    CHECK(std::fabs(dash.period() - 10.f) < 1e-5f);

    dash.intervals = {1.f, 2.f, 3.f};
    CHECK_THROWS((dash.validate(), false));
    dash.intervals = {1.f, 0.f};
    CHECK_THROWS((dash.validate(), false));
}

TEST_CASE("GraphicsPrimitives.canvas2DRecordsContinuousPolylineDistance") {
    PrimitiveCanvas2D canvas;
    PrimitivePaint    paint;
    paint.mode        = PaintMode::Stroke;
    paint.stroke.dash = DashPattern{{3.f, 2.f}, 0.f, DashSpace::ScreenPixels};
    std::array points{glm::vec2{0.f, 0.f}, glm::vec2{3.f, 4.f}, glm::vec2{6.f, 4.f}};

    canvas.drawPolyline(points, false, paint);

    REQUIRE_EQ(canvas.commands().size(), 1u);
    const auto& command = canvas.commands().front();
    REQUIRE_EQ(command.cumulativeLengths.size(), 3u);
    CHECK(std::fabs(command.cumulativeLengths[1] - 5.f) < 1e-5f);
    CHECK(std::fabs(command.cumulativeLengths[2] - 8.f) < 1e-5f);
    CHECK_EQ(canvas.statistics().segmentCount, 2u);
}

TEST_CASE("GraphicsPrimitives.closed3DPolylineIncludesClosingDistance") {
    SceneDrawContext context;
    context.viewportSize = {1280, 720};
    PrimitiveSceneCanvas3D canvas(context);
    ScenePrimitivePaint    paint;
    paint.mode = PaintMode::Stroke;
    std::array points{glm::vec3{0.f, 0.f, 0.f}, glm::vec3{3.f, 0.f, 0.f}, glm::vec3{3.f, 4.f, 0.f}};

    canvas.drawPolyline(points, true, paint);

    const auto& command = canvas.commands().front();
    REQUIRE_EQ(command.cumulativeWorldLengths.size(), 4u);
    CHECK(std::fabs(command.cumulativeWorldLengths.back() - 12.f) < 1e-5f);
    CHECK_EQ(canvas.statistics().segmentCount, 3u);
}

TEST_CASE("GraphicsPrimitives.canvasStateSnapshotsTransforms") {
    SceneDrawContext context;
    context.viewportSize = {640, 480};
    PrimitiveSceneCanvas3D canvas(context);
    ScenePrimitivePaint    paint;

    canvas.save();
    canvas.concat(glm::translate(glm::mat4(1.f), glm::vec3(2.f, 3.f, 4.f)));
    canvas.drawLine({0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, paint);
    canvas.restore();
    canvas.drawLine({0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, paint);

    REQUIRE_EQ(canvas.commands().size(), 2u);
    CHECK(std::fabs(canvas.commands()[0].transform[3].x - 2.f) < 1e-5f);
    CHECK(std::fabs(canvas.commands()[1].transform[3].x) < 1e-5f);
}

TEST_CASE("GraphicsPrimitives.rejectsInvalidContextAndCapacity") {
    SceneDrawContext context;
    context.viewportSize = {0, 720};
    CHECK_THROWS((PrimitiveSceneCanvas3D(context), false));

    PrimitiveCanvas2D canvas(1);
    PrimitivePaint    paint;
    canvas.drawLine({0.f, 0.f}, {1.f, 1.f}, paint);
    CHECK_THROWS((canvas.drawLine({1.f, 1.f}, {2.f, 2.f}, paint), false));
}

TEST_CASE("GraphicsPrimitives.explicitRecordingReportsBudgetWithoutPartialMutation") {
    PrimitivePaint paint;
    paint.mode = PaintMode::Stroke;
    PrimitiveCanvas2D canvas(1);
    const std::array  first{glm::vec2{0.f, 0.f}, glm::vec2{1.f, 0.f}};
    auto              recorded = canvas.tryDrawPolyline(first, false, paint);
    REQUIRE(recorded.ok());
    const std::array second{glm::vec2{0.f, 1.f}, glm::vec2{1.f, 1.f}};
    auto             rejected = canvas.tryDrawPolyline(second, false, paint);
    CHECK(!rejected.ok());
    REQUIRE(rejected.error() != nullptr);
    CHECK_EQ(static_cast<int>(rejected.error()->code()), static_cast<int>(eve::DiagnosticCode::Failed));
    CHECK_EQ(canvas.commands().size(), 1u);
    CHECK_EQ(canvas.statistics().droppedCommands, 1u);

    SceneDrawContext context;
    context.viewportSize = {640, 480};
    PrimitiveSceneCanvas3D sceneCanvas(context, 1);
    ScenePrimitivePaint    scenePaint;
    scenePaint.mode = PaintMode::Stroke;
    const std::array line{glm::vec3{0.f, 0.f, -2.f}, glm::vec3{1.f, 0.f, -2.f}};
    auto             sceneRecorded = sceneCanvas.tryDrawPolyline(line, false, scenePaint);
    REQUIRE(sceneRecorded.ok());
    auto sceneRejected = sceneCanvas.tryDrawPolyline(line, false, scenePaint);
    CHECK(!sceneRejected.ok());
    CHECK_EQ(sceneCanvas.commands().size(), 1u);
    CHECK_EQ(sceneCanvas.statistics().droppedCommands, 1u);
}

TEST_CASE("GraphicsPrimitives.pathFlattensBezierAndPreservesClosedContour") {
    Path2D path;
    path.moveTo({0.f, 0.f}).quadTo({5.f, 10.f}, {10.f, 0.f}).cubicTo({12.f, -4.f}, {18.f, -4.f}, {20.f, 0.f}).close();

    const auto coarse = path.flatten(2.f);
    const auto fine   = path.flatten(0.05f);
    REQUIRE_EQ(coarse.size(), 1u);
    CHECK(coarse.front().closed);
    CHECK(fine.front().points.size() > coarse.front().points.size());
    CHECK(std::fabs(fine.front().points.front().x) < 1e-5f);
    CHECK(std::fabs(fine.front().points.back().x - 20.f) < 1e-5f);
}

TEST_CASE("GraphicsPrimitives.canvasPathUsesSharedContinuousDistanceModel") {
    Path2D path;
    path.moveTo({0.f, 0.f}).lineTo({4.f, 0.f}).lineTo({4.f, 3.f}).close();
    PrimitiveCanvas2D canvas;
    PrimitivePaint    paint;
    paint.mode = PaintMode::Stroke;
    canvas.drawPath(path, paint);

    REQUIRE_EQ(canvas.commands().size(), 1u);
    CHECK(canvas.commands().front().closed);
    CHECK(std::fabs(canvas.commands().front().cumulativeLengths.back() - 12.f) < 1e-5f);
}

TEST_CASE("GraphicsPrimitives.concaveClosedPathProducesEarClippedFill") {
    Path2D path;
    path.moveTo({0.f, 0.f}).lineTo({8.f, 0.f}).lineTo({8.f, 8.f}).lineTo({4.f, 3.f}).lineTo({0.f, 8.f}).close();
    PrimitivePaint paint;
    paint.mode      = PaintMode::FillAndStroke;
    paint.antialias = false;
    PrimitiveCanvas2D canvas;
    canvas.drawPath(path, paint);
    CHECK_EQ(canvas.triangles().size(), 3u);
    REQUIRE_EQ(canvas.commands().size(), 1u);
    CHECK(canvas.commands().front().closed);
}

TEST_CASE("GraphicsPrimitives.pathFillRulesDistinguishNestedContourWinding") {
    const auto appendSquare = [](Path2D& path, float minimum, float maximum, bool clockwise) {
        if (clockwise)
            path.moveTo({minimum, minimum})
                .lineTo({minimum, maximum})
                .lineTo({maximum, maximum})
                .lineTo({maximum, minimum})
                .close();
        else
            path.moveTo({minimum, minimum})
                .lineTo({maximum, minimum})
                .lineTo({maximum, maximum})
                .lineTo({minimum, maximum})
                .close();
    };
    const auto triangleArea = [](const PrimitiveCanvas2D& canvas) {
        float area = 0.f;
        for (const TriangleCommand2D& triangle : canvas.triangles()) {
            const glm::vec2 ab = triangle.points[1] - triangle.points[0];
            const glm::vec2 ac = triangle.points[2] - triangle.points[0];
            area += std::fabs(ab.x * ac.y - ab.y * ac.x) * 0.5f;
        }
        return area;
    };
    PrimitivePaint fill;
    fill.mode      = PaintMode::Fill;
    fill.antialias = false;

    Path2D evenOdd;
    appendSquare(evenOdd, 0.f, 10.f, false);
    appendSquare(evenOdd, 3.f, 7.f, false);
    evenOdd.setFillRule(PathFillRule::EvenOdd);
    PrimitiveCanvas2D evenOddCanvas;
    evenOddCanvas.drawPath(evenOdd, fill);
    CHECK(std::fabs(triangleArea(evenOddCanvas) - 84.f) < 1e-4f);

    Path2D nonZeroSolid;
    appendSquare(nonZeroSolid, 0.f, 10.f, false);
    appendSquare(nonZeroSolid, 3.f, 7.f, false);
    nonZeroSolid.setFillRule(PathFillRule::NonZero);
    PrimitiveCanvas2D solidCanvas;
    solidCanvas.drawPath(nonZeroSolid, fill);
    CHECK(std::fabs(triangleArea(solidCanvas) - 100.f) < 1e-4f);

    Path2D nonZeroHole;
    appendSquare(nonZeroHole, 0.f, 10.f, false);
    appendSquare(nonZeroHole, 3.f, 7.f, true);
    nonZeroHole.setFillRule(PathFillRule::NonZero);
    PrimitiveCanvas2D holeCanvas;
    holeCanvas.drawPath(nonZeroHole, fill);
    CHECK(std::fabs(triangleArea(holeCanvas) - 84.f) < 1e-4f);
}

TEST_CASE("GraphicsPrimitives.pathRejectsVerbWithoutContour") {
    Path2D path;
    CHECK_THROWS((path.lineTo({1.f, 2.f}), false));
    CHECK_THROWS((path.close(), false));
}

TEST_CASE("GraphicsPrimitives.spatialWireShapesSharePolylineCommands") {
    SceneDrawContext context;
    context.viewportSize = {800, 600};
    PrimitiveSceneCanvas3D canvas(context);
    ScenePrimitivePaint    paint;
    paint.mode        = PaintMode::Stroke;
    paint.stroke.dash = DashPattern{{0.5f, 0.25f}, 0.f, DashSpace::WorldUnits};

    canvas.drawAabb({-1.f, -2.f, -3.f}, {1.f, 2.f, 3.f}, paint);
    CHECK_EQ(canvas.commands().size(), 12u);
    canvas.drawSphere({0.f, 0.f, 0.f}, 2.f, paint, 8);
    CHECK_EQ(canvas.commands().size(), 15u);
    CHECK_EQ(canvas.statistics().segmentCount, 36u);

    for (const auto& command : canvas.commands()) {
        CHECK(command.paint.stroke.dash.has_value());
    }
}

TEST_CASE("GraphicsPrimitives.gridCylinderConeAndArrowProduceFiniteCommands") {
    SceneDrawContext context;
    context.viewportSize = {800, 600};
    PrimitiveSceneCanvas3D canvas(context);
    ScenePrimitivePaint    paint;
    paint.mode = PaintMode::Stroke;

    canvas.drawGrid({0.f, 0.f, 0.f}, {4.f, 0.f, 0.f}, {0.f, 0.f, 3.f}, 4, 3, paint);
    canvas.drawCylinder({0.f, 0.f, 0.f}, {0.f, 2.f, 0.f}, 0.5f, paint, 8);
    canvas.drawCone({0.f, 2.f, 0.f}, {0.f, -1.f, 0.f}, 1.f, 0.5f, paint, 8);
    canvas.drawArrow({0.f, 0.f, 0.f}, {2.f, 0.f, 0.f}, 0.5f, 0.2f, paint);

    CHECK_EQ(canvas.statistics().droppedCommands, 0u);
    CHECK(canvas.statistics().commandCount > 0u);
    for (const auto& command : canvas.commands()) {
        CHECK(std::isfinite(command.cumulativeWorldLengths.back()));
    }
}

TEST_CASE("GraphicsPrimitives.resolve2DStrokeProducesPixelWidthQuad") {
    PrimitiveCanvas2D canvas;
    PrimitivePaint    paint;
    paint.mode         = PaintMode::Stroke;
    paint.stroke.width = 4.f;
    paint.antialias    = false;
    canvas.drawLine({10.f, 20.f}, {30.f, 20.f}, paint);

    const auto triangles = resolvePrimitiveStrokes2D(canvas, {100, 100});
    REQUIRE_EQ(triangles.vertices.size(), 6u);
    CHECK_EQ(triangles.statistics.triangleCount, 2u);
    const float firstY    = triangles.vertices[0].clipPosition.y;
    const float oppositeY = triangles.vertices[2].clipPosition.y;
    CHECK(std::fabs(std::fabs(oppositeY - firstY) - 0.08f) < 1e-5f);
    CHECK_EQ(triangles.statistics.uploadBytes, triangles.vertices.size() * sizeof(PrimitiveTriangleVertex));
}

TEST_CASE("GraphicsPrimitives.coverageAaEmitsTransparentOnePixelFringe") {
    PrimitivePaint paint;
    paint.mode         = PaintMode::Stroke;
    paint.stroke.width = 4.f;
    paint.antialias    = true;
    PrimitiveCanvas2D canvas;
    canvas.drawLine({10.f, 20.f}, {30.f, 20.f}, paint);
    const auto resolved = resolvePrimitiveStrokes2D(canvas, {100, 100});
    CHECK_EQ(resolved.vertices.size(), 18u);
    CHECK(std::any_of(resolved.vertices.begin(), resolved.vertices.end(),
                      [](const PrimitiveTriangleVertex& vertex) { return vertex.color.a == 0.f; }));
    CHECK(std::any_of(resolved.vertices.begin(), resolved.vertices.end(),
                      [](const PrimitiveTriangleVertex& vertex) { return vertex.color.a == 1.f; }));

    Batcher batch;
    batch.addTriangle({0.f, 0.f}, {1.f, 0.f}, {0.f, 1.f}, Color(1.f), Color(1.f, 1.f, 1.f, 0.5f),
                      Color(1.f, 1.f, 1.f, 0.f));
    REQUIRE_EQ(batch.vertices().size(), 3u);
    CHECK_EQ(batch.vertices()[1].color.a, 0.5f);
    CHECK_EQ(batch.vertices()[2].color.a, 0.f);
}

TEST_CASE("GraphicsPrimitives.filledContourCoverageAaUsesVertexAlphaFringe") {
    PrimitiveCanvas2D canvas;
    PrimitivePaint    fill;
    fill.mode      = PaintMode::Fill;
    fill.antialias = true;
    canvas.drawRect({10.f, 10.f}, {30.f, 30.f}, fill);
    CHECK_EQ(canvas.triangles().size(), 10u);
    const auto resolved = resolvePrimitiveStrokes2D(canvas, {100, 100});
    CHECK(std::any_of(resolved.vertices.begin(), resolved.vertices.end(),
                      [](const PrimitiveTriangleVertex& vertex) { return vertex.color.a == 0.f; }));
    CHECK(std::any_of(resolved.vertices.begin(), resolved.vertices.end(),
                      [](const PrimitiveTriangleVertex& vertex) { return vertex.color.a == 1.f; }));
}

TEST_CASE("GraphicsPrimitives.resolve2DPreservesBlendAndSubmissionOrder") {
    PrimitiveCanvas2D canvas;
    PrimitivePaint    opaque;
    opaque.mode      = PaintMode::Stroke;
    opaque.blend     = BlendMode::Opaque;
    opaque.antialias = false;
    canvas.drawLine({0.f, 0.f}, {10.f, 0.f}, opaque);
    PrimitivePaint additive = opaque;
    additive.blend          = BlendMode::Additive;
    canvas.drawLine({0.f, 2.f}, {10.f, 2.f}, additive);
    const auto resolved = resolvePrimitiveStrokes2D(canvas, {100, 100});
    REQUIRE_EQ(resolved.batches2D.size(), 2u);
    CHECK_EQ(static_cast<int>(resolved.batches2D[0].blend), static_cast<int>(BlendMode::Opaque));
    CHECK_EQ(static_cast<int>(resolved.batches2D[1].blend), static_cast<int>(BlendMode::Additive));
    CHECK_EQ(resolved.batches2D[0].sequence, 0u);
    CHECK_EQ(resolved.batches2D[1].sequence, 1u);
}

TEST_CASE("GraphicsPrimitives.resolve2DDashContinuesAcrossPolylineSegments") {
    PrimitiveCanvas2D canvas;
    PrimitivePaint    paint;
    paint.mode         = PaintMode::Stroke;
    paint.stroke.width = 2.f;
    paint.antialias    = false;
    paint.stroke.dash  = DashPattern{{6.f, 4.f}, 0.f, DashSpace::ScreenPixels};
    const std::array points{glm::vec2{0.f, 10.f}, glm::vec2{5.f, 10.f}, glm::vec2{10.f, 10.f}};
    canvas.drawPolyline(points, false, paint);

    const auto triangles = resolvePrimitiveStrokes2D(canvas, {100, 100});
    REQUIRE_EQ(triangles.vertices.size(), 12u);
    float maximumDistance = 0.f;
    for (const auto& vertex : triangles.vertices) {
        maximumDistance = std::max(maximumDistance, vertex.pathDistance);
    }
    CHECK(std::fabs(maximumDistance - 6.f) < 1e-5f);
}

TEST_CASE("GraphicsPrimitives.resolve2DDashPhaseSelectsGapAtStart") {
    PrimitiveCanvas2D canvas;
    PrimitivePaint    paint;
    paint.mode        = PaintMode::Stroke;
    paint.stroke.dash = DashPattern{{4.f, 4.f}, 4.f, DashSpace::ScreenPixels};
    paint.antialias   = false;
    canvas.drawLine({0.f, 0.f}, {8.f, 0.f}, paint);

    const auto triangles = resolvePrimitiveStrokes2D(canvas, {100, 100});
    REQUIRE_EQ(triangles.vertices.size(), 6u);
    CHECK(std::fabs(triangles.vertices.front().pathDistance - 4.f) < 1e-5f);
    CHECK(std::fabs(triangles.vertices[1].pathDistance - 8.f) < 1e-5f);
}

TEST_CASE("GraphicsPrimitives.rectangleHonorsFillStrokeAndCombinedModes") {
    PrimitiveCanvas2D canvas;
    PrimitivePaint    paint;
    paint.mode      = PaintMode::Fill;
    paint.antialias = false;
    canvas.drawRect({10.f, 20.f}, {30.f, 40.f}, paint);
    CHECK_EQ(canvas.triangles().size(), 2u);
    CHECK(canvas.commands().empty());

    canvas.reset();
    paint.mode = PaintMode::Stroke;
    canvas.drawRect({10.f, 20.f}, {30.f, 40.f}, paint);
    CHECK(canvas.triangles().empty());
    REQUIRE_EQ(canvas.commands().size(), 1u);
    CHECK(canvas.commands().front().closed);

    canvas.reset();
    paint.mode = PaintMode::FillAndStroke;
    canvas.drawRect({10.f, 20.f}, {30.f, 40.f}, paint);
    CHECK_EQ(canvas.triangles().size(), 2u);
    CHECK_EQ(canvas.commands().size(), 1u);
}

TEST_CASE("GraphicsPrimitives.circleEllipseAndArcGenerateRealFillTriangles") {
    PrimitiveCanvas2D canvas;
    PrimitivePaint    fill;
    fill.mode      = PaintMode::Fill;
    fill.antialias = false;
    canvas.drawCircle({50.f, 50.f}, 20.f, fill, 8);
    CHECK_EQ(canvas.triangles().size(), 8u);
    CHECK(canvas.commands().empty());

    PrimitivePaint combined;
    combined.mode      = PaintMode::FillAndStroke;
    combined.antialias = false;
    canvas.drawEllipse({20.f, 20.f}, {10.f, 5.f}, combined, 8);
    CHECK_EQ(canvas.triangles().size(), 16u);
    CHECK_EQ(canvas.commands().size(), 1u);

    canvas.drawArc({20.f, 20.f}, {8.f, 8.f}, 0.f, glm::pi<float>(), combined, 4);
    CHECK_EQ(canvas.triangles().size(), 20u);
    CHECK_EQ(canvas.commands().size(), 2u);
}

TEST_CASE("GraphicsPrimitives.unitCircleGeometryCacheReportsReuse") {
    PrimitivePaint fill;
    fill.mode = PaintMode::Fill;
    PrimitiveCanvas2D canvas;
    canvas.drawCircle({0.f, 0.f}, 1.f, fill, 37);
    const std::size_t firstHits = canvas.statistics().cacheHits;
    canvas.drawEllipse({2.f, 0.f}, {2.f, 1.f}, fill, 37);
    CHECK_EQ(canvas.statistics().cacheHits, firstHits + 1u);

    SceneDrawContext context;
    context.viewportSize = {640, 480};
    PrimitiveSceneCanvas3D sceneCanvas(context);
    ScenePrimitivePaint    stroke;
    stroke.mode = PaintMode::Stroke;
    sceneCanvas.drawSphere({0.f, 0.f, -4.f}, 1.f, stroke, 41);
    CHECK(sceneCanvas.statistics().cacheHits >= 2u);
}

TEST_CASE("GraphicsPrimitives.radialQualityAndAdaptiveLodResolveDeterministically") {
    CHECK_EQ(resolveRadialSegments({PrimitiveQuality::Low, 0.75f, 0}, 100.f), 16u);
    CHECK_EQ(resolveRadialSegments({PrimitiveQuality::Medium, 0.75f, 0}, 100.f), 32u);
    CHECK_EQ(resolveRadialSegments({PrimitiveQuality::High, 0.75f, 0}, 100.f), 64u);
    CHECK_EQ(resolveRadialSegments({PrimitiveQuality::Adaptive, 0.75f, 23}, 100.f), 23u);

    SceneDrawContext context;
    context.viewportSize = {800, 600};
    context.projection   = glm::perspective(glm::radians(60.f), 4.f / 3.f, 0.1f, 100.f);
    ScenePrimitivePaint stroke;
    stroke.mode = PaintMode::Stroke;
    const RadialTessellation adaptive{PrimitiveQuality::Adaptive, 0.5f, 0};
    PrimitiveSceneCanvas3D   nearCanvas(context);
    nearCanvas.drawSphere({0.f, 0.f, -2.f}, 1.f, stroke, adaptive);
    PrimitiveSceneCanvas3D farCanvas(context);
    farCanvas.drawSphere({0.f, 0.f, -20.f}, 1.f, stroke, adaptive);
    REQUIRE(!nearCanvas.commands().empty());
    REQUIRE(!farCanvas.commands().empty());
    CHECK(nearCanvas.commands().front().points.size() > farCanvas.commands().front().points.size());
}

TEST_CASE("GraphicsPrimitives.resolve2DCombinesFillAndStrokeTriangles") {
    PrimitiveCanvas2D canvas;
    PrimitivePaint    paint;
    paint.mode         = PaintMode::FillAndStroke;
    paint.stroke.width = 2.f;
    paint.antialias    = false;
    canvas.drawRect({10.f, 10.f}, {30.f, 30.f}, paint);

    const auto resolved = resolvePrimitiveStrokes2D(canvas, {100, 100});
    CHECK_EQ(resolved.statistics.triangleCount, 14u);
    CHECK_EQ(resolved.vertices.size(), 42u);
}

TEST_CASE("GraphicsPrimitives.strokeCapsProduceDistinctEndpointGeometry") {
    PrimitivePaint paint;
    paint.mode         = PaintMode::Stroke;
    paint.stroke.width = 4.f;
    paint.antialias    = false;

    PrimitiveCanvas2D butt;
    paint.stroke.cap = LineCap::Butt;
    butt.drawLine({10.f, 20.f}, {30.f, 20.f}, paint);
    CHECK_EQ(resolvePrimitiveStrokes2D(butt, {100, 100}).vertices.size(), 6u);

    PrimitiveCanvas2D square;
    paint.stroke.cap = LineCap::Square;
    square.drawLine({10.f, 20.f}, {30.f, 20.f}, paint);
    CHECK_EQ(resolvePrimitiveStrokes2D(square, {100, 100}).vertices.size(), 18u);

    PrimitiveCanvas2D round;
    paint.stroke.cap = LineCap::Round;
    round.drawLine({10.f, 20.f}, {30.f, 20.f}, paint);
    CHECK_EQ(resolvePrimitiveStrokes2D(round, {100, 100}).vertices.size(), 54u);
}

TEST_CASE("GraphicsPrimitives.strokeJoinsFillPolylineOuterCorner") {
    const std::array points{glm::vec2{10.f, 10.f}, glm::vec2{30.f, 10.f}, glm::vec2{30.f, 30.f}};
    PrimitivePaint   paint;
    paint.mode         = PaintMode::Stroke;
    paint.stroke.width = 6.f;
    paint.antialias    = false;
    paint.stroke.join  = LineJoin::Bevel;
    PrimitiveCanvas2D bevel;
    bevel.drawPolyline(points, false, paint);
    CHECK_EQ(resolvePrimitiveStrokes2D(bevel, {100, 100}).vertices.size(), 15u);

    paint.stroke.join = LineJoin::Round;
    PrimitiveCanvas2D round;
    round.drawPolyline(points, false, paint);
    CHECK(resolvePrimitiveStrokes2D(round, {100, 100}).vertices.size() > 15u);
}

TEST_CASE("GraphicsPrimitives.resolve3DScreenStrokeClipsNearPlane") {
    SceneDrawContext context;
    context.viewportSize = {800, 600};
    context.nearPlane    = 0.1f;
    context.farPlane     = 100.f;
    context.projection   = glm::perspective(glm::radians(60.f), 4.f / 3.f, 0.1f, 100.f);
    PrimitiveSceneCanvas3D canvas(context);
    ScenePrimitivePaint    paint;
    paint.mode              = PaintMode::Stroke;
    paint.stroke.width      = 3.f;
    paint.antialias         = false;
    paint.stroke.widthSpace = WidthSpace::ScreenPixels;
    canvas.drawLine({-1.f, 0.f, -1.f}, {1.f, 0.f, 0.f}, paint);
    canvas.drawLine({-1.f, 1.f, 1.f}, {1.f, 1.f, 2.f}, paint);

    const auto triangles = resolvePrimitiveStrokes3D(canvas);
    REQUIRE_EQ(triangles.vertices.size(), 6u);
    for (const auto& vertex : triangles.vertices) {
        CHECK(std::isfinite(vertex.clipPosition.x));
        CHECK(std::isfinite(vertex.clipPosition.w));
        CHECK(vertex.clipPosition.w > 0.f);
    }
}

TEST_CASE("GraphicsPrimitives.resolve3DWorldStrokeExpandsInViewSpace") {
    SceneDrawContext context;
    context.viewportSize = {800, 600};
    context.projection   = glm::perspective(glm::radians(60.f), 4.f / 3.f, 0.1f, 100.f);
    PrimitiveSceneCanvas3D canvas(context);
    ScenePrimitivePaint    paint;
    paint.mode              = PaintMode::Stroke;
    paint.stroke.width      = 0.2f;
    paint.antialias         = false;
    paint.stroke.widthSpace = WidthSpace::WorldUnits;
    canvas.drawLine({-1.f, 0.f, -4.f}, {1.f, 0.f, -4.f}, paint);

    const auto triangles = resolvePrimitiveStrokes3D(canvas);
    REQUIRE_EQ(triangles.vertices.size(), 6u);
    CHECK(std::fabs(triangles.vertices[0].clipPosition.y - triangles.vertices[2].clipPosition.y) > 1e-5f);
}

TEST_CASE("GraphicsPrimitives.resolve3DStrokeHonorsCapsAndJoins") {
    SceneDrawContext context;
    context.viewportSize = {800, 600};
    context.projection   = glm::perspective(glm::radians(60.f), 4.f / 3.f, 0.1f, 100.f);
    ScenePrimitivePaint paint;
    paint.mode              = PaintMode::Stroke;
    paint.stroke.width      = 4.f;
    paint.stroke.widthSpace = WidthSpace::ScreenPixels;
    paint.antialias         = false;

    PrimitiveSceneCanvas3D square(context);
    paint.stroke.cap = LineCap::Square;
    square.drawLine({-1.f, 0.f, -4.f}, {1.f, 0.f, -4.f}, paint);
    CHECK_EQ(resolvePrimitiveStrokes3D(square).vertices.size(), 18u);

    PrimitiveSceneCanvas3D round(context);
    paint.stroke.cap = LineCap::Round;
    round.drawLine({-1.f, 0.f, -4.f}, {1.f, 0.f, -4.f}, paint);
    CHECK_EQ(resolvePrimitiveStrokes3D(round).vertices.size(), 54u);

    PrimitiveSceneCanvas3D bevel(context);
    paint.stroke.cap  = LineCap::Butt;
    paint.stroke.join = LineJoin::Bevel;
    const std::array points{glm::vec3{-1.f, 0.f, -4.f}, glm::vec3{0.f, 0.f, -4.f}, glm::vec3{0.f, 1.f, -4.f}};
    bevel.drawPolyline(points, false, paint);
    CHECK_EQ(resolvePrimitiveStrokes3D(bevel).vertices.size(), 15u);
}

TEST_CASE("GraphicsPrimitives.resolve3DScreenDashUsesProjectedPixelDistance") {
    SceneDrawContext context;
    context.viewportSize = {800, 600};
    context.projection   = glm::perspective(glm::radians(60.f), 4.f / 3.f, 0.1f, 100.f);
    PrimitiveSceneCanvas3D canvas(context);
    ScenePrimitivePaint    paint;
    paint.mode         = PaintMode::Stroke;
    paint.stroke.width = 2.f;
    paint.stroke.dash  = DashPattern{{10.f, 10.f}, 0.f, DashSpace::ScreenPixels};
    canvas.drawLine({-1.f, 0.f, -4.f}, {1.f, 0.f, -4.f}, paint);

    const auto triangles = resolvePrimitiveStrokes3D(canvas);
    CHECK(triangles.vertices.size() > 6u);
    CHECK_EQ(triangles.vertices.size() % 6u, 0u);
}

TEST_CASE("GraphicsPrimitives.batcherAcceptsResolvedArbitraryTriangles") {
    Batcher batch;
    batch.addTriangle({1.f, 2.f}, {7.f, 3.f}, {2.f, 9.f}, Color(1.f, 0.f, 0.f, 1.f));
    REQUIRE_EQ(batch.vertices().size(), 3u);
    CHECK(std::fabs(batch.vertices()[1].pos.x - 7.f) < 1e-5f);
    batch.toNDC(10, 10);
    CHECK(std::fabs(batch.vertices()[0].pos.x - (-0.8f)) < 1e-5f);
}

TEST_CASE("GraphicsPrimitives.persistentSceneDetectsStaleAndReusedSlots") {
    PrimitiveScene        scene;
    PrimitiveDescriptor3D descriptor;
    descriptor.geometry = PrimitiveSphere3D{{0.f, 0.f, 0.f}, 1.f, 8};
    auto added          = scene.add(descriptor);
    REQUIRE(added.ok());
    const PrimitiveHandle first = added.value();
    CHECK(scene.tryGet(first) != nullptr);

    auto removed = scene.remove(first);
    REQUIRE(removed.ok());
    CHECK(scene.isStale(first));
    auto addedAgain = scene.add(descriptor);
    REQUIRE(addedAgain.ok());
    const PrimitiveHandle second = addedAgain.value();
    CHECK_EQ(second.index(), first.index());
    CHECK(second.generation() != first.generation());

    auto staleUpdate = scene.update(first, descriptor);
    CHECK(!staleUpdate.ok());
    CHECK_EQ(static_cast<int>(staleUpdate.error()->code()), static_cast<int>(eve::DiagnosticCode::StaleHandle));
}

TEST_CASE("GraphicsPrimitives.persistentSceneProjectsVisibleDescriptorsInSlotOrder") {
    PrimitiveScene        scene;
    PrimitiveDescriptor3D hidden;
    hidden.geometry   = PrimitiveAabb3D{glm::vec3(-1.f), glm::vec3(1.f)};
    hidden.visible    = false;
    auto hiddenHandle = scene.add(hidden);
    REQUIRE(hiddenHandle.ok());

    PrimitiveDescriptor3D arrow;
    arrow.geometry   = PrimitiveArrow3D{{0.f, 0.f, 0.f}, {0.f, 2.f, 0.f}, 0.4f, 0.2f};
    arrow.transform  = glm::translate(glm::mat4(1.f), {4.f, 0.f, 0.f});
    auto arrowHandle = scene.add(arrow);
    REQUIRE(arrowHandle.ok());

    SceneDrawContext context;
    context.viewportSize = {800, 600};
    PrimitiveSceneCanvas3D canvas(context);
    scene.render(canvas);
    CHECK(!canvas.commands().empty());
    for (const auto& command : canvas.commands()) {
        CHECK(std::fabs(command.transform[3].x - 4.f) < 1e-5f);
    }

    scene.clear();
    CHECK_EQ(scene.size(), 0u);
    CHECK(scene.isStale(arrowHandle.value()));
}

TEST_CASE("GraphicsPrimitives.persistentHandlesAreQualifiedByOwningScene") {
    PrimitiveScene        firstScene;
    PrimitiveScene        secondScene;
    PrimitiveDescriptor3D descriptor;
    descriptor.geometry = PrimitiveSphere3D{};
    auto first          = firstScene.add(descriptor);
    auto second         = secondScene.add(descriptor);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    CHECK(first.value().owner() != second.value().owner());
    CHECK(secondScene.tryGet(first.value()) == nullptr);
    CHECK(!secondScene.update(first.value(), descriptor).ok());
}

TEST_CASE("GraphicsPrimitives.sceneBatchUpdateIsAtomicOnStaleHandle") {
    PrimitiveScene        scene;
    PrimitiveDescriptor3D firstDescriptor;
    firstDescriptor.geometry = PrimitiveSphere3D{{1.f, 0.f, 0.f}, 1.f, 8};
    PrimitiveDescriptor3D secondDescriptor;
    secondDescriptor.geometry = PrimitiveSphere3D{{2.f, 0.f, 0.f}, 1.f, 8};
    auto first                = scene.add(firstDescriptor);
    auto second               = scene.add(secondDescriptor);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    REQUIRE(scene.remove(second.value()).ok());

    PrimitiveDescriptor3D replacement = firstDescriptor;
    replacement.visible               = false;
    const std::array updates{PrimitiveBatchUpdate{first.value(), replacement},
                             PrimitiveBatchUpdate{second.value(), replacement}};
    auto             result = scene.updateMany(updates);
    CHECK(!result.ok());
    REQUIRE(scene.tryGet(first.value()) != nullptr);
    CHECK(scene.tryGet(first.value())->visible);
}

TEST_CASE("GraphicsPrimitives.persistentSceneRejectsInvalidGeometryAtomically") {
    PrimitiveScene        scene;
    PrimitiveDescriptor3D valid;
    valid.geometry = PrimitiveSphere3D{{0.f, 0.f, 0.f}, 1.f, 8};
    auto handle    = scene.add(valid);
    REQUIRE(handle.ok());

    PrimitiveDescriptor3D invalid = valid;
    invalid.geometry              = PrimitiveSphere3D{{0.f, 0.f, 0.f}, -1.f, 8};
    auto update                   = scene.update(handle.value(), invalid);
    CHECK(!update.ok());
    REQUIRE(scene.tryGet(handle.value()) != nullptr);
    const auto* sphere = std::get_if<PrimitiveSphere3D>(&scene.tryGet(handle.value())->geometry);
    REQUIRE(sphere != nullptr);
    CHECK_EQ(sphere->radius, 1.f);

    auto add = scene.add(invalid);
    CHECK(!add.ok());
    CHECK_EQ(scene.size(), 1u);
}
