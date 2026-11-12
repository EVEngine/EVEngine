// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "default": "render-window",
  "module": "procgen_recursive_graphics"
}
*/
// clang-format on

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "procgen/MeshBuild.h"
#include "procgen/recursive/RecursiveInstance.h"
#include "procgen/recursive_graphics/FrontierMesh.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace {
double difference(const eve::graphics::Color& a, const eve::graphics::Color& b) {
    return std::abs(a.r - b.r) + std::abs(a.g - b.g) + std::abs(a.b - b.b);
}
// Capture the engine's presented-frame readback, never the desktop framebuffer.
void capture(eve::graphics::Graphics& graphics, const char* name) {
    const char* directory = std::getenv("EVE_MULTISCALE_CAPTURE_DIR");
    if (!directory || !*directory) return;
    const auto path = std::filesystem::path(directory) / name;
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    REQUIRE(out.good());
    out << "P6\n640 360\n255\n";
    for (int y = 0; y < 360; ++y)
        for (int x = 0; x < 640; ++x) {
            const auto color = graphics.getPixel(x, y);
            for (float value : {color.r, color.g, color.b}) {
                // getPixel returns linear Color; encode it for ordinary image viewers.
                const float linear = std::clamp(value, 0.f, 1.f);
                const float encoded =
                    linear <= 0.0031308f ? 12.92f * linear : 1.055f * std::pow(linear, 1.f / 2.4f) - 0.055f;
                const auto byte = static_cast<unsigned char>(std::lround(encoded * 255.f));
                out.put(static_cast<char>(byte));
            }
        }
    out.close();
    REQUIRE(out.good());
}
}  // namespace

TEST_CASE("procgen.recursiveGraphics.nativeFramesReflectEditsAndPreserveRejectedCandidates") {
    using namespace eve::procgen::recursive;
    using namespace eve::procgen::recursive_graphics;
    auto*                       window   = eve::window::Window::create();
    auto*                       graphics = eve::graphics::Graphics::create();
    eve::window::WindowSettings settings;
    settings.width  = 640;
    settings.height = 360;
    REQUIRE(window->setWindowSettings(settings));
    graphics->setScreenReadbackEnabled(true);
    graphics->setBackgroundColor({0.02f, 0.03f, 0.05f, 1.f});
    const glm::vec3 eye(0.f, 0.f, 8.f);
    const auto      view       = glm::lookAtRH(eye, glm::vec3(0.f), glm::vec3(0.f, 1.f, 0.f));
    auto            projection = glm::orthoRH_ZO(-4.f, 4.f, -2.25f, 2.25f, 0.1f, 20.f);
    projection[1][1] *= -1.f;
    auto draw = [&](eve::graphics::Mesh* mesh) {
        graphics->begin3DFrame();
        graphics->setMesh3DViewProj(projection * view);
        graphics->setMesh3DView(view);
        graphics->setMesh3DCameraPos(eye);
        graphics->setDirectionalLight(0.2f, 0.6f, 1.f);
        if (mesh) graphics->drawMesh(mesh, glm::mat4(1.f), nullptr, {0.2f, 0.8f, 0.5f, 1.f});
        graphics->present();
    };
    draw(nullptr);
    const auto      background = graphics->getPixel(280, 180);
    ModelDefinition definition;
    definition.capacity       = 2;
    definition.lengthExponent = 900;
    auto instance             = RecursiveInstance::create(definition, {});
    REQUIRE(instance.ok());
    const std::array<NodeKey, 2> keys{{{2}, {3}}};
    auto                         upload = [&](const FrontierMesh& result) {
        const auto& mesh = *result.mesh;
        return graphics->newMeshFromArrays(mesh.positions().data(), mesh.normals().data(), mesh.uvs().data(),
                                                                   mesh.getVertexCount(), mesh.indices().data(), mesh.getIndexCount());
    };
    auto initial = buildFrontierMesh(instance.value(), keys, {});
    REQUIRE(initial.ok());
    auto* oldMesh = upload(initial.value());
    REQUIRE(oldMesh != nullptr);
    for (int frame = 0; frame < 3; ++frame) draw(oldMesh);
    CHECK(difference(graphics->getPixel(280, 180), background) > 0.15);
    CHECK(difference(graphics->getPixel(360, 180), background) > 0.15);
    capture(*graphics, "frontier-before.ppm");

    const std::array<LeafEdit, 2> edits{{{0, 0, {}, {}}, {1, 1, {2, 0, 0}, {}}}};
    auto                          candidate = instance.value().prepareEdits(edits);
    REQUIRE(candidate.ok());
    auto committed = instance.value().commit(std::move(candidate.value()));
    REQUIRE(committed.ok());
    auto edited = buildFrontierMesh(instance.value(), keys, {});
    REQUIRE(edited.ok());
    auto* editedMesh = upload(edited.value());
    REQUIRE(editedMesh != nullptr);
    REQUIRE(graphics->releaseMesh(oldMesh));
    delete oldMesh;
    for (int frame = 0; frame < 3; ++frame) draw(editedMesh);
    CHECK(difference(graphics->getPixel(280, 180), background) < 0.02);
    CHECK(difference(graphics->getPixel(360, 180), background) < 0.02);
    const auto moved = graphics->getPixel(520, 180);
    CHECK(difference(moved, background) > 0.15);
    capture(*graphics, "frontier-after.ppm");

    FrontierMeshPolicy rejectedPolicy;
    rejectedPolicy.maxNodes = 1;
    auto rejected           = buildFrontierMesh(instance.value(), keys, rejectedPolicy);
    REQUIRE(!rejected.ok());
    draw(editedMesh);
    CHECK(difference(graphics->getPixel(520, 180), moved) < 0.02);
    REQUIRE(graphics->releaseMesh(editedMesh));
    delete editedMesh;
    draw(nullptr);
    CHECK(difference(graphics->getPixel(520, 180), background) < 0.02);
    window->close();
}
