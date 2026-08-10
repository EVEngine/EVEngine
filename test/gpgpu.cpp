#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "gpgpu/Gpgpu.h"
#include "gpgpu/ComputeShader.h"
#include "gpgpu/GpuBuffer.h"
#include "graphics/Graphics.h"
#include "window/Window.h"

#include <cmath>
#include <string>

using namespace eve::gpgpu;

namespace {

bool tryInitGpuWindow() {
    auto *win = eve::window::Window::create();
    auto *gfx = eve::graphics::Graphics::create();
    if (!win || !gfx) return false;
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 320;
    s.height = 240;
    s.centered = true;
    return win->setWindowSettings(s);
}

const char *kScaleKernel = R"(#version 450
layout(local_size_x = 64) in;
layout(set = 0, binding = 0) buffer Data {
    float values[];
} data;
layout(push_constant) uniform PC {
    float data[32];
} pc;
void main() {
    uint i = gl_GlobalInvocationID.x;
    data.values[i] *= pc.data[0];
}
)";

}  // namespace

TEST_CASE("gpgpu.module.create") {
    auto *mod = Gpgpu::create();
    REQUIRE(mod != nullptr);
    CHECK_EQ(mod->getName(), std::string("Gpgpu"));
}

TEST_CASE("gpgpu.graphics.backendName") {
    if (!tryInitGpuWindow()) return;
    auto *gfx = eve::graphics::Graphics::create();
    REQUIRE(gfx != nullptr);
    CHECK_EQ(gfx->getBackendName(), std::string("vulkan"));
}

TEST_CASE("gpgpu.newShaderFromSpvFile.delegatesWhenMissing") {
    if (!tryInitGpuWindow()) return;
    auto *mod = Gpgpu::create();
    if (!mod->isAvailable()) return;
    bool threw = false;
    try {
        mod->newShaderFromSpvFile("__eve_missing_compute.spv");
    } catch (...) {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("gpgpu.dispatch.scaleFloats") {
    if (!tryInitGpuWindow()) return;
    auto *mod = Gpgpu::create();
    REQUIRE(mod->isAvailable());

    const int count = 128;
    GpuBuffer *buf = mod->newBuffer(count * int(sizeof(float)), "storage");
    REQUIRE(buf != nullptr);
    CHECK_EQ(buf->getSize(), count * int(sizeof(float)));

    for (int i = 0; i < count; ++i)
        buf->writeFloat32(i, float(i + 1));

    ComputeShader *shader = nullptr;
    try {
        shader = mod->newShader(kScaleKernel);
    } catch (...) {
        // glslc may be missing in some CI images — skip GPU path.
        return;
    }
    REQUIRE(shader != nullptr);
    shader->bindBuffer(0, buf);
    shader->setFloat(0, 2.f);

    const int groups = (count + 63) / 64;
    mod->dispatch(shader, groups, 1, 1);

    CHECK(std::fabs(buf->readFloat32(0) - 2.f) < 1e-4f);
    CHECK(std::fabs(buf->readFloat32(1) - 4.f) < 1e-4f);
    CHECK(std::fabs(buf->readFloat32(count - 1) - float(count) * 2.f) < 1e-3f);

    delete shader;
    delete buf;
}
