// clang-format off
/* eve-test-policy:
{"schema":"evengine.test-policy","version":1,"default":"render-window","module":"graphics","modules":["graphics","window","image"]}
*/
// clang-format on
#include "Fixtures.h"
#include "common/RenderTrace.h"
#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "graphics/Material.h"
#include "graphics/RenderControl.h"
#include "graphics/RenderSystem3D.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>
#include <string_view>
#include <vector>

namespace {
using namespace eve::graphics;

struct InstanceTrace final : eve::debug::IRenderTracer {
    size_t batches = 0;
    InstanceTrace() { eve::debug::setRenderTracer(this); }
    ~InstanceTrace() override { eve::debug::setRenderTracer(nullptr); }
    void draw(const char *api, const char *) override {
        if (std::string_view(api) == "drawMeshInstances") ++batches;
    }
};

void simpleForward(Graphics &gfx) {
    for (const char *feature : {"gbuffer", "gbufferAlbedo", "ao", "gi", "shadow", "aa", "msaa", "atmosphere"})
        gfx.getRenderControl()->disable(feature);
    gfx.getRenderControl()->compile();
    gfx.setVSync(false);
    gfx.setScreenReadbackEnabled(true);
    gfx.setBackgroundColor(Color(0, 0, 0, 1));
}

std::vector<Color> pixels(Graphics &gfx) {
    std::vector<Color> result;
    for (int y = 0; y < gfx.getHeight(); ++y)
        for (int x = 0; x < gfx.getWidth(); ++x) result.push_back(gfx.getPixel(x, y));
    return result;
}

void comparePixels(const std::vector<Color> &expected, const std::vector<Color> &actual) {
    REQUIRE_EQ(expected.size(), actual.size());
    size_t edges = 0, visible = 0;
    double total = 0;
    for (size_t i = 0; i < actual.size(); ++i) {
        const float error = std::max({std::abs(expected[i].r - actual[i].r), std::abs(expected[i].g - actual[i].g),
                                      std::abs(expected[i].b - actual[i].b)});
        total += error;
        edges += error > 2.f / 255.f;
        visible += expected[i].r + expected[i].g + expected[i].b > .1f;
    }
    REQUIRE_GT(visible, size_t(100));
    // CPU/GPU view-projection multiplication may round coverage differently at edges.
    CHECK_LE(edges, size_t(12));
    CHECK_LT(total / actual.size(), .001);
}
}  // namespace

TEST_CASE("graphics.meshInstances.snapshotsMatchIndividualDrawsAndValidateBeforeRecording") {
    GfxFixture fixture(192, 128);
    auto      &gfx  = *fixture.gfx;
    auto      *mesh = gfx.newMeshCube(.6f);
    REQUIRE(mesh != nullptr);
    std::array<glm::mat4, 2> models{glm::translate(glm::mat4(1), glm::vec3(-1.5f, -.5f, 0)),
                                    glm::translate(glm::mat4(1), glm::vec3(-1.5f, .5f, 0))};
    if (!gfx.supportsMeshInstancing()) {
        auto unavailable = gfx.drawMeshInstances(*mesh, models, {}, Color(1, 0, 0, 1));
        REQUIRE(!unavailable.ok());
        CHECK(unavailable.status().describe().find("unavailable") != std::string::npos);
        return;
    }
    auto closed = gfx.drawMeshInstances(*mesh, models, {}, Color(1, 0, 0, 1));
    REQUIRE(!closed.ok());
    simpleForward(gfx);
    Lighting3DPack lighting{};
    lighting.ambient = glm::vec4(1, 1, 1, 0);
    gfx.setMesh3DLighting(lighting);
    const auto view       = glm::lookAtRH(glm::vec3(0, 0, 4), glm::vec3(0), glm::vec3(0, 1, 0));
    auto       projection = glm::orthoRH_ZO(-3.f, 3.f, -2.f, 2.f, .1f, 10.f);
    projection[1][1] *= -1.f;
    gfx.setMesh3DView(view);
    gfx.setMesh3DViewProj(projection * view);
    gfx.setMesh3DCameraPos(glm::vec3(0, 0, 4));
    gfx.setMesh3DMaterial(0, .8f);
    std::vector<Color> reference;
    for (int mode = 0; mode < 2; ++mode) {
        for (int frame = 0; frame < 4; ++frame) {
            gfx.clearScreen();
            gfx.begin3DFrame();
            if (mode) {
                auto invalidModels     = models;
                invalidModels[0][0][0] = std::numeric_limits<float>::quiet_NaN();
                auto invalid           = gfx.drawMeshInstances(*mesh, invalidModels, {}, Color(1, 0, 0, 1));
                REQUIRE(!invalid.ok());
                REQUIRE(gfx.drawMeshInstances(*mesh, {}, {}, Color(1, 0, 0, 1)).ok());
            }
            auto left  = models;
            auto right = models;
            for (auto &m : right) m[3].x = 1.5f;
            if (mode) {
                REQUIRE(gfx.drawMeshInstances(*mesh, left, {}, Color(1, 0, 0, 1)).ok());
                // Mutating caller memory after submission must not alter the recorded red batch.
                for (auto &m : left) m[3].x = 100.f;
                REQUIRE(gfx.drawMeshInstances(*mesh, right, {}, Color(0, 0, 1, 1)).ok());
            } else {
                for (const auto &m : left) gfx.drawMesh(mesh, m, nullptr, Color(1, 0, 0, 1));
                for (const auto &m : right) gfx.drawMesh(mesh, m, nullptr, Color(0, 0, 1, 1));
            }
            // Ordinary draws following batches must not inherit the instance flag or matrix range.
            gfx.drawMesh(mesh, glm::mat4(1), nullptr, Color(0, 1, 0, 1));
            gfx.present();
        }
        if (!mode)
            reference = pixels(gfx);
        else
            comparePixels(reference, pixels(gfx));
    }
}

TEST_CASE("graphics.meshInstances.sharedMaterialCompositionMatchesIndividualGeometry") {
    GfxFixture fixture(192, 128);
    auto      &gfx = *fixture.gfx;
    if (!gfx.supportsMeshInstancing()) {
        CHECK(!gfx.supportsMeshInstancing());
        return;
    }
    simpleForward(gfx);
    auto *camera = Camera3D::createCamera();
    camera->setEye(0, 0, 10);
    camera->setTarget(0, 0, 0);
    camera->setAmbient(.3f, .3f, .3f);
    auto *light = Light3D::createLight("dir");
    light->setDirection(.3f, 1.f, .5f);
    light->setColor(1, .9f, .8f, 2);
    light->setCastShadow(false);
    auto *mesh = gfx.newMeshCube(.3f);
    REQUIRE(mesh != nullptr);
    auto material = [&]() {
        auto *value = gfx.newMaterial();
        value->setTint(.8f, .3f, .1f, 1);
        value->setMetallic(.1f);
        value->setRoughness(.7f);
        return value;
    };
    auto                       *shared = material();
    std::vector<Renderable3D *> entities;
    for (int i = 0; i < 256; ++i) {
        auto *entity = Renderable3D::create();
        entity->setMesh(mesh);
        entity->setMaterial(material());
        entity->setPosition(float(i % 16) * .4f - 3.f, float(i / 16) * .4f - 3.f, 0);
        entity->setScale(.7f, .5f + float(i % 3) * .2f, .8f);
        entity->setRotation(float(i % 11) * 9.f, 0, 0);
        entity->setCastShadow(false);
        entities.push_back(entity);
    }
    InstanceTrace trace;
    auto          render = [&]() {
        trace.batches = 0;
        for (int i = 0; i < 4; ++i) {
            gfx.clearScreen();
            RenderSystem3D::render(gfx);
            gfx.present();
        }
        return pixels(gfx);
    };
    const auto reference = render();
    REQUIRE_EQ(trace.batches, size_t(0));
    for (auto *entity : entities) entity->setMaterial(shared);
    comparePixels(reference, render());
    REQUIRE_GT(trace.batches, size_t(0));
    // Different per-object shadow-receive policies must not be merged into one draw.
    for (size_t i = 0; i < entities.size(); ++i) entities[i]->setReceiveShadow(i % 2 == 0);
    comparePixels(reference, render());
    REQUIRE_EQ(trace.batches, size_t(0));
}
