#include <cstdint>
#include <vector>

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/shaders/aa_fxaa_frag_spv.inc"
#include "window/Window.h"

// ---------------------------------------------------------------------------
// Ownership contract under test
// ---------------------------------------------------------------------------
// Handles returned by Graphics::newTexture / newMesh* / newShader* are
// *borrowed*: the Graphics backend owns both the CPU facade and the GPU
// resource, and callers must not delete the handle directly (doing so double-
// frees the CPU facade the backend still tracks).
//
// Eager cleanup goes through releaseTexture / releaseMesh / releaseShader:
//   - returns true  when the resource was owned by this Graphics and has been
//     released (GPU resource freed, handle detached, gpuHandle == nullptr);
//   - returns false for null / foreign / internal / already-released handles;
//   - after a successful release the CPU facade is caller-owned and may be
//     deleted safely.
// ---------------------------------------------------------------------------

namespace {

using WindowSettings = eve::window::WindowSettings;

void openWindow(eve::window::Window *&win, eve::graphics::Graphics *&gfx, int w = 128,
                int h = 128) {
    win = eve::window::Window::create();
    gfx = eve::graphics::Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    WindowSettings s;
    s.width      = static_cast<uint16_t>(w);
    s.height     = static_cast<uint16_t>(h);
    s.centered   = true;
    s.fullscreen = false;
    s.resizable  = true;
    REQUIRE(win->setWindowSettings(s));
}

std::vector<uint8_t> rgbaPixels(int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    std::vector<uint8_t> px(size_t(w) * size_t(h) * 4);
    for (size_t i = 0; i < px.size(); i += 4) {
        px[i]     = r;
        px[i + 1] = g;
        px[i + 2] = b;
        px[i + 3] = a;
    }
    return px;
}

}  // namespace

TEST_CASE("graphics.resourceLifetime.textureHandleIsBorrowedUntilReleased") {
    eve::window::Window *win = nullptr;
    eve::graphics::Graphics *gfx = nullptr;
    openWindow(win, gfx);

    auto px = rgbaPixels(8, 8, 255, 0, 0, 255);
    eve::graphics::Texture *tex = gfx->newTexture(8, 8, px.data());
    REQUIRE(tex != nullptr);
    // A freshly created texture is backed by a live GPU resource.
    REQUIRE(tex->gpuHandle != nullptr);

    // Eager release detaches the handle and frees the GPU resource.
    CHECK(gfx->releaseTexture(tex));
    CHECK(tex->gpuHandle == nullptr);

    // Idempotent: a second release is a no-op.
    CHECK(!gfx->releaseTexture(tex));

    // After release the CPU facade is caller-owned and safe to delete.
    delete tex;

    // Null handles are rejected.
    CHECK(!gfx->releaseTexture(nullptr));

    win->close();
}

TEST_CASE("graphics.resourceLifetime.meshHandleIsBorrowedUntilReleased") {
    eve::window::Window *win = nullptr;
    eve::graphics::Graphics *gfx = nullptr;
    openWindow(win, gfx);

    eve::graphics::Mesh *mesh = gfx->newMeshCube(1.f);
    REQUIRE(mesh != nullptr);
    REQUIRE(mesh->gpuHandle != nullptr);

    CHECK(gfx->releaseMesh(mesh));
    CHECK(mesh->gpuHandle == nullptr);
    CHECK(!gfx->releaseMesh(mesh));
    delete mesh;
    CHECK(!gfx->releaseMesh(nullptr));

    win->close();
}

TEST_CASE("graphics.resourceLifetime.shaderHandleIsBorrowedUntilReleased") {
    eve::window::Window *win = nullptr;
    eve::graphics::Graphics *gfx = nullptr;
    openWindow(win, gfx);

    // Empty vertex stage selects the default textured vertex shader; the FXAA
    // fragment shader is a plain textured-pipeline post shader.
    std::vector<uint32_t> frag(aa_fxaa_frag_spv, aa_fxaa_frag_spv + aa_fxaa_frag_spv_count);
    if (gfx->getBackendName() == "webgpu") {
        // The WebGPU backend rejects SPIR-V custom shaders (browser WGSL only),
        // so there is no shader handle to release on that backend.
        bool threw = false;
        try {
            (void)gfx->newShaderFromSpv({}, frag);
        } catch (const std::exception &) {
            threw = true;
        }
        CHECK(threw);
        win->close();
        return;
    }
    eve::graphics::Shader *sh = gfx->newShaderFromSpv({}, frag);
    REQUIRE(sh != nullptr);
    REQUIRE(sh->gpuHandle != nullptr);

    CHECK(gfx->releaseShader(sh));
    CHECK(sh->gpuHandle == nullptr);
    CHECK(!gfx->releaseShader(sh));
    delete sh;
    CHECK(!gfx->releaseShader(nullptr));

    win->close();
}
