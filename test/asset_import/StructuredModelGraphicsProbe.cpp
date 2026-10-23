#include "asset/AssetCooker.h"
#include "asset/CanonicalMesh.h"
#include "asset/graphics/EvpackGraphicsLoader.h"
#include "asset/graphics/EvpackImageLoader.h"
#include "asset/graphics/EvpackStaticPrefab.h"
#include "asset/import/StructuredModelImporter.h"
#include "common/CrashHandler.h"
#include "filesystem/FileData.h"
#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "image/ImageData.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>

namespace {
std::vector<std::uint8_t> read(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot read " + path);
    return {std::istreambuf_iterator<char>(input), {}};
}
template <class T>
T checked(eve::Result<T> result) {
    if (!result) throw std::runtime_error(result.error()->message());
    return std::move(result).takeValue();
}
void checked(eve::Result<void> result) {
    if (!result) throw std::runtime_error(result.error()->message());
}
}  // namespace

int main(int argc, char** argv) try {
    eve::installCrashHandler();
    if (argc < 5 || argc > 6) {
        std::cerr << "usage: structured_model_graphics_probe <scad|ldraw> <input> <output.png> <display.frag.spv> "
                     "[openscad]\n";
        return 2;
    }
    using namespace eve;
    asset_import::ImportPackageIdentity identity{
        *PersistentId::parse("018f6f22-2490-7ad2-bf58-4f1dbca31040"),
        "structured-model-probe",
        "1.0.0",
        {{"provider", "local"}, {"license", Value::Object{{"redistribution", "project-only"}}}}};
    auto prepared = [&]() {
        if (std::string_view(argv[1]) == "scad")
            return checked(asset_import::prepareScadFileImport({identity, argv[2], argc == 6 ? argv[5] : "openscad"}));
        if (std::string_view(argv[1]) != "ldraw") throw std::runtime_error("unknown probe format");
        const auto path = std::filesystem::path(argv[2]);
        return checked(asset_import::prepareLDrawImport(
            {identity, path.filename().string(), {{path.filename().string(), read(argv[2])}}}));
    }();
    glm::vec3 minimum(std::numeric_limits<float>::max()), maximum(-std::numeric_limits<float>::max());
    for (const auto& entry : prepared.entries) {
        if (!entry.path.ends_with("/mesh.bin")) continue;
        const auto mesh = checked(asset::decodeCanonicalMesh(entry.bytes));
        for (std::size_t i = 0; i < mesh.positions.size(); i += 3) {
            const glm::vec3 point(mesh.positions[i], mesh.positions[i + 1], mesh.positions[i + 2]);
            minimum = glm::min(minimum, point);
            maximum = glm::max(maximum, point);
        }
    }
    const auto  size   = maximum - minimum;
    const float extent = std::max({size.x, size.y, size.z});
    if (!(extent > 0)) throw std::runtime_error("empty model bounds");
    auto                        eva     = checked(asset::buildEvaArchive(prepared.manifest, prepared.entries));
    auto                        archive = checked(asset::parseEvaArchive(eva));
    auto                        profile = checked(asset::assetCookProfileForTarget("windows-x86_64-vulkan"));
    auto                        cooked  = checked(asset::cookEvaToEvpack(archive, profile));
    auto                        pack = std::make_shared<const asset::Evpack>(checked(asset::parseEvpack(cooked.bytes)));
    asset::EvpackResourceReader reader(pack);
    auto*                       gfx = graphics::Graphics::create();
    if (!gfx) throw std::runtime_error("graphics provider absent");
    gfx->initHeadless(960, 720);
    asset_graphics::GraphicsMeshFactoryAdapter  meshes(*gfx);
    asset_graphics::GraphicsImageFactoryAdapter images(*gfx);
    const asset::EvpackCapabilities caps{"windows", "x86_64", gfx->getBackendName(), {"rgba8"}, {"spirv-1.6"},
                                         {"high"},  {}};
    auto                            prefab = checked(asset_graphics::EvpackStaticPrefab::load(reader, meshes, images,
                                                                                              prepared.manifest.entrypoints.at("default"), caps));
    const glm::vec3                 eye(2.8f, 2.f, 3.5f);
    const auto                      view       = glm::lookAtRH(eye, glm::vec3(0), glm::vec3(0, 1, 0));
    auto                            projection = glm::orthoRH_ZO(-1.7f, 1.7f, -1.275f, 1.275f, .1f, 20.f);
    projection[1][1] *= -1.f;
    gfx->setMesh3DViewProj(projection * view);
    gfx->setMesh3DView(view);
    gfx->setMesh3DCameraPos(eye);
    graphics::Lighting3DPack lighting{};
    lighting.ambient             = glm::vec4(.5f, .5f, .5f, 0);
    lighting.count               = 1;
    lighting.lights[0].posRadius = glm::vec4(glm::normalize(glm::vec3(1, 2, 1)), 0);
    lighting.lights[0].color     = glm::vec4(2.f, 2.f, 2.f, 0);
    gfx->setMesh3DLighting(lighting);
    const auto instance =
        glm::scale(glm::mat4(1), glm::vec3(2.f / extent)) * glm::translate(glm::mat4(1), -(minimum + maximum) * .5f);
    std::array<float, 16> transform{}, camera{};
    std::copy_n(glm::value_ptr(instance), 16, transform.begin());
    std::copy_n(glm::value_ptr(view), 16, camera.begin());
    auto* canvas = gfx->newCanvas(960, 720);
    if (!canvas) throw std::runtime_error("canvas allocation failed");
    gfx->begin3DFrameToCanvas(canvas);
    checked(prefab->draw(*gfx, transform, camera));
    gfx->end3DFrameToCanvas();
    const auto shaderBytes = read(argv[4]);
    if (shaderBytes.empty() || shaderBytes.size() % 4) throw std::runtime_error("invalid display SPIR-V");
    std::vector<std::uint32_t> shaderCode(shaderBytes.size() / 4);
    std::memcpy(shaderCode.data(), shaderBytes.data(), shaderBytes.size());
    auto* transfer = gfx->newShaderFromSpv({}, shaderCode);
    auto* display  = gfx->newCanvas(960, 720);
    if (!transfer || !display) throw std::runtime_error("display transfer allocation failed");
    gfx->setCanvas(display);
    gfx->drawTexturedRectShader(canvas->getTexture(), transfer, 0, 0, 960, 720, graphics::Color(1, 1, 1, 1));
    gfx->setCanvas();
    std::unique_ptr<image::ImageData> image(display->newImageData());
    if (!image) throw std::runtime_error("engine canvas readback failed");
    std::unique_ptr<filesystem::FileData> png(
        image->encode(medialoader::FormatHandler::ENCODED_PNG, "model.png", false));
    if (!png) throw std::runtime_error("PNG encoding failed");
    std::ofstream output(argv[3], std::ios::binary);
    output.write(static_cast<const char*>(png->getData()), std::streamsize(png->getSize()));
    output.close();
    if (!output) throw std::runtime_error("PNG write failed");
    const auto drawCount = prefab->drawCount();
    checked(prefab->release());
    std::cout << "STRUCTURED_MODEL_RENDER_OK parts=" << drawCount << " output=" << argv[3] << '\n';
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
