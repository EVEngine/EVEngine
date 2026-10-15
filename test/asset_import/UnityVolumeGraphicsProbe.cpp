#include "asset/graphics/EvpackImageLoader.h"
#include "graphics/Graphics.h"
#include "graphics/Texture.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>

int main(int argc, char** argv) try {
    using namespace eve;
    if (argc != 3) {
        std::cerr << "usage: eve_unity_volume_graphics_probe <evpack> <asset-ref>\n";
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
    gfx->initHeadless(16, 16);
    asset::EvpackCapabilities caps{"windows", "x86_64", gfx->getBackendName(), {"rgba8"}, {"spirv-1.6"}, {"high"}, {}};
    asset_graphics::GraphicsImageFactoryAdapter factory(*gfx);
    asset_graphics::EvpackVolumeTextureLoader   loader(reader, factory);
    auto                                        loaded = loader.load(ref.value(), caps);
    if (!loaded) {
        std::cerr << loaded.error()->message();
        return 1;
    }
    auto*      texture  = loaded.value().texture;
    const bool expected = texture && texture->getWidth() == 16 && texture->getHeight() == 16 && texture->depth == 16 &&
                          texture->getSampler().repeatU && texture->getSampler().repeatV &&
                          texture->getSampler().repeatW && texture->getSampler().min == graphics::FilterMode::Linear &&
                          texture->getSampler().mag == graphics::FilterMode::Linear;
    if (texture) {
        auto released = factory.releaseImage(texture);
        if (!released) {
            std::cerr << released.error()->message();
            return 1;
        }
    }
    std::cout << "backend=" << gfx->getBackendName() << " dimensions=16x16x16 repeatUVW=true uploaded=" << expected
              << '\n';
    return expected ? 0 : 1;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
