#include <array>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>
#include "common/config.h"
#include "fluids/OceanBindings.h"
#include "fluids/OceanBoat.h"
#include "fluids/OceanSprayRenderer.h"
#if !defined(EVENGINE_WEBGPU)
#include "graphics/vulkan/Graphics.h"
#endif
#include "graphics/Graphics.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

TEST_CASE("fluids.ocean.sprayBatchRendersAndRejectsInvalidPublication") {
    using namespace eve::fluids;
    auto*                       window = eve::window::Window::create();
    auto*                       gfx    = eve::graphics::Graphics::create();
    eve::window::WindowSettings ws;
    ws.width  = 160;
    ws.height = 120;
    REQUIRE(window->setWindowSettings(ws));
    auto renderer = OceanSprayRendererBuilder(*gfx).build();
    REQUIRE(renderer.ok());
    CHECK(!renderer.value()->draw().ok());
    OceanSpectrumBuilder sb;
    sb.settings.significantWaveHeight = 0;
    sb.settings.bands                 = {{8, 32, 8, 32}};
    auto sea                          = sb.build();
    REQUIRE(sea.ok());
    auto frame = sea.value().frameBuilder(0).build();
    REQUIRE(frame.ok());
    OceanWaterView     water(frame.value());
    OceanSpraySettings settings;
    settings.radius   = 0.4;
    settings.capacity = 4;
    auto initial      = OceanSprayFrame::create(settings);
    REQUIRE(initial.ok());
    OceanBoatReceipt receipt;
    receipt.contacts.push_back({{0, 0, 0}, {5, 0, 0}, 1});
    const std::array<OceanSpraySource, 1> sources{{{1, receipt}}};
    auto spray = OceanSprayStepBuilder(initial.value(), water, sources, 1.0 / 30.0).build();
    REQUIRE(spray.ok());
    REQUIRE(!spray.value().particles().empty());
    const glm::vec3       target(spray.value().particles()[0].position), eye = target + glm::vec3(0, 0, 3);
    OceanSprayMeshBuilder mb(spray.value());
    mb.view.eye = eye;
    auto mesh   = mb.build();
    REQUIRE(mesh.ok());
    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor({0.0F, 0.0F, 0.0F, 1.0F});
    const auto view       = glm::lookAtRH(eye, target, glm::vec3(0, 1, 0));
    auto       projection = glm::perspectiveRH_ZO(glm::radians(55.0F), 160.0F / 120.0F, 0.1F, 100.0F);
    projection[1][1] *= -1;
    auto draw = [&]() {
        gfx->begin3DFrame();
        gfx->setMesh3DViewProj(projection * view);
        gfx->setMesh3DView(view);
        gfx->setMesh3DCameraPos(eye);
        REQUIRE(renderer.value()->draw().ok());
        gfx->present();
        return gfx->getPixel(80, 60);
    };
    REQUIRE(renderer.value()->publish(mesh.value()).ok());
    auto pixel = draw();
    CHECK(pixel.r > 0.2F);
    auto bad         = mesh.value();
    bad.positions[0] = std::numeric_limits<float>::quiet_NaN();
    CHECK(!renderer.value()->publish(bad).ok());
    auto preserved = draw();
    CHECK(std::abs(preserved.r - pixel.r) < 0.01F);
    REQUIRE(renderer.value()->publish({}).ok());
    auto empty = draw();
    CHECK(empty.r < 0.01F);
    renderer.value().reset();
    window->close();
}

TEST_CASE("fluids.ocean.sprayScriptBuildersSurviveGc") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    auto    table = vm.addTable("eve");
    eve::fluids::exposeOceanTypes(table);
    eve::fluids::OceanBoatReceipt receipt;
    receipt.contacts.push_back({{0, 0, 0}, {5, 0, 0}, 1});
    vm.addFunc("receipt", [&]() { return &receipt; });
    vm.run(vm.compileSource(R"(
        function checked(r) { if(!r.ok) throw r.status.summary; return r.hasValue?r.value:null; }
        local sb=eve.OceanSpectrumBuilder(),s=checked(sb.settings()); s.significantWaveHeight=0.0;
        s.bands=[{resolution=8,length=32.0,minimumWavelength=8.0,maximumWavelength=32.0}];
        checked(sb.configure(s));local sea=checked(sb.build());
        local water=checked(checked(sea.frameBuilder(0,60)).build());
        local builder=eve.OceanSprayBuilder(),settings=checked(builder.settings());
        settings.seed="18446744073709551615";checked(builder.configure(settings));
        local frame=checked(builder.build());local step=checked(frame.stepBuilder(water,null,60));
        checked(step.addReceipt(1,receipt()));duplicateRejected <- !step.addReceipt(1,receipt()).ok;
        frame=null;water=null;sea=null;collectgarbage();
        local next=checked(step.build());alive <- checked(next.summary()).alive;
        step=null;collectgarbage();retained <- checked(next.summary()).alive==alive;
    )"));
    CHECK(vm.find("duplicateRejected").toBool());
    CHECK(vm.find("alive").toInt() > 0);
    CHECK(vm.find("retained").toBool());
}

#if !defined(EVENGINE_WEBGPU)
TEST_CASE("fluids.ocean.sprayRendererObservesProviderRetirement") {
    auto* previous = eve::graphics::Graphics::create();
    auto  device   = std::make_unique<eve::graphics::vulkan::Graphics>();
    eve::ModuleManager::insert("Graphics", device.get());
    struct Restore {
        eve::graphics::Graphics* previous;
        ~Restore() { eve::ModuleManager::insert("Graphics", previous); }
    } restore{previous};
    device->initHeadless(64, 64);
    auto renderer = eve::fluids::OceanSprayRendererBuilder(*device).build();
    REQUIRE(renderer.ok());
    REQUIRE(renderer.value()->publish({}).ok());
    device.reset();
    eve::ModuleManager::insert("Graphics", previous);
    CHECK(!renderer.value()->draw().ok());
    CHECK(!renderer.value()->publish({}).ok());
    renderer.value().reset();
}
#endif
