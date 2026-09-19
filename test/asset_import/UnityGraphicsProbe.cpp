#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <SDL2/SDL.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <iterator>
#include <string_view>
#include "asset/EvpackResourceReader.h"
#include "asset/graphics/EvpackGraphicsLoader.h"
#include "asset/graphics/EvpackImageLoader.h"
#include "asset/graphics/EvpackStaticPrefab.h"
#include "asset/scene/EvpackSceneTemplateLoader.h"
#include "common/CrashHandler.h"
#include "filesystem/FileData.h"
#include "graphics/AntiAliasing.h"
#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "graphics/Shadow.h"
#include "image/ImageData.h"
#include "window/Window.h"

int main(int argc, char** argv) try {
    eve::installCrashHandler();
    if (argc < 4 || argc > 6) {
        std::cerr << "usage: unity_graphics_probe <evpack> <output.png> <display.frag.spv> "
                     "[prefab-name|--tve-demo] [--interactive]\n";
        return 2;
    }
    const bool hasSelector = argc >= 5;
    const bool interactive = argc == 6 && std::string_view(argv[5]) == "--interactive";
    if (argc == 6 && !interactive) return 2;
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
    eve::window::Window*             window = nullptr;
    if (interactive) {
        window = eve::window::Window::create();
        if (!window) return 1;
        eve::window::WindowSettings settings;
        settings.width = 1400;
        settings.height = 1000;
        settings.centered = true;
        settings.resizable = true;
        if (!window->setWindowSettings(settings)) return 1;
        window->setWindowTitle("EVEngine - TVE Forest Quality Example");
    } else {
        gfx->initHeadless(1400, 1000);
    }
    eve::asset_graphics::GraphicsMeshFactoryAdapter  meshes(*gfx);
    eve::asset_graphics::GraphicsImageFactoryAdapter images(*gfx);
    eve::asset::EvpackCapabilities caps{"windows", "x86_64", gfx->getBackendName(), {"rgba8"}, {"spirv-1.6"},
                                        {"high"},  {}};
    const bool tveDemo = hasSelector && std::string_view(argv[4]) == "--tve-demo";
    const std::array<std::string_view, 6> demoNames{"TVE_Pine_Big", "TVE_Fern", "TVE_RedBush", "TVE_Grass_01",
                                                     "TVE_Rock_Ground", "TVE_Clover_01"};
    struct LoadedPrefab {
        std::string name;
        std::unique_ptr<eve::asset_graphics::EvpackStaticPrefab> prefab;
    };
    std::vector<LoadedPrefab> prefabs;
    for (const auto& chunk : pack->chunks()) {
        if (chunk.type != "eve.scene-template" || chunk.kind != eve::asset::EvpackChunkKind::Definition) continue;
        auto ref = eve::AssetRef::fromId(chunk.assetId);
        if (!ref) return 1;
        std::string prefabName;
        if (hasSelector) {
            auto scene = eve::asset_scene::EvpackSceneTemplateLoader(reader).load(ref.value(), caps);
            if (!scene) return 1;
            if (scene.value().root.children.empty()) continue;
            prefabName = scene.value().root.children.front().name;
            if (tveDemo) {
                if (std::find(demoNames.begin(), demoNames.end(), prefabName) == demoNames.end()) continue;
            } else if (prefabName != argv[4])
                continue;
        } else if (prefabs.size() >= 96)
            break;
        auto loaded = eve::asset_graphics::EvpackStaticPrefab::load(reader, meshes, images, ref.value(), caps);
        if (!loaded) {
            std::cerr << (prefabName.empty() ? "<unnamed>" : prefabName) << ": " << loaded.error()->message();
            if (tveDemo) {
                std::cerr << " (skipped)\n";
                continue;
            }
            return 1;
        }
        if (loaded.value()->drawCount()) prefabs.push_back({std::move(prefabName), std::move(loaded).takeValue()});
    }
    if (prefabs.empty() || prefabs.size() > 96) {
        std::cerr << "probe expects 1..96 static prefabs";
        return 1;
    }
    if (tveDemo && prefabs.size() != demoNames.size()) {
        std::cerr << "TVE demo requires every selected tree, shrub, grass and ground prefab";
        return 1;
    }
    struct DemoPlacement {
        LoadedPrefab* prefab = nullptr;
        glm::mat4 model{1.f};
    };
    std::vector<DemoPlacement> demoPlacements;
    auto place = [&](LoadedPrefab& loaded, const glm::vec3& position, float yaw, float scale) {
        demoPlacements.push_back({&loaded, glm::translate(glm::mat4(1.f), position) *
                                               glm::rotate(glm::mat4(1.f), yaw, glm::vec3(0.f, 1.f, 0.f)) *
                                               glm::scale(glm::mat4(1.f), glm::vec3(scale))});
    };
    if (tveDemo) {
        for (auto& loaded : prefabs) {
            if (loaded.name == "TVE_Pine_Big") {
                const std::array<glm::vec4, 7> trees{{{-1.f, 0.f, 7.f, 1.05f}, {-8.f, 0.f, 12.f, .78f},
                                                      {7.f, 0.f, 13.f, .86f}, {-12.f, 0.f, 20.f, 1.12f},
                                                      {1.f, 0.f, 21.f, .72f}, {11.f, 0.f, 24.f, .94f},
                                                      {-4.f, 0.f, 29.f, 1.08f}}};
                for (std::size_t i = 0; i < trees.size(); ++i)
                    place(loaded, glm::vec3(trees[i]), float(i) * .71f, trees[i].w);
            }
            if (loaded.name == "TVE_Rock_Ground") {
                const std::array<glm::vec4, 7> rocks{{{-5.f, -.2f, 5.f, 3.2f}, {5.f, -.2f, 8.f, 2.8f},
                                                      {-10.f, -.2f, 15.f, 3.6f}, {10.f, -.2f, 17.f, 3.3f},
                                                      {-3.f, -.2f, 20.f, 3.8f}, {5.f, -.2f, 25.f, 3.5f},
                                                      {-8.f, -.2f, 28.f, 4.1f}}};
                for (std::size_t i = 0; i < rocks.size(); ++i)
                    place(loaded, glm::vec3(rocks[i]), float(i) * .93f, rocks[i].w);
            }
            const int copies = loaded.name == "TVE_Grass_01" ? 56 : loaded.name == "TVE_Fern" ? 24
                               : loaded.name == "TVE_Clover_01" ? 28 : loaded.name == "TVE_RedBush" ? 14 : 0;
            for (int copy = 0; copy < copies; ++copy) {
                const float angle = float(copy) * 2.3999632f + float(loaded.name.size()) * .37f;
                const float radius = 2.5f + std::fmod(float(copy) * 3.17f, 12.f);
                const glm::vec3 position(std::cos(angle) * radius, 0.f,
                                         12.f + std::sin(angle) * radius + float(copy % 3) * 1.7f);
                const float scale = loaded.name == "TVE_Grass_01" ? .62f + float(copy % 4) * .08f
                                    : loaded.name == "TVE_Clover_01" ? .55f + float(copy % 3) * .1f
                                                                    : .8f + float(copy % 4) * .12f;
                place(loaded, position, angle, scale);
            }
        }
    }
    auto*           canvas     = gfx->newCanvas(1400, 1000);
    const glm::vec3 eye        = tveDemo ? glm::vec3(15.f, 8.f, -18.f)
                                         : hasSelector ? glm::vec3(60.f, 45.f, -65.f) : glm::vec3(36.f, 44.f, 38.f);
    const auto view = glm::lookAtRH(eye,
                                    tveDemo ? glm::vec3(-2.f, 4.5f, 14.f)
                                            : glm::vec3(0.f, hasSelector ? 6.f : 0.f, 0.f),
                                    glm::vec3(0, 1, 0));
    auto projection = tveDemo ? glm::perspectiveRH_ZO(glm::radians(48.f), 1.4f, 0.1f, 300.f)
                              : glm::orthoRH_ZO(-24.f, 24.f, -17.15f, 17.15f, 0.1f, 300.f);
    projection[1][1] *= -1.f;
    gfx->setMesh3DViewProj(projection * view);
    gfx->setMesh3DView(view);
    gfx->setMesh3DCameraPos(eye);
    gfx->setMesh3DClip(0.1f, 200.f);
    eve::graphics::Lighting3DPack lighting{};
    lighting.ambient             = glm::vec4(.95f, 1.f, .9f, 0.f);
    lighting.count               = tveDemo ? 2 : 1;
    lighting.lights[0].posRadius = glm::vec4(glm::normalize(glm::vec3(1, 2, 1)), 0.f);
    lighting.lights[0].color     = glm::vec4(2.35f, 2.18f, 1.88f, 0.f);
    if (tveDemo) {
        lighting.lights[1].posRadius = glm::vec4(glm::normalize(glm::vec3(-1.f, 1.2f, -.6f)), 0.f);
        lighting.lights[1].color     = glm::vec4(.48f, .62f, .78f, 0.f);
    }
    gfx->setMesh3DLighting(lighting);
    if (tveDemo) gfx->setBackgroundColorRGBA(.24f, .47f, .70f, 1.f);
    eve::graphics::Texture* skyCubemap = nullptr;
    eve::graphics::Mesh*    ground = nullptr;
    eve::graphics::ShadowUpload cachedShadows{};
    if (tveDemo) {
        const glm::vec3 lightDirection = glm::normalize(glm::vec3(1.f, 2.f, 1.f));
        cachedShadows = eve::graphics::buildDirectionalCSM(
            lightDirection, eye, {-2.f, 4.5f, 14.f}, {0.f, 1.f, 0.f}, glm::radians(48.f), 1.4f, .1f, 200.f,
            .0025f, .86f);
        for (int cascade = 0; cascade < eve::graphics::ShadowConfig::kCascades; ++cascade) {
            gfx->beginShadowPass(cascade);
            std::array<float, 16> lightViewProjection{};
            std::copy_n(glm::value_ptr(cachedShadows.ubo.lightVP[cascade]), 16, lightViewProjection.begin());
            for (const auto& placement : demoPlacements) {
                std::array<float, 16> transform{};
                std::copy_n(glm::value_ptr(placement.model), 16, transform.begin());
                auto drawn = placement.prefab->prefab->drawShadow(*gfx, transform, lightViewProjection);
                if (!drawn) {
                    std::cerr << drawn.error()->message();
                    return 1;
                }
            }
            gfx->endShadowPass();
        }
        gfx->setMesh3DShadows(cachedShadows);
    }
    gfx->begin3DFrameToCanvas(canvas);
    if (tveDemo) {
        constexpr int skySize = 128;
        std::vector<std::uint8_t> skyPixels(std::size_t(skySize) * skySize * 4);
        for (int y = 0; y < skySize; ++y) {
            const float v = float(y) / float(skySize - 1);
            for (int x = 0; x < skySize; ++x) {
                const float u = float(x) / float(skySize - 1);
                const float horizon = std::pow(v, .72f);
                float r = 18.f + horizon * 48.f;
                float g = 52.f + horizon * 82.f;
                float b = 128.f + horizon * 78.f;
                const float sunDistance = std::sqrt((u - .72f) * (u - .72f) + (v - .24f) * (v - .24f));
                const float sun = std::clamp((.12f - sunDistance) * 10.f, 0.f, 1.f);
                const float cloud = std::clamp((std::sin(u * 22.f + v * 7.f) + std::sin(u * 9.f - v * 13.f) - .9f) *
                                                   .18f * std::clamp((v - .28f) * 3.f, 0.f, 1.f),
                                               0.f, .22f);
                r = r * (1.f - cloud) + 235.f * cloud + 255.f * sun;
                g = g * (1.f - cloud) + 240.f * cloud + 226.f * sun;
                b = b * (1.f - cloud) + 245.f * cloud + 164.f * sun;
                const std::size_t pixel = (std::size_t(y) * skySize + std::size_t(x)) * 4;
                skyPixels[pixel + 0] = std::uint8_t(std::clamp(r, 0.f, 255.f));
                skyPixels[pixel + 1] = std::uint8_t(std::clamp(g, 0.f, 255.f));
                skyPixels[pixel + 2] = std::uint8_t(std::clamp(b, 0.f, 255.f));
                skyPixels[pixel + 3] = 255;
            }
        }
        std::vector<std::uint8_t> skyFaces(skyPixels.size() * 6);
        for (int face = 0; face < 6; ++face)
            std::copy(skyPixels.begin(), skyPixels.end(), skyFaces.begin() + std::ptrdiff_t(face) * skyPixels.size());
        skyCubemap = gfx->newCubemap(skySize, skyFaces.data());
        if (!skyCubemap) return 1;
        gfx->setMesh3DEnv(skyCubemap, .28f);

        const float groundPositions[]{-18.f, 0.f, 0.f, 18.f, 0.f, 0.f, 18.f, 0.f, 34.f, -18.f, 0.f, 34.f};
        const float groundNormals[]{0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f};
        const float groundUv[]{0.f, 0.f, 8.f, 0.f, 8.f, 8.f, 0.f, 8.f};
        const std::uint32_t groundIndices[]{0, 2, 1, 0, 3, 2};
        ground = gfx->newMeshFromArrays(groundPositions, groundNormals, groundUv, 4, groundIndices, 6);
        if (!ground) return 1;
        gfx->setMesh3DMaterial(0.f, .95f);
        gfx->setMesh3DShadowReceive(true);
        gfx->drawMesh(ground, glm::translate(glm::mat4(1.f), glm::vec3(0.f, -.24f, 0.f)), nullptr,
                      eve::graphics::Color(.012f, .05f, .009f, 1.f));
    }
    auto drawPrefab = [&](LoadedPrefab& loaded, const glm::mat4& matrix) {
        std::array<float, 16> transform;
        std::copy_n(glm::value_ptr(matrix), 16, transform.begin());
        std::array<float, 16> cameraView;
        std::copy_n(glm::value_ptr(view), 16, cameraView.begin());
        auto drawn = loaded.prefab->draw(*gfx, transform, cameraView);
        if (!drawn) {
            std::cerr << drawn.error()->message();
            return false;
        }
        return true;
    };
    if (tveDemo) {
        for (const auto& placement : demoPlacements)
            if (!drawPrefab(*placement.prefab, placement.model)) return 1;
    } else {
        for (std::size_t i = 0; i < prefabs.size(); ++i) {
            glm::vec3 position((float(i % 12) - 5.5f) * 3.f, 0.f, (float(i / 12) - 3.5f) * 3.f);
            if (hasSelector) position = {};
            if (!drawPrefab(prefabs[i], glm::translate(glm::mat4(1.f), position))) return 1;
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
    std::unique_ptr<eve::graphics::AntiAliasing> antiAliasing(gfx->newAntiAliasing());
    auto*                                        finalDisplay = display;
    if (tveDemo && antiAliasing) {
        antiAliasing->setMode("smaa");
        antiAliasing->setQuality("high");
        auto* antialiased = gfx->newCanvas(1400, 1000);
        if (!antialiased) return 1;
        antiAliasing->applyTo(gfx, display->getTexture(), antialiased);
        finalDisplay = antialiased;
    }
    // Readback belongs to the engine's offscreen canvas, never the desktop framebuffer.
    std::unique_ptr<eve::image::ImageData> image(finalDisplay->newImageData());
    if (!image) return 1;
    if (tveDemo) {
        std::size_t foreground = 0;
        int minX = image->getWidth(), minY = image->getHeight(), maxX = -1, maxY = -1;
        const auto background = image->getPixel(0, 0);
        for (int y = 0; y < image->getHeight(); y += 2) {
            for (int x = 0; x < image->getWidth(); x += 2) {
                const auto pixel = image->getPixel(x, y);
                const float chroma = std::max({pixel.r, pixel.g, pixel.b}) - std::min({pixel.r, pixel.g, pixel.b});
                const float backgroundDelta = std::abs(pixel.r - background.r) + std::abs(pixel.g - background.g) +
                                              std::abs(pixel.b - background.b);
                if (chroma < .035f || backgroundDelta < .12f) continue;
                ++foreground;
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
            }
        }
        const std::size_t samples = std::size_t((image->getWidth() + 1) / 2) * std::size_t((image->getHeight() + 1) / 2);
        std::cerr << "TVE demo foreground samples=" << foreground << '/' << samples << " bounds=" << minX << ','
                  << minY << ".." << maxX << ',' << maxY << '\n';
        if (foreground < samples / 14 || maxX - minX < image->getWidth() / 2 || maxY - minY < image->getHeight() / 2) {
            std::cerr << "TVE demo foreground coverage is below the visual acceptance threshold";
            return 1;
        }
    }
    std::unique_ptr<eve::filesystem::FileData> png(
        image->encode(medialoader::FormatHandler::ENCODED_PNG, "preview.png", false));
    if (!png) return 1;
    std::ofstream output(argv[2], std::ios::binary);
    output.write(static_cast<const char*>(png->getData()), std::streamsize(png->getSize()));
    output.close();
    if (interactive && window) {
        const glm::vec3 orbitTarget(-2.f, 4.5f, 14.f);
        const glm::vec3 orbitOffset = eye - orbitTarget;
        const std::uint32_t orbitStart = SDL_GetTicks();
        while (window->isOpen()) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT ||
                    (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE))
                    window->close();
            }
            if (!window->isOpen()) break;
            const float orbitAngle = float(SDL_GetTicks() - orbitStart) * .00012f;
            const glm::vec3 orbitEye = orbitTarget + glm::vec3(
                std::cos(orbitAngle) * orbitOffset.x + std::sin(orbitAngle) * orbitOffset.z,
                orbitOffset.y,
                -std::sin(orbitAngle) * orbitOffset.x + std::cos(orbitAngle) * orbitOffset.z);
            const glm::mat4 orbitView = glm::lookAtRH(orbitEye, orbitTarget, glm::vec3(0.f, 1.f, 0.f));
            gfx->setMesh3DViewProj(projection * orbitView);
            gfx->setMesh3DView(orbitView);
            gfx->setMesh3DCameraPos(orbitEye);
            gfx->setMesh3DShadows(cachedShadows);
            gfx->setMesh3DLighting(lighting);
            gfx->setMesh3DEnv(skyCubemap, .28f);
            gfx->begin3DFrameToCanvas(canvas);
            gfx->setMesh3DMaterial(0.f, .95f);
            gfx->setMesh3DShadowReceive(true);
            gfx->drawMesh(ground, glm::translate(glm::mat4(1.f), glm::vec3(0.f, -.24f, 0.f)), nullptr,
                          eve::graphics::Color(.012f, .05f, .009f, 1.f));
            std::array<float, 16> orbitCameraView{};
            std::copy_n(glm::value_ptr(orbitView), 16, orbitCameraView.begin());
            for (const auto& placement : demoPlacements) {
                std::array<float, 16> transform{};
                std::copy_n(glm::value_ptr(placement.model), 16, transform.begin());
                auto drawn = placement.prefab->prefab->draw(*gfx, transform, orbitCameraView);
                if (!drawn) {
                    std::cerr << drawn.error()->message();
                    window->close();
                    break;
                }
            }
            gfx->end3DFrameToCanvas();
            gfx->setCanvas(display);
            gfx->drawTexturedRectShader(canvas->getTexture(), transfer, 0, 0, 1400, 1000,
                                        eve::graphics::Color(1.f, 1.f, 1.f, 1.f));
            gfx->setCanvas();
            antiAliasing->applyTo(gfx, display->getTexture(), finalDisplay);
            gfx->clearScreen();
            gfx->drawTexturedRectRGBA(finalDisplay->getTexture(), 0.f, 0.f, float(gfx->getWidth()),
                                      float(gfx->getHeight()), 1.f, 1.f, 1.f, 1.f);
            gfx->present();
            SDL_Delay(16);
        }
    }
    for (auto& prefab : prefabs) {
        auto released = prefab.prefab->release();
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
