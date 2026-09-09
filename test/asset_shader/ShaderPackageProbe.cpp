#include "asset/AssetCooker.h"
#include "asset/EvpackResourceReader.h"
#include "asset/ShaderAsset.h"
#include "asset/import/ShaderImporter.h"

#include <fstream>
#include <iostream>
#include <iterator>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    std::ifstream stream(argv[1], std::ios::binary);
    if (!stream) return 3;
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(stream)), {});
    auto                      id = eve::PersistentId::parse("018f6f22-2490-7ad2-bf58-4f1dbca31040");
    if (!id) return 4;
    auto imported = eve::asset_import::prepareShaderImport(
        {*id, "shader.probe", "1.0.0", {}},
        std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    if (!imported) return 4;
    auto archive = eve::asset::buildEvaArchive(imported.value().manifest, imported.value().entries);
    if (!archive) return 4;
    auto source = eve::asset::parseEvaArchive(archive.value());
    if (!source) return 4;
    auto profile = eve::asset::assetCookProfileForTarget("windows-x86_64-vulkan");
    if (!profile) return 5;
    auto cooked = eve::asset::cookEvaToEvpack(source.value(), profile.value());
    if (!cooked) return 6;
    auto pack = eve::asset::parseEvpack(cooked.value().bytes);
    if (!pack) return 7;
    eve::asset::EvpackResourceReader reader(std::make_shared<const eve::asset::Evpack>(std::move(pack).takeValue()));
    const auto&                      entrypoints = source.value().manifest.entrypoints;
    if (!entrypoints.contains("default")) return 8;
    auto shader =
        eve::asset::loadShaderAsset(reader, entrypoints.at("default"),
                                    {"windows", "x86_64", "vulkan", {"bc", "rgba8"}, {"spirv-1.6"}, {"high"}, {}});
    if (!shader) return 9;
    std::cout << "shader-package-ok vertexWords=" << shader.value().vertex.size()
              << " fragmentWords=" << shader.value().fragment.size() << '\n';
}
