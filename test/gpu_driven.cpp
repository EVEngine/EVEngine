#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <SDL2/SDL.h>

#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/Texture.h"
#include "graphics/vulkan/Graphics.h"
#include "graphics/vulkan/GpuDriven.h"
#include "window/Window.h"

using namespace eve::graphics;

namespace {

void openGfxWindow(eve::window::Window *&win, Graphics *&gfx, int w = 320, int h = 240) {
    win = eve::window::Window::create();
    gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = w;
    s.height = h;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
}

}  // namespace

TEST_CASE("GpuDriven.capsAvailable") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx);
    auto *vg = dynamic_cast<eve::graphics::vulkan::Graphics *>(gfx);
    REQUIRE(vg != nullptr);
    CHECK(vg->gpuDrivenCaps().gpuDrivenAvailable());
    win->close();
}

TEST_CASE("GpuDriven.bindlessTextureRegistration") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx);
    auto *vg = dynamic_cast<eve::graphics::vulkan::Graphics *>(gfx);
    REQUIRE(vg != nullptr);
    if (!vg->gpuDrivenCaps().gpuDrivenAvailable()) {
        win->close();
        return;  // environment without 1.2 features: nothing to validate
    }

    const uint8_t red[4] = {255, 0, 0, 255};
    const uint8_t green[4] = {0, 255, 0, 255};
    Texture *a = gfx->newTexture(1, 1, red);
    Texture *b = gfx->newTexture(1, 1, green);
    const uint32_t sa = vg->debugBindlessIndex(a);
    const uint32_t sb = vg->debugBindlessIndex(b);
    CHECK(sa != eve::graphics::vulkan::kInvalidBindlessSlot);
    CHECK(sb != eve::graphics::vulkan::kInvalidBindlessSlot);
    CHECK(sa != sb);  // distinct slots

    const std::vector<uint8_t> cubePx(6 * 4, 128);
    Texture *cube = gfx->newCubemap(1, cubePx.data());
    auto *gpuCube = static_cast<eve::graphics::vulkan::GpuTexture *>(cube->gpuHandle);
    CHECK(gpuCube->bindlessIndexCube != eve::graphics::vulkan::kInvalidBindlessSlot);
    win->close();
}

TEST_CASE("GpuDriven.meshTableRegistration") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx);
    auto *vg = dynamic_cast<eve::graphics::vulkan::Graphics *>(gfx);
    REQUIRE(vg != nullptr);
    if (!vg->gpuDrivenCaps().gpuDrivenAvailable()) {
        win->close();
        return;
    }

    Mesh *m = gfx->newMeshSphere(8, 4);
    REQUIRE(m != nullptr);
    const uint32_t idx = vg->debugMeshRecordIndex(m);
    CHECK(idx != eve::graphics::vulkan::kInvalidBindlessSlot);
    auto *gpu = static_cast<eve::graphics::vulkan::GpuMesh *>(m->gpuHandle);
    CHECK(gpu->record.vertexCount > 0);
    CHECK(gpu->record.indexCount > 0);
    CHECK(gpu->record.boundsCenterRadius.w > 0.f);  // bounds computed at upload
    win->close();
}
