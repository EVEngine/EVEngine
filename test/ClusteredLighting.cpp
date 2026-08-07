#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>

#include "graphics/ClusteredLight.h"
#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "window/Window.h"

using namespace eve::graphics;

static float luma(const Color &c) { return (c.r + c.g + c.b) / 3.f; }

static void openGfxWindow(eve::window::Window *&win, Graphics *&gfx, int w = 320, int h = 240) {
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

static void resetScene3D() {
    if (ecs::ComponentManager<Renderable3D>::inst().registy != nullptr) {
        auto view = ecs::View<Renderable3D, Renderable3D::MeshRenderer>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [mr] = *it;
            mr->visible = false;
        }
    }
    if (ecs::ComponentManager<Camera3D>::inst().registy != nullptr) {
        auto camView = ecs::View<Camera3D, Camera3D::Data>();
        for (auto it = camView.begin(); it != camView.end(); ++it) {
            auto [data] = *it;
            data->active = false;
        }
    }
    if (ecs::ComponentManager<Light3D>::inst().registy != nullptr) {
        auto lightView = ecs::View<Light3D, Light3D::Data>();
        for (auto it = lightView.begin(); it != lightView.end(); ++it) {
            auto [d] = *it;
            d->enabled = false;
        }
    }
    if (ecs::ComponentManager<Renderable2D>::inst().registy != nullptr) {
        auto view = ecs::View<Renderable2D, Renderable2D::Sprite>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [sp] = *it;
            sp->visible = false;
        }
    }
}

static Texture *makeSolidGray(Graphics *gfx, uint8_t g) {
    uint8_t px[4] = {g, g, g, 255};
    return gfx->newTexture(1, 1, px);
}

TEST_CASE("ClusteredLighting.buildProducesNonEmptyTables") {
    std::vector<ClusteredLightGpu> points;
    for (int i = 0; i < 32; ++i) {
        ClusteredLightGpu L{};
        const float a = float(i) * 0.2f;
        L.posRadius = glm::vec4(std::cos(a) * 1.5f, 0.f, std::sin(a) * 1.5f, 2.f);
        L.color = glm::vec4(1.f, 1.f, 1.f, 1.f);
        points.push_back(L);
    }
    std::vector<ClusteredLightGpu> dirs;
    ClusteredLightGpu d{};
    d.posRadius = glm::vec4(0.f, 1.f, 0.f, 0.f);
    d.color = glm::vec4(0.2f, 0.2f, 0.2f, 1.f);
    dirs.push_back(d);

    glm::mat4 view = glm::lookAtRH(glm::vec3(0, 0, 3), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    auto upload = buildClusteredLighting(points, dirs, view, 0.1f, 100.f, 320, 240, 1.0f,
                                         glm::vec4(0.1f, 0.1f, 0.1f, 0.f));
    REQUIRE(upload.active);
    CHECK_EQ(upload.lights.size(), 32u);
    CHECK_EQ(upload.clusterTable.size(), size_t(ClusteredLightConfig::kClusterCount));
    CHECK(!upload.lightIndices.empty());
    uint32_t total = 0;
    for (const auto &e : upload.clusterTable) total += e.count;
    CHECK_GT(total, 0u);
}

TEST_CASE("ClusteredLighting.manyPointLightsBrightenCenter") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx);
    resetScene3D();

    Mesh *mesh = gfx->newMeshSphere(24, 12);
    REQUIRE(mesh != nullptr);

    auto *cam = Camera3D::createCamera();
    cam->data()->eyeZ = 3.f;
    cam->setAmbient(0.04f, 0.04f, 0.04f);

    auto *ent = Renderable3D::create();
    ent->meshRenderer()->mesh = mesh;
    ent->meshRenderer()->texture = makeSolidGray(gfx, 220);
    ent->setMetallic(0.05f);
    ent->setRoughness(0.55f);

    auto *hud = Renderable2D::create();
    hud->transform()->x = 0;
    hud->transform()->y = 0;
    hud->sprite()->width = 2;
    hud->sprite()->height = 2;

    // >8 lights triggers clustered path.
    std::vector<Light3D *> lights;
    for (int i = 0; i < 24; ++i) {
        auto *L = Light3D::createLight("point");
        const float a = float(i) * (6.2831853f / 24.f);
        L->setPosition(std::cos(a) * 1.2f, 0.3f, std::sin(a) * 1.2f);
        L->setColor(1.f, 1.f, 1.f, 1.8f);
        L->setRadius(3.5f);
        lights.push_back(L);
    }
    auto *dir = Light3D::createLight("dir");
    dir->setDirection(0.f, 0.4f, 1.f);
    dir->setColor(0.15f, 0.15f, 0.15f, 1.f);

    gfx->setScreenReadbackEnabled(true);
    for (int i = 0; i < 3; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }
    const float L_on = luma(gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2));

    for (auto *L : lights) L->setEnabled(false);
    for (int i = 0; i < 3; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }
    const float L_off = luma(gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2));

    REQUIRE(L_on > L_off + 0.04f);

    dir->setEnabled(false);
    win->close();
}
