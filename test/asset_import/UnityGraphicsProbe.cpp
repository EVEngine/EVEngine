#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <iterator>
#include "asset/EvpackResourceReader.h"
#include "asset/graphics/EvpackGraphicsLoader.h"
#include "asset/graphics/EvpackImageLoader.h"
#include "asset/graphics/EvpackStaticPrefab.h"
#include "common/CrashHandler.h"
#include "filesystem/FileData.h"
#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "image/ImageData.h"

int main(int argc, char** argv) try {
    eve::installCrashHandler();
    if (argc != 4) {
        std::cerr << "usage: unity_graphics_probe <evpack> <output.png> <display.frag.spv>\n";
        return 2;
    }
    std::ifstream input(argv[1], std::ios::binary);
    if (!input) return 2;
    std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(input), {}};
    auto                      parsed = eve::asset::parseEvpack(bytes);
    if (!parsed) {
        std::cerr << parsed.error()->message();
        return 1;
    }
    auto                             pack = std::make_shared<const eve::asset::Evpack>(std::move(parsed).takeValue());
    eve::asset::EvpackResourceReader reader(pack);
    auto*                            gfx = eve::graphics::Graphics::create();
    gfx->initHeadless(1400, 1000);
    eve::asset_graphics::GraphicsMeshFactoryAdapter  meshes(*gfx);
    eve::asset_graphics::GraphicsImageFactoryAdapter images(*gfx);
    eve::asset::EvpackCapabilities caps{"windows", "x86_64", gfx->getBackendName(), {"rgba8"}, {"spirv-1.6"},
                                        {"high"},  {}};
    std::vector<std::unique_ptr<eve::asset_graphics::EvpackStaticPrefab>> prefabs;
    for (const auto& chunk : pack->chunks()) {
        if (chunk.type != "eve.scene-template" || chunk.kind != eve::asset::EvpackChunkKind::Definition) continue;
        auto ref = eve::AssetRef::fromId(chunk.assetId);
        if (!ref) return 1;
        auto loaded = eve::asset_graphics::EvpackStaticPrefab::load(reader, meshes, images, ref.value(), caps);
        if (!loaded) {
            std::cerr << loaded.error()->message();
            return 1;
        }
        if (loaded.value()->drawCount()) prefabs.push_back(std::move(loaded).takeValue());
    }
    if (prefabs.empty() || prefabs.size() > 96) {
        std::cerr << "probe expects 1..96 static prefabs";
        return 1;
    }
    auto*           canvas = gfx->newCanvas(1400, 1000);
    const glm::vec3 eye(36.f, 44.f, 38.f);
    const auto      view       = glm::lookAtRH(eye, glm::vec3(0.f), glm::vec3(0, 1, 0));
    auto            projection = glm::orthoRH_ZO(-24.f, 24.f, -17.15f, 17.15f, 0.1f, 200.f);
    projection[1][1] *= -1.f;
    gfx->setMesh3DViewProj(projection * view);
    gfx->setMesh3DView(view);
    gfx->setMesh3DCameraPos(eye);
    gfx->setMesh3DClip(0.1f, 200.f);
    eve::graphics::Lighting3DPack lighting{};
    lighting.ambient             = glm::vec4(0.65f, 0.65f, 0.65f, 0.f);
    lighting.count               = 1;
    lighting.lights[0].posRadius = glm::vec4(glm::normalize(glm::vec3(1, 2, 1)), 0.f);
    lighting.lights[0].color     = glm::vec4(1.f, 1.f, 1.f, 0.f);
    gfx->setMesh3DLighting(lighting);
    gfx->begin3DFrameToCanvas(canvas);
    for (std::size_t i = 0; i < prefabs.size(); ++i) {
        auto matrix =
            glm::translate(glm::mat4(1), glm::vec3((float(i % 12) - 5.5f) * 3.f, 0, (float(i / 12) - 3.5f) * 3.f));
        std::array<float, 16> transform;
        std::copy_n(glm::value_ptr(matrix), 16, transform.begin());
        auto drawn = prefabs[i]->draw(*gfx, transform);
        if (!drawn) {
            std::cerr << drawn.error()->message();
            return 1;
        }
    }
    gfx->end3DFrameToCanvas();
    // The offscreen 3D target stores linear light. Apply the display transfer on GPU
    // before encoding PNG, just as a presentation pass would for a UNORM target.
    auto*         display = gfx->newCanvas(1400, 1000);
    std::ifstream shaderFile(argv[3], std::ios::binary | std::ios::ate);
    if (!shaderFile || shaderFile.tellg() <= 0 || shaderFile.tellg() % 4 != 0) return 2;
    std::vector<std::uint32_t> shaderCode(std::size_t(shaderFile.tellg()) / 4);
    shaderFile.seekg(0);
    shaderFile.read(reinterpret_cast<char*>(shaderCode.data()), std::streamsize(shaderCode.size() * 4));
    if (!shaderFile) return 2;
    auto* transfer = gfx->newShaderFromSpv({}, shaderCode);
    if (!display || !transfer) return 1;
    gfx->setCanvas(display);
    gfx->drawTexturedRectShader(canvas->getTexture(), transfer, 0, 0, 1400, 1000,
                                eve::graphics::Color(1.f, 1.f, 1.f, 1.f));
    gfx->setCanvas();
    // Readback belongs to the engine's offscreen canvas, never the desktop framebuffer.
    std::unique_ptr<eve::image::ImageData> image(display->newImageData());
    if (!image) return 1;
    std::unique_ptr<eve::filesystem::FileData> png(
        image->encode(medialoader::FormatHandler::ENCODED_PNG, "preview.png", false));
    if (!png) return 1;
    std::ofstream output(argv[2], std::ios::binary);
    output.write(static_cast<const char*>(png->getData()), std::streamsize(png->getSize()));
    output.close();
    for (auto& prefab : prefabs) {
        auto released = prefab->release();
        if (!released) {
            std::cerr << released.error()->message();
            return 1;
        }
    }
    std::cout << "rendered " << prefabs.size() << " prefabs using " << gfx->getBackendName() << '\n';
    return output ? 0 : 2;
} catch (const std::exception& error) {
    std::cerr << "graphics probe failed: " << error.what() << '\n';
    return 1;
}
