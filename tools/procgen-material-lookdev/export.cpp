#include <filesystem>
#include <fstream>
#include "image/ImageData.h"
#include "procgen/texture/EverydayMaterials.h"
int main(int argc, char** argv) {
    const std::string folder = argc > 1 ? argv[1] : "/tmp/material-lookdev/assets";
    std::filesystem::create_directories(folder);
    const char* names[] = {"wood",  "metal",    "rubber",    "cloth",  "plastic", "steel", "glass",
                           "paper", "aluminum", "cast_iron", "cement", "paint",   "marble"};
    for (int i = 0; i < 13; ++i) {
        eve::procgen::Params p;
        p.setSize(1024, 1024);
        p.setSeed(42);
        auto result = eve::procgen::EverydayMaterials::generate(static_cast<eve::procgen::EverydayMaterial>(i), p);
        if (!result.ok()) return 1;
        auto                   maps     = std::move(result).takeValue();
        eve::image::ImageData* images[] = {maps->albedo,   maps->normal, maps->roughness,
                                           maps->metallic, maps->height, maps->ao};
        const char*            roles[]  = {"albedo", "normal", "roughness", "metallic", "height", "ao"};
        for (int j = 0; j < 6; ++j) {
            std::ofstream f(folder + "/" + names[i] + "_" + roles[j] + ".ppm", std::ios::binary);
            f << "P6\n1024 1024\n255\n";
            auto* pixels = static_cast<const char*>(images[j]->getData());
            for (int k = 0; k < 1024 * 1024; ++k) f.write(pixels + k * 4, 3);
        }
    }
}
