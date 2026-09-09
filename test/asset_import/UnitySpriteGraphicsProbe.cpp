#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include "asset/SpriteAnimation.h"
#include "asset/graphics/EvpackImageLoader.h"
#include "filesystem/FileData.h"
#include "graphics/Graphics.h"
#include "graphics/Texture.h"
#include "image/ImageData.h"

int main(int argc, char** argv) try {
    using namespace eve;
    if (argc != 4) {
        std::cerr << "usage: unity_sprite_graphics_probe <evpack> <frame-directory> <display.spv>\n";
        return 2;
    }
    std::ifstream file(argv[1], std::ios::binary);
    if (!file) return 2;
    std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(file), {}};
    auto                      parsed = asset::parseEvpack(bytes);
    if (!parsed) {
        std::cerr << parsed.error()->message();
        return 1;
    }
    auto                        pack = std::make_shared<const asset::Evpack>(std::move(parsed).takeValue());
    asset::EvpackResourceReader reader(pack);
    auto*                       gfx = graphics::Graphics::create();
    gfx->initHeadless(600, 450);
    asset::EvpackCapabilities caps{"windows", "x86_64", gfx->getBackendName(), {"rgba8"}, {"spirv-1.6"}, {"high"}, {}};
    std::optional<asset::SpriteAnimationClip> clip;
    for (const auto& c : pack->chunks())
        if (c.type == "eve.sprite-animation") {
            auto ref = AssetRef::fromId(c.assetId);
            if (!ref) return 1;
            auto loaded = asset::SpriteAnimationClip::load(reader, ref.value(), caps);
            if (!loaded) {
                std::cerr << loaded.error()->message();
                return 1;
            }
            if (clip) {
                std::cerr << "probe expects one authored clip";
                return 1;
            }
            clip = std::move(loaded).takeValue();
        }
    if (!clip || clip->duration() > 60) return 1;
    asset_graphics::GraphicsImageFactoryAdapter factory(*gfx);
    struct Leases {
        asset_graphics::GraphicsImageFactoryAdapter& factory;
        std::map<std::string, graphics::Texture*>    values;
        ~Leases() {
            for (auto& [id, texture] : values) {
                auto r = factory.releaseImage(texture);
                if (!r) std::cerr << r.error()->message();
            }
        }
    } textures{factory};
    asset_graphics::EvpackImageLoader images(reader, factory);
    std::ifstream                     shaderFile(argv[3], std::ios::binary | std::ios::ate);
    if (!shaderFile || shaderFile.tellg() <= 0 || shaderFile.tellg() % 4) return 2;
    std::vector<std::uint32_t> shaderCode(std::size_t(shaderFile.tellg()) / 4);
    shaderFile.seekg(0);
    shaderFile.read(reinterpret_cast<char*>(shaderCode.data()), std::streamsize(shaderCode.size() * 4));
    if (!shaderFile) return 2;
    auto* transfer = gfx->newShaderFromSpv({}, shaderCode);
    auto* canvas   = gfx->newCanvas(600, 450);
    auto* display  = gfx->newCanvas(600, 450);
    std::filesystem::create_directories(argv[2]);
    // Render 60 Hz time samples through the production sampler, including a loop wrap.
    const int count = int(std::ceil(clip->duration() * 60)) + 1;
    for (int i = 0; i < count; ++i) {
        auto frame = clip->sample(double(i) / 60);
        if (!frame) return 1;
        const auto& f   = frame.value();
        const auto  key = f.image.format();
        if (!textures.values.contains(key)) {
            auto image = images.load(f.image, caps);
            if (!image) {
                std::cerr << image.error()->message();
                return 1;
            }
            textures.values.emplace(key, image.value().texture);
        }
        auto* texture = textures.values.at(key);
        if (texture->getWidth() != f.imageSize[0] || texture->getHeight() != f.imageSize[1]) return 1;
        gfx->setTextureSampler(texture, f.nearest ? "nearest" : "linear", "none", 1, 0);
        gfx->setCanvas(canvas);
        gfx->clear(graphics::Color(.025f, .025f, .035f, 1), std::nullopt, std::nullopt);
        const float scale = 3;
        gfx->drawTexturedRectUV(texture, float(300 - f.pivot[0] * f.rect[2] * scale),
                                float(225 - f.pivot[1] * f.rect[3] * scale), float(f.rect[2] * scale),
                                float(f.rect[3] * scale), float(f.rect[0] / f.imageSize[0]),
                                float(f.rect[1] / f.imageSize[1]), float((f.rect[0] + f.rect[2]) / f.imageSize[0]),
                                float((f.rect[1] + f.rect[3]) / f.imageSize[1]), graphics::Color(1, 1, 1, 1));
        gfx->setCanvas();
        gfx->setCanvas(display);
        gfx->drawTexturedRectShader(canvas->getTexture(), transfer, 0, 0, 600, 450, graphics::Color(1, 1, 1, 1));
        gfx->setCanvas();
        std::unique_ptr<image::ImageData> pixels(display->newImageData());
        if (!pixels) return 1;
        std::unique_ptr<filesystem::FileData> png(
            pixels->encode(medialoader::FormatHandler::ENCODED_PNG, "frame.png", false));
        if (!png) return 1;
        char name[32];
        std::snprintf(name, sizeof(name), "frame-%04d.png", i);
        std::ofstream output(std::filesystem::path(argv[2]) / name, std::ios::binary);
        output.write(static_cast<const char*>(png->getData()), std::streamsize(png->getSize()));
        if (!output) return 2;
    }
    std::cout << "rendered " << count << " samples; " << clip->frameCount() << " keys, " << textures.values.size()
              << " textures, duration " << clip->duration() << "s\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what();
    return 1;
}
