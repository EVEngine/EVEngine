// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "module": "graphics_multiscale",
  "default": "render-window"
}
*/
// clang-format on
#include <array>
#include <cmath>
#include <cstdio>
#include <glm/mat4x4.hpp>
#include <simplesquirrel/simplesquirrel.hpp>
#include <span>
#include <string>
#include <vector>
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/Shader.h"
#include "graphics/ShaderImageUpdate.h"
#include "graphics/ShaderResources.h"
#include "graphics/multiscale/AngularView.h"
#include "graphics/multiscale/FarFlux.h"
#include "graphics/multiscale/MultiscaleView.h"
#include "graphics/multiscale/SphereView.h"
#include "graphics/multiscale/shaders/sphere_batch_frag_spv.inc"
#include "graphics/multiscale/shaders/sphere_batch_vert_spv.inc"
#include "graphics/multiscale/shaders/sphere_frag_spv.inc"
#include "graphics/multiscale/shaders/sphere_vert_spv.inc"
#include "spatial/multiscale/CellPosition.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

TEST_CASE("graphics.multiscale.resourceBatchUsesIndependentRecordsAndCheckedInstanceRanges") {
    auto*                       window   = eve::window::Window::create();
    auto*                       graphics = eve::graphics::Graphics::create();
    eve::window::WindowSettings settings;
    settings.width  = 320;
    settings.height = 180;
    REQUIRE(window->setWindowSettings(settings));
    graphics->setScreenReadbackEnabled(true);
    graphics->setBackgroundColor({0.02f, 0.03f, 0.05f, 1.f});
    // Compare after the same presentation transform, rather than interpreting
    // display readback as the shader's linear RGB output.
    auto reference = eve::graphics::multiscale::SphereView::build(*graphics);
    REQUIRE(reference.ok());
    std::array<eve::spatial::CellPosition, 1> referenceCenters{};
    referenceCenters[0].cell[2] = 10;
    const std::array<eve::graphics::multiscale::SphereAppearance, 1> referenceAppearance{
        {{2, 900, {0.8f, 0.1f, 0.1f}}}};
    eve::graphics::multiscale::AngularView referenceView;
    referenceView.cellExponent = 900;
    referenceView.aspect       = 320.0 / 180.0;
    auto referenceFrame =
        eve::graphics::multiscale::SphereFrame::build(referenceCenters, referenceAppearance, {}, referenceView, 1);
    REQUIRE(referenceFrame.ok());
    REQUIRE(reference.value()->render(referenceFrame.value()).ok());
    const auto expectedNear = graphics->getPixel(160, 90);
    REQUIRE(expectedNear.r > expectedNear.b + 0.3f);
    reference.value().reset();
    auto uploaded = eve::graphics::multiscale::SphereBatch::prepare(*graphics, referenceFrame.value());
    REQUIRE(uploaded.ok());
    { auto retiredCpuFrame = std::move(referenceFrame.value()); }
    auto movedFrame = eve::graphics::multiscale::SphereBatch::prepare(*graphics, referenceFrame.value());
    REQUIRE(!movedFrame.ok());
    for (int frame = 0; frame < 3; ++frame) {
        REQUIRE(uploaded.value()->draw().ok());
        graphics->present();
        REQUIRE(std::abs(graphics->getPixel(160, 90).r - expectedNear.r) < 0.01f);
        REQUIRE(std::abs(graphics->getPixel(160, 90).b - expectedNear.b) < 0.01f);
    }
    auto changedAppearance     = referenceAppearance;
    changedAppearance[0].color = {0.1f, 0.1f, 0.8f};
    auto changedFrame =
        eve::graphics::multiscale::SphereFrame::build(referenceCenters, changedAppearance, {}, referenceView, 1);
    REQUIRE(changedFrame.ok());
    REQUIRE(uploaded.value()->update(changedFrame.value()).ok());
    REQUIRE(uploaded.value()->draw().ok());
    graphics->present();
    REQUIRE(std::abs(graphics->getPixel(160, 90).b - expectedNear.r) < 0.01f);
    auto blankFrame = eve::graphics::multiscale::SphereFrame::build({}, {}, {}, referenceView, 1);
    REQUIRE(blankFrame.ok());
    REQUIRE(uploaded.value()->update(blankFrame.value()).ok());
    REQUIRE(uploaded.value()->draw().ok());
    graphics->present();
    REQUIRE(graphics->getPixel(160, 90).b < expectedNear.r * 0.5f);
    REQUIRE(uploaded.value()->update(changedFrame.value()).ok());
    std::array<eve::spatial::CellPosition, 2>                  tooManyCenters{referenceCenters[0], referenceCenters[0]};
    std::array<eve::graphics::multiscale::SphereAppearance, 2> tooManyAppearance{referenceAppearance[0],
                                                                                 referenceAppearance[0]};
    auto                                                       tooMany =
        eve::graphics::multiscale::SphereFrame::build(tooManyCenters, tooManyAppearance, {}, referenceView, 2);
    REQUIRE(tooMany.ok());
    REQUIRE(!uploaded.value()->update(tooMany.value()).ok());
    REQUIRE(!uploaded.value()->update(referenceFrame.value()).ok());
    REQUIRE(uploaded.value()->draw().ok());
    REQUIRE(!uploaded.value()->update(blankFrame.value()).ok());  // Active pass must reject before writes.
    graphics->present();
    REQUIRE(std::abs(graphics->getPixel(160, 90).b - expectedNear.r) < 0.01f);
    uploaded.value().reset();
    // The final record starts a second texture row; it must occlude all preceding records.
    std::vector<eve::spatial::CellPosition>                  rowCenters(513);
    std::vector<eve::graphics::multiscale::SphereAppearance> rowAppearance(513, {20, 900, {0.1f, 0.1f, 0.8f}});
    for (auto& center : rowCenters) center.cell[2] = 100;
    rowCenters.back().cell[2] = 10;
    rowAppearance.back()      = {2, 900, {0.8f, 0.1f, 0.1f}};
    auto rowFrame = eve::graphics::multiscale::SphereFrame::build(rowCenters, rowAppearance, {}, referenceView, 513);
    REQUIRE(rowFrame.ok());
    auto rowBatch = eve::graphics::multiscale::SphereBatch::prepare(*graphics, rowFrame.value());
    REQUIRE(rowBatch.ok());
    REQUIRE(rowBatch.value()->draw().ok());
    graphics->present();
    REQUIRE(std::abs(graphics->getPixel(160, 90).r - expectedNear.r) < 0.01f);
    REQUIRE(std::abs(graphics->getPixel(160, 90).b - expectedNear.b) < 0.01f);
    rowBatch.value().reset();
    // One thousand records form an exact two-row image with a 2000-texel row.
    rowCenters.assign(1000, referenceCenters[0]);
    rowAppearance.assign(1000, {20, 900, {0.1f, 0.1f, 0.8f}});
    for (auto& center : rowCenters) center.cell[2] = 100;
    rowCenters.back().cell[2] = 10;
    rowAppearance.back()      = {2, 900, {0.8f, 0.1f, 0.1f}};
    auto exactFrame = eve::graphics::multiscale::SphereFrame::build(rowCenters, rowAppearance, {}, referenceView, 1000);
    REQUIRE(exactFrame.ok());
    auto exactBatch = eve::graphics::multiscale::SphereBatch::prepare(*graphics, exactFrame.value());
    REQUIRE(exactBatch.ok());
    REQUIRE(exactBatch.value()->draw().ok());
    graphics->present();
    REQUIRE(std::abs(graphics->getPixel(160, 90).r - expectedNear.r) < 0.01f);
    rowAppearance.back() = {2, 900, {0.1f, 0.1f, 0.8f}};
    auto exactUpdate = eve::graphics::multiscale::SphereFrame::build(rowCenters, rowAppearance, {}, referenceView, 1000);
    REQUIRE(exactUpdate.ok());
    REQUIRE(exactBatch.value()->update(exactUpdate.value()).ok());
    REQUIRE(exactBatch.value()->draw().ok());
    graphics->present();
    REQUIRE(std::abs(graphics->getPixel(160, 90).b - expectedNear.r) < 0.01f);
    exactBatch.value().reset();
    // Internal workers must preserve candidate order when most inputs are culled.
    rowCenters.assign(32768, referenceCenters[0]);
    rowAppearance.assign(32768, {2, 900, {0.1f, 0.1f, 0.8f}});
    for (auto& center : rowCenters) center.cell[0] = 100;
    rowCenters.back().cell[0] = 0;
    rowAppearance.back()      = {2, 900, {0.8f, 0.1f, 0.1f}};
    auto parallelFrame =
        eve::graphics::multiscale::SphereFrame::build(rowCenters, rowAppearance, {}, referenceView, 32768);
    REQUIRE(parallelFrame.ok());
    auto parallelBatch = eve::graphics::multiscale::SphereBatch::prepare(*graphics, parallelFrame.value());
    REQUIRE(parallelBatch.ok());
    REQUIRE(parallelBatch.value()->draw().ok());
    graphics->present();
    REQUIRE(std::abs(graphics->getPixel(160, 90).r - expectedNear.r) < 0.01f);
    parallelBatch.value().reset();
    rowAppearance[100].color             = {-1.f, 0.f, 0.f};
    rowAppearance[20000].radiusMantissa = -1;
    auto parallelInvalid =
        eve::graphics::multiscale::SphereFrame::build(rowCenters, rowAppearance, {}, referenceView, 32768);
    REQUIRE(!parallelInvalid.ok());
    REQUIRE(parallelInvalid.status().describe().find("Invalid opaque linear color") != std::string::npos);
    auto emptyFrame = eve::graphics::multiscale::SphereFrame::build({}, {}, {}, referenceView, 1);
    REQUIRE(emptyFrame.ok());
    auto emptyBatch = eve::graphics::multiscale::SphereBatch::prepare(*graphics, emptyFrame.value());
    REQUIRE(emptyBatch.ok());
    REQUIRE(emptyBatch.value()->draw().ok());
    graphics->present();
    REQUIRE(graphics->getPixel(160, 90).r < expectedNear.r * 0.5f);
    emptyBatch.value().reset();
    auto* shader = graphics->newMeshShaderFromSpv({sphere_vert_spv, sphere_vert_spv + sphere_vert_spv_count},
                                                  {sphere_frag_spv, sphere_frag_spv + sphere_frag_spv_count});
    REQUIRE(shader != nullptr);
    auto surface = graphics->configureMeshShaderSurface(*shader, eve::graphics::BlendMode::Opaque, true, true);
    REQUIRE(surface.ok());
    auto raster = graphics->configureMeshShaderRaster(*shader, {});
    REQUIRE(raster.ok());
    const float         positions[]{-1, -1, 0, 1, -1, 0, 1, 1, 0, -1, 1, 0};
    const float         normals[]{0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
    const float         uv[]{0, 0, 1, 0, 1, 1, 0, 1};
    const std::uint32_t indices[]{0, 1, 2, 0, 2, 3};
    auto*               mesh = graphics->newMeshFromArrays(positions, normals, uv, 4, indices, 6);
    REQUIRE(mesh != nullptr);
    // Two independent records: equal angular geometry, different physical depths and colors.
    std::array<float, 32> records{
        0, 0, 0.625f, 0.125f, 320.f / 180.f, 1, 904, 0, 0.8f, 0.1f, 0.1f, 0, -0.2f, -0.3f, 0.2f, 0.3f,
        0, 0, 0.625f, 0.125f, 320.f / 180.f, 1, 960, 0, 0.1f, 0.1f, 0.8f, 0, -0.2f, -0.3f, 0.2f, 0.3f};
    std::array<glm::mat4, 2>        transforms{glm::mat4(1.f), glm::mat4(1.f)};
    eve::graphics::ShaderImageInput image;
    image.format  = eve::graphics::ShaderImageFormat::RGBA32Float;
    image.width   = 4;
    image.height  = 2;
    image.sampler = eve::graphics::TextureSampler::nearest();
    image.bytes   = std::as_bytes(std::span(records));
    eve::graphics::ShaderResourceInputs inputs;
    inputs.images           = std::span(&image, 1);
    inputs.instanceMatrices = std::as_bytes(std::span(transforms));
    const std::array<std::uint32_t, 4> dimensions{image.width, image.height, 2, 0};
    inputs.constants = std::as_bytes(std::span(dimensions));
    const std::vector<std::uint32_t> vertex{sphere_batch_vert_spv, sphere_batch_vert_spv + sphere_batch_vert_spv_count};
    const std::vector<std::uint32_t> fragment{sphere_batch_frag_spv,
                                              sphere_batch_frag_spv + sphere_batch_frag_spv_count};
    auto                             prepared = graphics->replaceMeshShaderResources(*shader, vertex, fragment, inputs);
    if (!prepared.ok()) std::fprintf(stderr, "%s\n", prepared.status().describe().c_str());
    REQUIRE(prepared.ok());
    records.fill(0);  // GPU resources must own copies, not retain these caller bytes.
    for (int frame = 0; frame < 3; ++frame) {
        graphics->begin3DFrame();
        auto drawn = graphics->drawMeshShaderInstances(*mesh, *shader, glm::mat4(1.f), eve::graphics::Color(1.f), 0, 2);
        REQUIRE(drawn.ok());
        graphics->present();
    }
    REQUIRE(std::abs(graphics->getPixel(160, 90).r - expectedNear.r) < 0.01f);
    REQUIRE(std::abs(graphics->getPixel(160, 90).b - expectedNear.b) < 0.01f);
    graphics->begin3DFrame();
    auto invalid = graphics->drawMeshShaderInstances(*mesh, *shader, glm::mat4(1.f), eve::graphics::Color(1.f), 2, 1);
    REQUIRE(!invalid.ok());
    auto farOnly = graphics->drawMeshShaderInstances(*mesh, *shader, glm::mat4(1.f), eve::graphics::Color(1.f), 1, 1);
    REQUIRE(farOnly.ok());
    graphics->present();
    REQUIRE(std::abs(graphics->getPixel(160, 90).b - expectedNear.r) < 0.01f);
    REQUIRE(std::abs(graphics->getPixel(160, 90).r - expectedNear.b) < 0.01f);
    records          = {0, 0, 0.625f, 0.125f, 320.f / 180.f, 1, 904, 0, 0.1f, 0.1f, 0.8f, 0, -0.2f, -0.3f, 0.2f, 0.3f,
                        0, 0, 0.625f, 0.125f, 320.f / 180.f, 1, 960, 0, 0.8f, 0.1f, 0.1f, 0, -0.2f, -0.3f, 0.2f, 0.3f};
    auto imageUpdate = eve::graphics::ShaderImageUpdate::prepare(image);
    REQUIRE(imageUpdate.ok());
    records.fill(0);
    REQUIRE(imageUpdate.value().apply(*graphics, *shader).ok());
    graphics->begin3DFrame();
    REQUIRE(graphics->drawMeshShaderInstances(*mesh, *shader, glm::mat4(1.f), eve::graphics::Color(1.f), 0, 2).ok());
    graphics->present();
    REQUIRE(std::abs(graphics->getPixel(160, 90).b - expectedNear.r) < 0.01f);
    auto wrongShape   = image;
    wrongShape.width  = 8;
    wrongShape.height = 1;
    auto mismatch     = eve::graphics::ShaderImageUpdate::prepare(wrongShape);
    REQUIRE(mismatch.ok());
    REQUIRE(!mismatch.value().apply(*graphics, *shader).ok());
    wrongShape.bytes  = {};
    auto invalidBytes = eve::graphics::ShaderImageUpdate::prepare(wrongShape);
    REQUIRE(!invalidBytes.ok());
    auto ownedUpdate = std::move(imageUpdate.value());
    REQUIRE(!imageUpdate.value().apply(*graphics, *shader).ok());
    REQUIRE(ownedUpdate.apply(*graphics, *shader).ok());
    image.contentOwner   = std::make_shared<int>(42);
    auto invalidIdentity = eve::graphics::ShaderImageUpdate::prepare(image);
    REQUIRE(!invalidIdentity.ok());
    // A live immutable identity must never be changed through the new opt-in operation.
    REQUIRE(graphics->replaceMeshShaderResources(*shader, vertex, fragment, inputs).ok());
    REQUIRE(!ownedUpdate.apply(*graphics, *shader).ok());
    auto* peer = graphics->newMeshShaderFromSpv({sphere_vert_spv, sphere_vert_spv + sphere_vert_spv_count},
                                                {sphere_frag_spv, sphere_frag_spv + sphere_frag_spv_count});
    REQUIRE(peer != nullptr);
    REQUIRE(graphics->replaceMeshShaderResources(*peer, vertex, fragment, inputs).ok());
    image.contentOwner.reset();
    REQUIRE(!ownedUpdate.apply(*graphics, *shader).ok());  // Both shaders still share the image after identity expiry.
    REQUIRE(graphics->releaseShader(peer));
    delete peer;
    REQUIRE(ownedUpdate.apply(*graphics, *shader).ok());  // Unique storage is writable once no identity remains.
    REQUIRE(graphics->releaseMesh(mesh));
    delete mesh;
    REQUIRE(graphics->releaseShader(shader));
    delete shader;
    {
        ssq::VM vm(2048, ssq::Libs::ALL);
        auto    table = vm.addTable("eve");
        table.addAbstractClass<eve::graphics::Graphics>("TestGraphicsProvider");
        eve::graphics::multiscale::MultiscaleView::expose(table);
        vm.addFunc("testGraphics", [graphics]() { return graphics; });
        vm.run(vm.compileSource(R"(
            local api = eve.MultiscaleView();
            local input = {
                camera={cell=["0","0","0"], fraction=[0,0,0]},
                view={right=[1,0,0], up=[0,1,0], forward=[0,0,1],
                      tanHalfVerticalFov=1, aspect=320.0/180.0, cellExponent=900},
                spheres=[{center={cell=["0","0","10"], fraction=[0,0,0]},
                          radiusMantissa=2, radiusExponent=900, color=[0.8,0.1,0.1]}]
            };
            local frame = api.prepareSphereFrame(input,1);
            assert(frame.ok);
            batch <- api.prepareSphereBatch(testGraphics(), frame.value);
            assert(batch.ok);
            wrongFrame <- api.prepareSphereBatch(testGraphics(), api);
            nullFrame <- api.prepareSphereBatch(testGraphics(), null);
            input.spheres[0].color = [0.1,0.1,0.8];
            local changed = api.prepareSphereFrame(input,1);
            assert(changed.ok);
            updated <- batch.value.update(changed.value);
            wrongUpdate <- batch.value.update(api);
            nullUpdate <- batch.value.update(null);
            wrongReceiverUpdate <- batch.value.update.call(api, changed.value);
            changed = null;
            frame = null;
            input = null;
            collectgarbage();
            drawn <- batch.value.draw();
        )"));
        REQUIRE(vm.find("batch").toTable().get<bool>("ok"));
        REQUIRE(vm.find("drawn").toTable().get<bool>("ok"));
        REQUIRE(!vm.find("wrongFrame").toTable().get<bool>("ok"));
        REQUIRE(!vm.find("nullFrame").toTable().get<bool>("ok"));
        REQUIRE(vm.find("updated").toTable().get<bool>("ok"));
        REQUIRE(!vm.find("wrongUpdate").toTable().get<bool>("ok"));
        REQUIRE(!vm.find("nullUpdate").toTable().get<bool>("ok"));
        REQUIRE(!vm.find("wrongReceiverUpdate").toTable().get<bool>("ok"));
        graphics->present();
        REQUIRE(std::abs(graphics->getPixel(160, 90).b - expectedNear.r) < 0.01f);
        REQUIRE(std::abs(graphics->getPixel(160, 90).r - expectedNear.b) < 0.01f);
        vm.run(vm.compileSource("batch = null; collectgarbage();"));
    }
    window->close();
}

TEST_CASE("graphics.multiscale.distantPointFluxDisplaysWithExposureAndOpaqueOcclusion") {
    auto* window = eve::window::Window::create();
    auto* graphics = eve::graphics::Graphics::create();
    eve::window::WindowSettings settings;
    settings.width = 320;
    settings.height = 180;
    REQUIRE(window->setWindowSettings(settings));
    graphics->setScreenReadbackEnabled(true);
    graphics->setBackgroundColor({0.02f, 0.03f, 0.05f, 1.f});
    eve::graphics::multiscale::AngularView view;
    view.cellExponent = 900;
    view.aspect = 320.0 / 180.0;
    eve::graphics::multiscale::FarEmitter emitter;
    emitter.position.cell[2] = 1000000000000000000LL;
    emitter.powerExponent = 1920;
    emitter.rgbWeights = {1, 0, 0};
    eve::graphics::multiscale::FarFluxPolicy policy;
    policy.width = 320;
    policy.height = 180;
    auto flux = eve::graphics::multiscale::FarFluxFrame::build(std::span(&emitter, 1), {}, view, policy);
    REQUIRE(flux.ok());
    auto display = eve::graphics::multiscale::FarFluxView::prepare(*graphics, flux.value(), 16.f);
    if (!display.ok()) std::fprintf(stderr, "%s\n", display.status().describe().c_str());
    REQUIRE(display.ok());
    graphics->begin3DFrame();
    auto firstDraw = display.value()->draw();
    if (!firstDraw.ok()) std::fprintf(stderr, "%s\n", firstDraw.status().describe().c_str());
    REQUIRE(firstDraw.ok());
    graphics->present();
    const auto bright = graphics->getPixel(160, 90);
    REQUIRE(bright.r > bright.b + 0.15f);
    REQUIRE(display.value()->setExposure(1.f).ok());
    graphics->begin3DFrame();
    REQUIRE(display.value()->draw().ok());
    graphics->present();
    const auto dim = graphics->getPixel(160, 90);
    REQUIRE(dim.r < bright.r - 0.03f);
    emitter.rgbWeights = {0, 1, 0};
    auto nextFlux = eve::graphics::multiscale::FarFluxFrame::build(std::span(&emitter, 1), {}, view, policy);
    REQUIRE(nextFlux.ok());
    REQUIRE(display.value()->update(nextFlux.value()).ok());
    graphics->begin3DFrame();
    REQUIRE(display.value()->draw().ok());
    graphics->present();
    const auto replaced = graphics->getPixel(160, 90);
    REQUIRE(replaced.g > replaced.r + 0.03f);
    emitter.rgbWeights = {1, 0, 0};
    auto restoredFlux = eve::graphics::multiscale::FarFluxFrame::build(std::span(&emitter, 1), {}, view, policy);
    REQUIRE(restoredFlux.ok());
    REQUIRE(display.value()->update(restoredFlux.value()).ok());
    std::array<eve::spatial::CellPosition, 1> centers{};
    centers[0].cell[2] = 10;
    const std::array<eve::graphics::multiscale::SphereAppearance, 1> appearances{
        {{2, 900, {0.1f, 0.1f, 0.8f}}}};
    auto sphere = eve::graphics::multiscale::SphereFrame::build(centers, appearances, {}, view, 1);
    REQUIRE(sphere.ok());
    auto batch = eve::graphics::multiscale::SphereBatch::prepare(*graphics, sphere.value());
    REQUIRE(batch.ok());
    REQUIRE(display.value()->setExposure(16.f).ok());
    REQUIRE(batch.value()->draw().ok());
    REQUIRE(display.value()->draw().ok());
    graphics->present();
    const auto occluded = graphics->getPixel(160, 90);
    REQUIRE(occluded.b > occluded.r + 0.3f);
    batch.value().reset();
    display.value().reset();
    window->close();
}
