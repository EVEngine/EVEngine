#include <execinfo.h>
#include <signal.h>
#include <unistd.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <simplesquirrel/simplesquirrel.hpp>
#include <vector>
#include "common/Export.h"
#include "common/Module.h"
#include "fluids/OceanBindings.h"
#include "gpgpu/Gpgpu.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "image/ImageData.h"
#include "window/Window.h"

namespace eve::diagnostic {
static void crash(int signal) {
    const char text[] = "ENGINE_PROBE_SIGNAL\n";
    (void)::write(2, text, sizeof(text) - 1);
    void*     stack[40];
    const int count = backtrace(stack, 40);
    backtrace_symbols_fd(stack, count, 2);
    std::_Exit(128 + signal);
}
class GpuProbe : public Module {
public:
    Module_REG(GpuProbe);
    std::vector<glm::vec3> cpu_;
    int                    run() {
        ::signal(SIGSEGV, crash);
        ::signal(SIGABRT, crash);
        ::signal(SIGBUS, crash);
        auto* gfx = graphics::Graphics::create();
        std::printf("ENGINE_PROBE backend=%s width=%d height=%d\n", gfx->getBackendName().c_str(), gfx->getWidth(),
                                       gfx->getHeight());
        std::fflush(stdout);
        gfx->begin3DFrame();
        gfx->present();
        const float    positions[]{-1, -1, .5f, 1, -1, .5f, 1, 1, .5f, -1, 1, .5f};
        const float    normals[]{0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
        const float    uvs[]{0, 0, 1, 0, 1, 1, 0, 1};
        const uint32_t indices[]{0, 2, 1, 2, 0, 3};
        auto*          mesh = gfx->newMeshFromArrays(positions, normals, uvs, 4, indices, 6);
        const uint8_t  red[]{255, 0, 0, 255};
        auto*          texture  = gfx->newTexture(1, 1, red);
        int            failures = 0;
        for (int size : {64, 96}) {
            std::printf("ENGINE_PROBE begin GBuffer %d\n", size);
            std::fflush(stdout);
            gfx->beginGBufferPass(size, size);
            gfx->drawMeshGBuffer(mesh, glm::mat4(1), glm::mat4(1), .1f, 100.f, texture, 1, 1, 1, 0, 0, .25f, .75f);
            gfx->endGBufferPass();
            std::unique_ptr<image::ImageData> image(gfx->readGBufferToImageData("albedo"));
            if (!image) throw std::runtime_error("No GBuffer readback");
            auto* bytes = (const uint8_t*)image->getData();
            auto* p = bytes + (size_t(image->getHeight() / 2) * image->getWidth() + image->getWidth() / 2) * 4;
            bool  ok = p[0] > 247 && p[1] < 8 && p[2] < 8;
            std::printf("ENGINE_PROBE GBuffer %d center=%u,%u,%u,%u %s\n", size, p[0], p[1], p[2], p[3],
                        ok ? "PASS" : "FAIL");
            std::fflush(stdout);
            failures += !ok;
            std::ofstream out("gbuffer-" + std::to_string(size) + ".ppm", std::ios::binary);
            out << "P6\n" << image->getWidth() << ' ' << image->getHeight() << "\n255\n";
            for (int i = 0; i < image->getWidth() * image->getHeight(); ++i) out.write((const char*)bytes + i * 4, 3);
            gfx->begin3DFrame();
            gfx->present();
        }
        auto*                    canvas = gfx->newCanvas(64, 64);
        graphics::Lighting3DPack lights{};
        lights.ambient = glm::vec4(1);
        gfx->setBackgroundColorRGBA(0, 0, 0, 1);
        gfx->begin3DFrameToCanvas(canvas);
        gfx->setMesh3DViewProj(glm::mat4(1));
        gfx->setMesh3DView(glm::mat4(1));
        gfx->setMesh3DCameraPos({0, 0, 3});
        gfx->setMesh3DLighting(lights);
        gfx->drawMesh(mesh, glm::mat4(1), texture, graphics::Color(1, 1, 1, 1));
        gfx->end3DFrameToCanvas();
        auto c  = canvas->getPixel(32, 32);
        bool ok = c.r > .5f && c.g < .1f && c.b < .1f;
        std::printf("ENGINE_PROBE directCanvas center=%f,%f,%f %s\n", c.r, c.g, c.b, ok ? "PASS" : "FAIL");
        std::fflush(stdout);
        failures += !ok;
        return failures;
    }
    int runOcean() {
        auto*   gfx = graphics::Graphics::create();
        auto*   gpu = gpgpu::Gpgpu::create();
        ssq::VM vm(4096, ssq::Libs::ALL);
        auto    table = vm.addTable("eve");
        table.addClass<graphics::Graphics>("Graphics", std::function<graphics::Graphics*()>([] { return nullptr; }),
                                           false);
        table.addClass<gpgpu::Gpgpu>("Gpgpu", std::function<gpgpu::Gpgpu*()>([] { return nullptr; }), false);
        vm.addFunc("testGraphics", [gfx] { return gfx; });
        vm.addFunc("testCompute", [gpu] { return gpu; });
        fluids::exposeOceanTypes(table);
        auto source = [](const char* name) {
            std::ifstream input(name);
            if (!input) throw std::runtime_error(name);
            return std::string(std::istreambuf_iterator<char>(input), {});
        };
        std::printf("OCEAN_SETUP_START\n");
        std::fflush(stdout);
        vm.run(vm.compileSource(source("ocean-setup.nut").c_str()));
        std::printf("OCEAN_SETUP_DONE\n");
        std::fflush(stdout);
        beginOcean();
        vm.run(vm.compileSource("drawCpu();"));
        endOcean("cpu");
        beginOcean();
        vm.run(vm.compileSource("drawGpu();"));
        int failures = endOcean("gpu");
        std::printf("OCEAN_DRAW_DONE\n");
        std::fflush(stdout);
        vm.run(vm.compileSource(source("ocean-cleanup.nut").c_str()));
        std::printf("OCEAN_GC_DONE\n");
        std::fflush(stdout);
        return failures;
    }
    void beginOcean() {
        auto*     gfx = graphics::Graphics::create();
        glm::vec3 eye(0, 8, 10);
        auto      view       = glm::lookAtRH(eye, glm::vec3(0), glm::vec3(0, 1, 0));
        auto      projection = glm::perspectiveRH_ZO(glm::radians(55.0f), 160.0f / 120.0f, .1f, 500.0f);
        projection[1][1] *= -1;
        gfx->setScreenReadbackEnabled(true);
        gfx->begin3DFrame();
        gfx->setMesh3DViewProj(projection * view);
        gfx->setMesh3DView(view);
        gfx->setMesh3DCameraPos(eye);
        gfx->setDirectionalLight(.2f, 1.f, .4f);
    }
    int endOcean(const std::string& name) {
        auto* gfx = graphics::Graphics::create();
        gfx->present();
        std::vector<glm::vec3> pixels;
        for (int y = 12; y <= 108; y += 12)
            for (int x = 20; x <= 140; x += 12) pixels.emplace_back(gfx->getPixel(x, y));
        if (name == "cpu") {
            cpu_ = pixels;
            std::printf("ENGINE_PROBE Ocean CPU captured\n");
            std::fflush(stdout);
            return 0;
        }
        float peak = 0, difference = 0;
        for (size_t i = 0; i < pixels.size(); ++i) {
            peak       = std::max(peak, glm::length(cpu_.at(i)));
            difference = std::max(difference, glm::length(cpu_.at(i) - pixels[i]));
        }
        bool ok = peak > .2f && difference < .03f;
        std::printf("ENGINE_PROBE Ocean peak=%f difference=%f %s\n", peak, difference, ok ? "PASS" : "FAIL");
        std::fflush(stdout);
        return ok ? 0 : 1;
    }
    void finish() { window::Window::create()->close(); }
};
Module_IMPL(GpuProbe, new GpuProbe());
void GpuProbe::expose(ssq::Table& table) {
    auto cls = table.addClass(name, GpuProbe::create, false);
    expose(cls);
}
void GpuProbe::expose(ssq::Class& cls) {
    cls.addFunc("run", &GpuProbe::run);
    cls.addFunc("finish", &GpuProbe::finish);
    cls.addFunc("beginOcean", &GpuProbe::beginOcean);
    cls.addFunc("runOcean", &GpuProbe::runOcean);
    cls.addFunc("endOcean", &GpuProbe::endOcean);
}
}  // namespace eve::diagnostic
EVE_PLUGIN_EXPORT int eve_plugin_init() { return 0; }
