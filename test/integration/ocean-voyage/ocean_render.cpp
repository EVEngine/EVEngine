// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "default": "render-window",
  "module": "fluids"
}
*/
// clang-format on

#include "fluids/OceanBindings.h"
#include "fluids/OceanGpuSpectrum.h"
#include "fluids/OceanHullMask.h"
#include "fluids/OceanRenderer.h"
#include "fluids/OceanWake.h"
#include "fluids/OceanWaterView.h"
#include "gpgpu/Gpgpu.h"
#include "graphics/Graphics.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>

namespace {
eve::Result<void> advanceWake(eve::fluids::OceanWake& wake, double dt, double endTime,
                              const eve::fluids::OceanSpectrumFrame& frame) {
    auto prepared = wake.prepareStep(dt, endTime, frame);
    if (!prepared) return eve::Result<void>::failure(prepared.status());
    return wake.commitStep(*prepared.value());
}
}  // namespace

TEST_CASE("fluids.ocean.rendererPublishesSharedSurfaceAndFoam") {
    auto*                       window   = eve::window::Window::create();
    auto*                       graphics = eve::graphics::Graphics::create();
    eve::window::WindowSettings settings;
    settings.width  = 160;
    settings.height = 120;
    REQUIRE(window->setWindowSettings(settings));
    eve::fluids::OceanRendererBuilder builder(*graphics);
    builder.mesh  = {32, 16, 0.5, 128.0};
    auto renderer = builder.build();
    REQUIRE(renderer.ok());
    CHECK(!renderer.value()->draw().ok());
    eve::fluids::OceanSpectrumBuilder spectrumBuilder;
    spectrumBuilder.settings.significantWaveHeight = 0.0;
    spectrumBuilder.settings.bands                 = {{16, 64.0, 8.0, 64.0}};
    auto spectrum                                  = spectrumBuilder.build();
    REQUIRE(spectrum.ok());
    auto spectrumFrame = spectrum.value().frameBuilder(0.0).build();
    REQUIRE(spectrumFrame.ok());
    eve::fluids::OceanWaterView water(spectrumFrame.value());
    auto                        frame = renderer.value()->frameBuilder(water).build();
    REQUIRE(frame.ok());
    REQUIRE(renderer.value()->publish(frame.value()).ok());
    graphics->setScreenReadbackEnabled(true);
    graphics->setBackgroundColor({0.8F, 0.1F, 0.4F, 1.0F});
    const glm::vec3 eye(0.0F, 8.0F, 0.0F);
    const auto      view       = glm::lookAtRH(eye, glm::vec3(0.0F), glm::vec3(0.0F, 0.0F, -1.0F));
    auto            projection = glm::perspectiveRH_ZO(glm::radians(55.0F), 160.0F / 120.0F, 0.1F, 500.0F);
    projection[1][1] *= -1.0F;
    auto draw = [&](bool drawSea = true) {
        graphics->begin3DFrame();
        graphics->setMesh3DViewProj(projection * view);
        graphics->setMesh3DView(view);
        graphics->setMesh3DCameraPos(eye);
        graphics->setDirectionalLight(0.2F, 1.0F, 0.4F);
        if (drawSea) REQUIRE(renderer.value()->draw().ok());
        graphics->present();
        return graphics->getPixel(80, 60);
    };
    const auto background = draw(false);
    auto       clearSea   = draw();
    CHECK(clearSea.b > clearSea.r);
    // Invalid upload preserves the previous visible frame.
    auto invalid         = frame.value();
    invalid.positions[0] = std::numeric_limits<float>::quiet_NaN();
    CHECK(!renderer.value()->publish(invalid).ok());
    auto preserved = draw();
    CHECK(std::abs(preserved.b - clearSea.b) < 0.01F);
    for (std::size_t i = 0; i < frame.value().surface.size(); i += 2) frame.value().surface[i] = 1.0F;
    REQUIRE(renderer.value()->publish(frame.value()).ok());
    auto foamed = draw();
    CHECK(foamed.r > clearSea.r + 0.2F);
    CHECK(foamed.g > clearSea.g + 0.2F);
    std::array<eve::fluids::OceanHullVolume, 1> hulls{};
    hulls[0].halfExtent = {2, 2, 2};
    auto mask           = eve::fluids::OceanHullMaskBuilder(hulls).build();
    REQUIRE(mask.ok());
    REQUIRE(renderer.value()->hullMaskBuilder(mask.value()).publish().ok());
    auto dry = draw();
    CHECK(std::abs(dry.r - background.r) < 0.01F);
    CHECK(std::abs(dry.g - background.g) < 0.01F);
    CHECK(std::abs(dry.b - background.b) < 0.01F);
    CHECK(std::abs(dry.g - foamed.g) > 0.1F);
    auto emptyMask = eve::fluids::OceanHullMaskBuilder({}).build();
    REQUIRE(emptyMask.ok());
    REQUIRE(renderer.value()->hullMaskBuilder(emptyMask.value()).publish().ok());
    auto restored = draw();
    CHECK(std::abs(restored.r - foamed.r) < 0.01F);
    renderer.value().reset();
    window->close();
}

TEST_CASE("fluids.ocean.residentRenderingMatchesCpuWithWakeAndFloatingOrigin") {
    using namespace eve::fluids;

    auto*                       window   = eve::window::Window::create();
    auto*                       graphics = eve::graphics::Graphics::create();
    auto*                       compute  = eve::gpgpu::Gpgpu::create();
    eve::window::WindowSettings settings;
    settings.width  = 160;
    settings.height = 120;
    REQUIRE(window->setWindowSettings(settings));
    OceanRendererBuilder renderBuilder(*graphics);
    renderBuilder.mesh = {64, 32, 0.5, 128.0};
    auto reference     = renderBuilder.build();
    REQUIRE(reference.ok());
    renderBuilder.backend = OceanRenderBackend::GpuResident;
    auto resident         = renderBuilder.build();
    if (graphics->getBackendName() != "vulkan") {
        REQUIRE(!resident.ok());
        window->close();
        return;
    }
    REQUIRE(resident.ok());
    const glm::dvec3     origin(100000000.0, 2.0, -100000000.0);
    const glm::dvec2     center(100000000.25, -99999999.5);
    OceanSpectrumBuilder seaBuilder;
    seaBuilder.settings.seaLevel              = 2.0;
    seaBuilder.settings.significantWaveHeight = 0.8;
    seaBuilder.settings.bands                 = {{16, 64.0, 8.0, 32.0}, {32, 256.0, 32.0, 256.0}};
    auto sea                                  = seaBuilder.build();
    REQUIRE(sea.ok());
    auto gpu = OceanGpuSpectrumBuilder(sea.value(), *graphics, *compute).build();
    REQUIRE(gpu.ok());
    OceanWakeSettings wakeSettings;
    wakeSettings.center          = center;
    wakeSettings.resolution      = 32;
    wakeSettings.length          = 64.0;
    wakeSettings.absorptionWidth = 8.0;
    auto wake                    = OceanWake::create(wakeSettings);
    REQUIRE(wake.ok());
    const std::array<OceanWakeImpulse, 1> impulses{{{center, 4.0, 2.0, {1.0, 0.5}, 0.75}}};
    REQUIRE(wake.value()->inject(impulses).ok());
    graphics->setScreenReadbackEnabled(true);
    const glm::vec3 eye(0.0F, 8.0F, 10.0F);
    const auto      view       = glm::lookAtRH(eye, glm::vec3(0.0F), glm::vec3(0.0F, 1.0F, 0.0F));
    auto            projection = glm::perspectiveRH_ZO(glm::radians(55.0F), 160.0F / 120.0F, 0.1F, 500.0F);
    projection[1][1] *= -1.0F;
    auto draw = [&](OceanRenderer& renderer, bool drawSea = true) {
        graphics->begin3DFrame();
        graphics->setMesh3DViewProj(projection * view);
        graphics->setMesh3DView(view);
        graphics->setMesh3DCameraPos(eye);
        graphics->setDirectionalLight(0.2F, 1.0F, 0.4F);
        if (drawSea) REQUIRE(renderer.draw().ok());
        graphics->present();
        std::array<glm::vec3, 99> pixels;
        for (int z = 0; z < 9; ++z)
            for (int x = 0; x < 11; ++x) {
                auto value         = graphics->getPixel(20 + x * 12, 12 + z * 12);
                pixels[z * 11 + x] = {value.r, value.g, value.b};
            }
        return pixels;
    };
    std::array<glm::vec3, 99> previous{};
    bool                      animated = false;
    for (int step = 1; step <= 3; ++step) {
        auto start = sea.value().frameBuilder(wake.value()->time()).build();
        REQUIRE(start.ok());
        REQUIRE(advanceWake(*wake.value(), 1.0 / 60.0, step / 60.0, start.value()).ok());
        auto frame = sea.value().frameBuilder(wake.value()->time()).build();
        REQUIRE(frame.ok());
        OceanWaterView water(frame.value(), *wake.value());
        auto           cpuBuilder = reference.value()->frameBuilder(water);
        cpuBuilder.center         = center;
        cpuBuilder.origin         = origin;
        auto vertices             = cpuBuilder.build();
        REQUIRE(vertices.ok());
        REQUIRE(reference.value()->publish(vertices.value()).ok());
        auto token = gpu.value()->frameBuilder(wake.value()->time()).submit();
        REQUIRE(token.ok());
        auto publication   = resident.value()->gpuFrameBuilder(*gpu.value(), token.value());
        publication.center = center;
        publication.origin = origin;
        publication.wake   = wake.value().get();
        REQUIRE(publication.publish().ok());
        auto   expected = draw(*reference.value());
        auto   actual   = draw(*resident.value());
        double worst    = 0.0;
        float  peak = 0.0F, darkest = 10.0F;
        for (std::size_t i = 0; i < actual.size(); ++i) {
            worst   = std::max(worst, double(glm::length(actual[i] - expected[i])));
            peak    = std::max(peak, glm::length(expected[i]));
            darkest = std::min(darkest, glm::length(expected[i]));
            if (step > 1 && glm::length(expected[i] - previous[i]) > 0.001F) animated = true;
        }
        previous = expected;
        CHECK(peak > 0.2F);
        CHECK(peak - darkest > 0.1F);
        std::printf("Resident ocean frame=%d worst RGB difference=%.9g\n", step, worst);
        CHECK(worst < 0.03);
        // Producer reuse cannot change the already-published texture snapshot.
        auto newer = gpu.value()->frameBuilder(wake.value()->time() + 1.0).submit();
        REQUIRE(newer.ok());
        CHECK(!publication.publish().ok());
        auto mismatched = resident.value()->gpuFrameBuilder(*gpu.value(), newer.value());
        mismatched.wake = wake.value().get();
        CHECK(!mismatched.publish().ok());
        auto preserved = draw(*resident.value());
        for (std::size_t i = 0; i < actual.size(); ++i) CHECK(glm::length(preserved[i] - actual[i]) < 0.01F);
    }
    CHECK(animated);
    graphics->setBackgroundColor({0.8F, 0.1F, 0.4F, 1.0F});
    std::array<OceanHullVolume, 1> hulls{};
    hulls[0].center     = origin;
    hulls[0].halfExtent = {3, 3, 3};
    OceanHullMaskBuilder maskBuilder(hulls);
    maskBuilder.origin = origin;
    auto mask          = maskBuilder.build();
    REQUIRE(mask.ok());
    REQUIRE(reference.value()->hullMaskBuilder(mask.value()).publish().ok());
    REQUIRE(resident.value()->hullMaskBuilder(mask.value()).publish().ok());
    const auto background = draw(*reference.value(), false);
    auto       dryCpu     = draw(*reference.value());
    auto       dryGpu     = draw(*resident.value());
    for (std::size_t i = 0; i < dryCpu.size(); ++i) CHECK(glm::length(dryCpu[i] - dryGpu[i]) < 0.03F);
    CHECK(glm::length(dryGpu[49] - background[49]) < 0.01F);
    CHECK(glm::length(dryGpu[49] - previous[49]) > 0.1F);
    maskBuilder.origin.x += 1;
    auto wrongOrigin = maskBuilder.build();
    REQUIRE(wrongOrigin.ok());
    REQUIRE(resident.value()->hullMaskBuilder(wrongOrigin.value()).publish().ok());
    CHECK(!resident.value()->draw().ok());
    REQUIRE(resident.value()->hullMaskBuilder(mask.value()).publish().ok());
    gpu.value().reset();
    auto retained = draw(*resident.value());
    CHECK(std::isfinite(retained[0].x));
    resident.value().reset();
    reference.value().reset();
    window->close();
}

TEST_CASE("fluids.ocean.scriptRenderBuildersOwnInputsAcrossGc") {
    auto*                       window   = eve::window::Window::create();
    auto*                       graphics = eve::graphics::Graphics::create();
    auto*                       compute  = eve::gpgpu::Gpgpu::create();
    eve::window::WindowSettings settings;
    settings.width  = 160;
    settings.height = 120;
    REQUIRE(window->setWindowSettings(settings));
    REQUIRE(graphics->getBackendName() == "vulkan");
    graphics->setScreenReadbackEnabled(true);
    {
        ssq::VM vm(4096, ssq::Libs::ALL);
        auto    table = vm.addTable("eve");
        table.addClass<eve::graphics::Graphics>(
            "Graphics", std::function<eve::graphics::Graphics*()>([] { return nullptr; }), false);
        table.addClass<eve::gpgpu::Gpgpu>("Gpgpu", std::function<eve::gpgpu::Gpgpu*()>([] { return nullptr; }), false);
        vm.addFunc("testGraphics", [graphics] { return graphics; });
        vm.addFunc("testCompute", [compute] { return compute; });
        eve::fluids::exposeOceanTypes(table);
        vm.run(vm.compileSource(R"(
            function checked(r) { if (!r.ok) throw "ocean render operation failed"; return r.hasValue ? r.value : null; }
            local sb = eve.OceanSpectrumBuilder(); local s = checked(sb.settings());
            s.significantWaveHeight = 0.8;
            s.bands = [{resolution=16,length=64.0,minimumWavelength=8.0,maximumWavelength=64.0}];
            checked(sb.configure(s)); local sea = checked(sb.build());
            local wb = eve.OceanWakeBuilder(); local ws = checked(wb.settings());
            ws.resolution=16; ws.length=32.0; ws.absorptionWidth=8.0;
            checked(wb.configure(ws)); local wake = checked(wb.build());
            local gb = eve.OceanGpuSpectrumBuilder();
            local gpu = checked(gb.build(sea,testGraphics(),testCompute()));
            local token = checked(checked(gpu.frameBuilder(0,60)).submit());
            local rb = eve.OceanRendererBuilder(); local rs = checked(rb.settings());
            rs.segments=32; rs.rings=16; rs.outerRadius=128.0;
            checked(rb.configure(rs)); render <- checked(rb.build(testGraphics()));
            rs.backend="cpu"; checked(rb.configure(rs)); cpuRender <- checked(rb.build(testGraphics()));
            local placement = {center=[0.0,0.0],origin=[0.0,0.0,0.0]};
            publication <- checked(render.gpuFrameBuilder(gpu,token,wake,placement));
            local frame = checked(checked(sea.frameBuilder(0,60)).build());
            cpuPublication <- checked(cpuRender.frameBuilder(frame,wake,placement));
            checked(publication.publish());
            checked(cpuPublication.publish());
            token = checked(checked(gpu.frameBuilder(1,60)).submit());
            staleRejected <- !publication.publish().ok;
            retainedPublication <- checked(render.gpuFrameBuilder(gpu,token,null,placement));
            local disposable = checked(gb.build(sea,testGraphics(),testCompute()));
            local pending = checked(disposable.frameBuilder(2,60));
            disposable=null; collectgarbage();
            checked(pending.submit());
            gpu=null; token=null; sea=null; frame=null; wake=null;
            collectgarbage();
            checked(cpuPublication.publish());
            function drawGpu() { checked(render.draw()); }
            function drawCpu() { checked(cpuRender.draw()); }
        )"));
        CHECK(vm.find("staleRejected").toBool());
        const glm::vec3 eye(0.0F, 8.0F, 10.0F);
        const auto      view       = glm::lookAtRH(eye, glm::vec3(0.0F), glm::vec3(0.0F, 1.0F, 0.0F));
        auto            projection = glm::perspectiveRH_ZO(glm::radians(55.0F), 160.0F / 120.0F, 0.1F, 500.0F);
        projection[1][1] *= -1.0F;
        auto draw = [&](const char* script) {
            graphics->begin3DFrame();
            graphics->setMesh3DViewProj(projection * view);
            graphics->setMesh3DView(view);
            graphics->setMesh3DCameraPos(eye);
            graphics->setDirectionalLight(0.2F, 1.0F, 0.4F);
            vm.run(vm.compileSource(script));
            graphics->present();
            std::vector<glm::vec3> pixels;
            for (int y = 12; y <= 108; y += 12)
                for (int x = 20; x <= 140; x += 12) pixels.emplace_back(graphics->getPixel(x, y));
            return pixels;
        };
        auto  cpu = draw("drawCpu();"), gpu = draw("drawGpu();");
        float peak = 0.0F, difference = 0.0F;
        for (std::size_t i = 0; i < cpu.size(); ++i) {
            peak       = std::max(peak, glm::length(cpu[i]));
            difference = std::max(difference, glm::length(cpu[i] - gpu[i]));
        }
        CHECK(peak > 0.2F);
        CHECK(difference < 0.03F);
        vm.run(vm.compileSource(R"(
            local hullConfig={origin=[0,0,0],volumes=[{center=[0,0,0],rotation=[0,0,0,1],
                halfExtent=[1,1,1],bevels=[]}]};
            local hullPublication=checked(render.hullMaskBuilder(hullConfig));
            hullConfig.volumes[0].halfExtent[0]=-1;
            local invalidHull=checked(render.hullMaskBuilder(hullConfig));
            if(invalidHull.publish().ok) throw "Invalid hull accepted";
            hullConfig.unknown <- true;
            if(render.hullMaskBuilder(hullConfig).ok) throw "Unknown hull field accepted";
            hullConfig=null; invalidHull=null;
            render=null; cpuRender=null; publication=null; collectgarbage();
            checked(hullPublication.publish());
            hullPublication=null; collectgarbage();
            checked(retainedPublication.publish());
            checked(cpuPublication.publish());
            retainedPublication=null; cpuPublication=null; collectgarbage();
        )"));
    }
    window->close();
}
