#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <assimp/mesh.h>
#include <assimp/scene.h>
#include <cstdint>
#include <vector>

#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "medialoader/model/ModelLoader.h"
#include "window/Window.h"

using namespace eve::graphics;

static const char kCubeObj[] =
    "v -0.5 -0.5 -0.5\n"
    "v  0.5 -0.5 -0.5\n"
    "v  0.5  0.5 -0.5\n"
    "v -0.5  0.5 -0.5\n"
    "v -0.5 -0.5  0.5\n"
    "v  0.5 -0.5  0.5\n"
    "v  0.5  0.5  0.5\n"
    "v -0.5  0.5  0.5\n"
    "f 1 2 3\n"
    "f 1 3 4\n"
    "f 5 6 7\n"
    "f 5 7 8\n"
    "f 1 2 6\n"
    "f 1 6 5\n"
    "f 2 3 7\n"
    "f 2 7 6\n"
    "f 3 4 8\n"
    "f 3 8 7\n"
    "f 4 1 5\n"
    "f 4 5 8\n";

static void openGfxWindow(eve::window::Window *&win, Graphics *&gfx) {
    win = eve::window::Window::create();
    gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 320;
    s.height = 240;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
}

// NOTE: Graphics is a process-wide singleton — one window lifetime per process.
TEST_CASE("Mesh.newMeshFromAssimpCube") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx);

    medialoader::ModelLoader loader;
    auto scene = loader.loadFromMemory(kCubeObj, sizeof(kCubeObj) - 1, ".obj");
    REQUIRE(!scene.empty());
    REQUIRE(scene->mNumMeshes >= 1);

    Mesh *m = gfx->newMeshFromAssimp(*scene->mMeshes[0]);
    REQUIRE(m != nullptr);
    REQUIRE_GT(m->indexCount, 0);

    win->close();
}

TEST_CASE("RenderSystem3D.smokeRotatingCube") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx);

    medialoader::ModelLoader loader;
    auto scene = loader.loadFromMemory(kCubeObj, sizeof(kCubeObj) - 1, ".obj");
    REQUIRE(!scene.empty());
    Mesh *mesh = gfx->newMeshFromAssimp(*scene->mMeshes[0]);
    REQUIRE(mesh != nullptr);

    auto *cam = Camera3D::createCamera();
    cam->data()->eyeZ = 3.f;

    auto *ent = Renderable3D::create();
    ent->meshRenderer()->mesh = mesh;

    std::vector<uint8_t> checker(16 * 16 * 4);
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            uint8_t c = ((x / 4) ^ (y / 4)) & 1 ? 220 : 40;
            size_t i = size_t(y * 16 + x) * 4;
            checker[i] = c;
            checker[i + 1] = c;
            checker[i + 2] = 255;
            checker[i + 3] = 255;
        }
    }
    ent->meshRenderer()->texture = gfx->newTexture(16, 16, checker.data());

    // Same-frame 2D overlay (also ensures present closes the open 3D pass).
    auto *hud = Renderable2D::create();
    hud->transform()->x = 4;
    hud->transform()->y = 4;
    hud->sprite()->width = 8;
    hud->sprite()->height = 8;
    hud->sprite()->r = 1.f;
    hud->sprite()->g = 0.2f;
    hud->sprite()->b = 0.2f;

    RenderSystem3D::setDirectionalLight(0.4f, 1.f, 0.3f, 1.f, 1.f, 1.f);

    for (int i = 0; i < 30; ++i) {
        ent->transform()->yaw = float(i) * 0.05f;
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }

    win->close();
}
