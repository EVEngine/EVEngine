#include <cstring>
#include <filesystem>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <iterator>
#include "asset/AssetCooker.h"
#include "asset/EvpackResourceReader.h"
#include "asset/import/AssetImporter.h"
#include "asset/stylize/EvpackMeshVfx.h"
#include "asset/stylize/MeshVfxPackage.h"
#include "common/CrashHandler.h"
#include "filesystem/FileData.h"
#include "graphics/Graphics.h"
#include "graphics/Shader.h"
#include "image/ImageData.h"
#include "stylize/MeshVfxAsset.h"

namespace {
template <class T>
T take(eve::Result<T> result) {
    if (!result) throw std::runtime_error(result.error()->message());
    return std::move(result).takeValue();
}
void check(eve::Result<void> result) {
    if (!result) throw std::runtime_error(result.error()->message());
}
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
std::vector<std::uint8_t> read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    require(bool(file), "Cannot read input");
    return {std::istreambuf_iterator<char>(file), {}};
}
void write(const std::filesystem::path& path, const void* data, std::size_t size) {
    std::ofstream file(path, std::ios::binary);
    file.write(static_cast<const char*>(data), std::streamsize(size));
    file.close();
    require(bool(file), "Cannot write output");
}
}  // namespace
int main(int argc, char** argv) try {
    eve::installCrashHandler();
    if (argc != 4) {
        std::cerr << "usage: shader_effect_probe shader.json output-directory display.frag.spv\n";
        return 2;
    }
    const std::filesystem::path directory(argv[2]);
    std::filesystem::create_directories(directory / "author");
    std::filesystem::create_directories(directory / "consumer");
    auto bytes  = read(argv[1]);
    auto shader = take(eve::Value::fromJson(std::string_view(reinterpret_cast<char*>(bytes.data()), bytes.size())));
    auto id     = eve::PersistentId::parse("018f6f22-2490-7ad2-bf58-4f1dbca31040");
    require(bool(id), "id");
    auto                            shaderRef = take(eve::AssetRef::fromId(id->child("shader:default")));
    eve::stylize::MeshVfxAsset      effect;
    eve::stylize::MeshVfxLayerAsset layer;
    layer.style                     = shaderRef.format();
    layer.playback                  = {0.f, 2.f, 0.f, true};
    layer.floatCurves["phase"].keys = {{0.f, 0.f}, {1.f, 2.f}};
    effect.layers.push_back(layer);
    effect.events.push_back({0.25f, "pulse"});
    require(!eve::asset_stylize::prepareMeshVfxPackage({*id, "shader.effect.demo", "1.0.0", {}}, effect, {}),
            "Missing shader accepted");
    auto invalidEffect                                 = effect;
    invalidEffect.layers[0].floatParameters["missing"] = 1.f;
    require(!eve::asset_stylize::prepareMeshVfxPackage({*id, "shader.effect.demo", "1.0.0", {}}, invalidEffect,
                                                       {{shaderRef, shader}}),
            "Unknown effect parameter accepted");
    auto prepared    = take(eve::asset_stylize::prepareMeshVfxPackage({*id, "shader.effect.demo", "1.0.0", {}}, effect,
                                                                      {{shaderRef, shader}}));
    auto sourceBytes = take(eve::asset::buildEvaArchive(prepared.manifest, prepared.entries));
    write(directory / "author/effect.eva", sourceBytes.data(), sourceBytes.size());
    auto       source    = take(eve::asset::parseEvaArchive(read(directory / "author/effect.eva")));
    const auto effectRef = source.manifest.entrypoints.at("default");
    auto       cooked =
        take(eve::asset::cookEvaToEvpack(source, take(eve::asset::assetCookProfileForTarget("windows-x86_64-vulkan"))));
    write(directory / "consumer/effect.evpack", cooked.bytes.data(), cooked.bytes.size());
    auto pack = std::make_shared<const eve::asset::Evpack>(
        take(eve::asset::parseEvpack(read(directory / "consumer/effect.evpack"))));
    eve::asset::EvpackResourceReader         reader(pack);
    std::unique_ptr<eve::graphics::Graphics> gfx(eve::graphics::Graphics::create());
    gfx->initHeadless(960, 640);
    eve::asset::EvpackCapabilities caps{"windows", "x86_64", "vulkan", {"rgba8"}, {"spirv-1.6"}, {"high"}, {}};
    auto                           first = take(eve::asset_stylize::EvpackMeshVfx::load(reader, effectRef, caps, *gfx));
    auto  second                         = take(eve::asset_stylize::EvpackMeshVfx::load(reader, effectRef, caps, *gfx));
    auto* mesh                           = gfx->newMeshSphere(64, 32);
    auto* canvas                         = gfx->newCanvas(960, 640);
    auto* display                        = gfx->newCanvas(960, 640);
    auto  transferBytes                  = read(argv[3]);
    require(transferBytes.size() % 4 == 0, "SPIR-V alignment");
    std::vector<std::uint32_t> transferCode(transferBytes.size() / 4);
    std::memcpy(transferCode.data(), transferBytes.data(), transferBytes.size());
    auto*           transfer = gfx->newShaderFromSpv({}, transferCode);
    const glm::vec3 eye(0, 1.6f, 6.f);
    auto            view       = glm::lookAtRH(eye, glm::vec3(0), glm::vec3(0, 1, 0));
    auto            projection = glm::orthoRH_ZO(-3.f, 3.f, -2.f, 2.f, 0.1f, 30.f);
    projection[1][1] *= -1.f;
    gfx->setMesh3DViewProj(projection * view);
    gfx->setMesh3DView(view);
    gfx->setMesh3DCameraPos(eye);
    gfx->setMesh3DClip(0.1f, 30.f);
    check(first->advance(0.f));
    check(second->advance(0.f));
    check(second->setFloat(0, "strength", 0.35f));
    std::vector<std::uint8_t> rightPixels;
    auto                      render = [&](const char* name) {
        gfx->begin3DFrameToCanvas(canvas);
        gfx->setMesh3DSurface(eve::graphics::SurfaceMode::Transparent, eve::graphics::BlendMode::Additive, false, true,
                                                   0.f);
        for (int i = 0; i < 2; ++i) {
            auto                  matrix = glm::translate(glm::mat4(1), glm::vec3(i ? 1.3f : -1.3f, 0, 0));
            std::array<float, 16> transform;
            std::copy_n(glm::value_ptr(matrix), 16, transform.begin());
            check((i ? second : first)
                                           ->draw(*mesh, transform, {},
                             i ? std::array<float, 4>{1.f, 0.35f, 1.f, 1.f} : std::array<float, 4>{1, 1, 1, 1}));
        }
        gfx->end3DFrameToCanvas();
        gfx->setCanvas(display);
        gfx->drawTexturedRectShader(canvas->getTexture(), transfer, 0, 0, 960, 640, eve::graphics::Color(1, 1, 1, 1));
        gfx->setCanvas();
        std::unique_ptr<eve::image::ImageData> image(display->newImageData());
        require(bool(image), "Engine readback failed");
        const auto  pixelSize = image->getPixelSize();
        const auto* pixels    = static_cast<const std::uint8_t*>(image->getData());
        rightPixels.clear();
        for (int y = 0; y < 640; ++y) {
            const auto* row = pixels + (std::size_t(y) * 960 + 480) * pixelSize;
            rightPixels.insert(rightPixels.end(), row, row + 480 * pixelSize);
        }
        std::unique_ptr<eve::filesystem::FileData> png(
            image->encode(medialoader::FormatHandler::ENCODED_PNG, "frame.png", false));
        require(bool(png), "PNG encode failed");
        const auto*               data = static_cast<const std::uint8_t*>(png->getData());
        std::vector<std::uint8_t> result(data, data + png->getSize());
        write(directory / name, data, png->getSize());
        return result;
    };
    auto       initial           = render("initial.png");
    const auto independentPixels = rightPixels;
    require(!first->advance(-1.f), "Negative time accepted");
    require(!first->setFloat(0, "missing", 1.f), "Unknown parameter accepted");
    require(!first->reload(reader, shaderRef, caps), "Invalid reload accepted");
    require(render("failed-reload.png") == initial, "Failed reload changed live effect");
    check(first->advance(0.7f));
    require(first->drainEvents() == std::vector<std::string>{"pulse"}, "Timeline event mismatch");
    auto animated = render("animated.png");
    require(rightPixels == independentPixels, "Advancing first instance changed second instance");
    require(animated != initial, "Curve did not change rendered frame");
    check(first->reload(reader, effectRef, caps));
    check(first->advance(0.f));
    require(render("reloaded.png") == initial, "Successful reload did not reset defaults");
    check(first->stop(0.f));
    require(render("stopped.png") != initial, "Stop did not affect rendering");
    first.reset();
    second.reset();
    require(gfx->releaseShader(transfer), "Transfer shader release failed");
    delete transfer;
    std::cout << "shader-effect-package-ok: native archive, consumer pack, Vulkan render, curves, events, independent "
                 "instances, failed/successful reload, stop\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
