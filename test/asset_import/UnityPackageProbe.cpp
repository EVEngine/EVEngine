#include "asset/AssetCooker.h"
#include "asset/EvaArchive.h"
#include "asset/graphics/EvpackStaticPrefab.h"
#include "asset/import/UnityImporter.h"
#include "asset/import/UnitySource.h"
#include "asset/scene/EvpackSceneTemplateLoader.h"
#include "graphics/IImageResourceFactory.h"
#include "graphics/IMeshResourceFactory.h"

#include <fstream>
#include <iostream>
#include <iterator>

// Manual, local-only admission probe. Purchased assets are supplied by path, never fixtures.
int main(int argc, char** argv) {
    using namespace eve;
    using namespace eve::asset;
    using namespace eve::asset_import;
    if (argc != 2 && argc != 3) {
        std::cerr << "usage: unity_package_probe <unitypackage> [output-prefix]\n";
        return 2;
    }
    std::ifstream input(argv[1], std::ios::binary);
    if (!input) return 2;
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(input), {}};
    auto                            sources = readUnityPackage(bytes);
    if (!sources) {
        std::cerr << sources.error()->message() << '\n';
        return 1;
    }
    auto index = indexUnitySources(sources.value());
    if (!index) {
        std::cerr << index.error()->message() << '\n';
        return 1;
    }
    std::cout << "indexed sources: " << index.value().assets.size() << '\n';
    UnityProjectImportRequest request;
    request.package = {
        *PersistentId::parse("018f6f22-2490-7ad2-bf58-4f1dbca31040"),
        "unity.local-probe",
        "1.0.0",
        {{"provider", Value("local")}, {"license", Value(Value::Object{{"redistribution", Value("project-only")}})}}};
    request.files = std::move(sources).takeValue();
    auto prepared = prepareUnityProjectImport(request);
    if (!prepared) {
        std::cerr << prepared.error()->message() << '\n';
        return 1;
    }
    std::cout << "canonical assets: " << prepared.value().manifest.assets.size() << '\n';
    std::map<std::string, std::size_t> unsupported;
    for (const auto& finding : prepared.value().findings)
        if (finding.disposition == ImportDisposition::Unsupported) ++unsupported[finding.feature];
    for (const auto& [feature, count] : unsupported) std::cout << "unsupported " << feature << ": " << count << '\n';
    auto encoded = buildEvaArchive(prepared.value().manifest, prepared.value().entries);
    if (!encoded) {
        std::cerr << encoded.error()->message() << '\n';
        return 1;
    }
    auto archive = parseEvaArchive(encoded.value());
    if (!archive) {
        std::cerr << archive.error()->message() << '\n';
        return 1;
    }
    AssetCookProfile profile{
        {"win32", "x86_64", "vulkan", {"rgba8"}, "spirv-1.6", "high", {}}, CookPublication::LocalInspection, 16};
    auto cooked = cookEvaToEvpack(archive.value(), profile);
    if (!cooked) {
        std::cerr << cooked.error()->message() << '\n';
        return 1;
    }
    auto pack = parseEvpack(cooked.value().bytes);
    if (!pack) {
        std::cerr << pack.error()->message() << '\n';
        return 1;
    }
    auto                                   sharedPack = std::make_shared<const Evpack>(std::move(pack).takeValue());
    EvpackResourceReader                   reader(sharedPack);
    asset_scene::EvpackSceneTemplateLoader sceneLoader(reader);
    EvpackCapabilities                     caps{"win32", "x86_64", "vulkan", {"rgba8"}, {"spirv-1.6"}, {"high"}, {}};
    std::size_t                            bound = 0;
    class MeshFactory                      final : public graphics::IMeshResourceFactory {
    public:
        int                     token = 0, uploads = 0, releases = 0;
        Result<graphics::Mesh*> uploadMesh(const float*, const float*, const float*, int, const std::uint32_t*,
                                                                int) override {
            ++uploads;
            return Result<graphics::Mesh*>::success(reinterpret_cast<graphics::Mesh*>(&token));
        }
        Result<void> releaseMesh(graphics::Mesh*) override {
            ++releases;
            return Result<void>::success();
        }
    } meshes;
    class ImageFactory final : public graphics::IImageResourceFactory {
    public:
        int                        token = 0, uploads = 0, releases = 0;
        Result<graphics::Texture*> uploadRgba8(std::uint32_t, std::uint32_t, const std::uint8_t*, bool) override {
            ++uploads;
            return Result<graphics::Texture*>::success(reinterpret_cast<graphics::Texture*>(&token));
        }
        Result<void> releaseImage(graphics::Texture*) override {
            ++releases;
            return Result<void>::success();
        }
    } images;
    for (const auto& asset : prepared.value().manifest.assets) {
        if (asset.type != "eve.scene-template") continue;
        auto scene = sceneLoader.load(asset.asset, caps);
        if (!scene) {
            std::cerr << scene.error()->message() << '\n';
            return 1;
        }
        bound += scene.value().renderers.size();
        auto renderer = asset_graphics::EvpackStaticPrefab::load(reader, meshes, images, asset.asset, caps);
        if (!renderer) {
            std::cerr << renderer.error()->message() << '\n';
            return 1;
        }
    }
    std::cout << "runtime renderer bindings: " << bound << '\n';
    std::cout << "recording-backend mesh uploads/releases: " << meshes.uploads << '/' << meshes.releases
              << "; image uploads/releases: " << images.uploads << '/' << images.releases << '\n';
    if (meshes.uploads != meshes.releases || images.uploads != images.releases) return 1;
    std::cout << "eva bytes: " << encoded.value().size() << "; evpack chunks: " << sharedPack->chunks().size() << '\n';
    if (argc == 3) {
        auto write = [&](const char* suffix, const auto& data) {
            std::ofstream output(std::string(argv[2]) + suffix, std::ios::binary);
            output.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
            output.close();
            return bool(output);
        };
        if (!write(".eva", encoded.value()) || !write(".evpack", cooked.value().bytes)) return 2;
    }
    return 0;
}
