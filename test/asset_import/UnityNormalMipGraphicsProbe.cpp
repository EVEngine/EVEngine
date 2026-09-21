#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include "asset/graphics/EvpackImageLoader.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Texture.h"
#include "image/ImageData.h"

int main(int argc, char** argv) try {
    using namespace eve;
    if (argc != 3) {
        std::cerr << "usage: unity_normal_mip_graphics_probe <evpack> <asset-ref>\n";
        return 2;
    }
    std::ifstream input(argv[1], std::ios::binary);
    if (!input) return 2;
    std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input), {}};
    auto                 pack = asset::parseEvpack(bytes);
    if (!pack) {
        std::cerr << pack.error()->message();
        return 1;
    }
    auto ref = AssetRef::parse(argv[2]);
    if (!ref) {
        std::cerr << ref.error()->message();
        return 1;
    }
    asset::EvpackResourceReader reader(std::make_shared<const asset::Evpack>(std::move(pack).takeValue()));
    auto*                       gfx = graphics::Graphics::create();
    gfx->initHeadless(32, 32);
    asset::EvpackCapabilities caps{"windows", "x86_64", gfx->getBackendName(), {"rgba8"}, {"spirv-1.6"}, {"high"}, {}};
    auto                      payload = reader.read(ref.value(), "eve.image/3", caps, 64 * 1024 * 1024);
    if (!payload) {
        std::cerr << payload.error()->message();
        return 1;
    }
    const asset::RuntimeAssetChunk* bulk = nullptr;
    for (const auto& chunk : payload.value().chunks)
        if (chunk.kind == asset::EvpackChunkKind::Bulk) bulk = &chunk;
    if (!bulk || bulk->bytes.size() < 28) return 1;
    asset_graphics::GraphicsImageFactoryAdapter factory(*gfx);
    asset_graphics::EvpackImageLoader           loader(reader, factory);
    auto                                        uploaded = loader.load(ref.value(), caps);
    if (!uploaded) {
        std::cerr << uploaded.error()->message();
        return 1;
    }
    auto*                                     texture = uploaded.value().texture;
    auto*                                     canvas  = gfx->newCanvas(32, 32);
    uint32_t                                  w = uint32_t(texture->getWidth()), h = uint32_t(texture->getHeight());
    size_t                                    offset       = 28;
    float                                     maximumError = 0;
    bool                                      mismatch     = false;
    const std::array<std::array<float, 2>, 4> coordinates{{{.13f, .27f}, {.67f, .83f}, {.45f, .55f}, {.5f, .5f}}};
    for (int level = 0; level < texture->mipmapCount; ++level) {
        auto sampler   = graphics::TextureSampler::nearest();
        sampler.mipmap = graphics::MipmapMode::Nearest;
        sampler.minLod = sampler.maxLod = float(level);
        gfx->setTextureSampler(texture, sampler);
        for (const auto& uv : coordinates) {
            gfx->setCanvas(canvas);
            gfx->clear(graphics::Color(0, 0, 0, 1), std::nullopt, std::nullopt);
            gfx->drawTexturedRectUV(texture, 0, 0, 32, 32, uv[0], uv[1], uv[0], uv[1], graphics::Color(1, 1, 1, 1));
            gfx->setCanvas();
            std::unique_ptr<image::ImageData> pixels(canvas->newImageData());
            if (!pixels) {
                mismatch = true;
                break;
            }
            const auto                 actual = pixels->getPixel(16, 16);
            const std::array<float, 4> channels{actual.r, actual.g, actual.b, actual.a};
            const size_t               at = offset + (size_t(uint32_t(uv[1] * h)) * w + uint32_t(uv[0] * w)) * 4;
            if (at + 4 > bulk->bytes.size()) {
                mismatch = true;
                break;
            }
            for (size_t c = 0; c < channels.size(); ++c) {
                const float error = std::abs(channels[c] - bulk->bytes[at + c] / 255.f);
                maximumError      = std::max(maximumError, error);
                if (error > .01f) mismatch = true;
            }
        }
        offset += size_t(w) * h * 4;
        w = std::max(w / 2, 1u);
        h = std::max(h / 2, 1u);
    }
    const int levels   = texture->mipmapCount;
    auto      released = factory.releaseImage(texture);
    if (!released) {
        std::cerr << released.error()->message();
        return 1;
    }
    if (offset != bulk->bytes.size()) mismatch = true;
    std::cout << "backend=" << gfx->getBackendName() << " levels=" << levels
              << " samplePoints=" << levels * coordinates.size() << " maxChannelError=" << maximumError << '\n';
    return mismatch ? 1 : 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
