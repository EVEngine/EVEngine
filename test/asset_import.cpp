#include "asset/AssetCooker.h"
#include "asset/RuntimeDefinition.h"
#include "asset/EvpackResourceReader.h"
#include "asset_graphics/EvpackGraphicsLoader.h"
#include "asset_graphics/EvpackImageLoader.h"
#include "asset_import/AssetImporter.h"
#include "graphics/IMeshResourceFactory.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <fstream>
#include <iterator>

using namespace eve;
using namespace eve::asset;
using namespace eve::asset_import;

namespace {

PersistentId importId() {
    return *PersistentId::parse("018f6f22-2490-7ad2-bf58-4f1dbca31040");
}

ImportPackageIdentity identity(std::string name) {
    return {importId(), std::move(name), "1.0.0",
            {{"provider", Value("local")},
             {"license", Value(Value::Object{{"redistribution", Value("project-only")}})}}};
}

std::vector<std::uint8_t> fileBytes(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

const EvaArchiveEntry* entry(const PreparedAssetImport& prepared, std::string_view path) {
    const auto found = std::find_if(prepared.entries.begin(), prepared.entries.end(),
                                    [&](const auto& value) { return value.path == path; });
    return found == prepared.entries.end() ? nullptr : &*found;
}

AssetCookProfile linuxProfile() {
    return {{"linux", "x86_64", "vulkan", {"rgba8"}, "spirv-1.6", "high", {}},
            CookPublication::LocalInspection, 16};
}

class VerticalMeshFactory final : public eve::graphics::IMeshResourceFactory {
public:
    Result<eve::graphics::Mesh*> uploadMesh(const float*, const float*, const float*,
                                            int vertices, const std::uint32_t*,
                                            int indices) override {
        vertexCount = vertices;
        indexCount = indices;
        return Result<eve::graphics::Mesh*>::success(
            reinterpret_cast<eve::graphics::Mesh*>(&token));
    }
    Result<void> releaseMesh(eve::graphics::Mesh*) override { return Result<void>::success(); }
    int vertexCount = 0;
    int indexCount = 0;

private:
    std::uint64_t token = 0;
};

class VerticalImageFactory final : public eve::graphics::IImageResourceFactory {
public:
    Result<eve::graphics::Texture*> uploadRgba8(std::uint32_t widthValue,
                                                std::uint32_t heightValue,
                                                const std::uint8_t* pixels,
                                                bool srgbValue) override {
        width = widthValue;
        height = heightValue;
        srgb = srgbValue;
        firstPixel.assign(pixels, pixels + 4);
        return Result<eve::graphics::Texture*>::success(
            reinterpret_cast<eve::graphics::Texture*>(&token));
    }
    Result<void> releaseImage(eve::graphics::Texture*) override {
        return Result<void>::success();
    }
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bool srgb = true;
    std::vector<std::uint8_t> firstPixel;

private:
    std::uint64_t token = 0;
};

}  // namespace

TEST_CASE("asset.import.pngTraversesEvaCookAndEvpack") {
    // Complete 1x1 RGBA PNG. The source is retained losslessly in canonical eve.image.
    const std::vector<std::uint8_t> png = {
        0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,0x49,0x48,0x44,0x52,
        0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x08,0x06,0x00,0x00,0x00,0x1f,0x15,0xc4,
        0x89,0x00,0x00,0x00,0x0d,0x49,0x44,0x41,0x54,0x08,0xd7,0x63,0xf8,0xcf,0xc0,0xf0,
        0x1f,0x00,0x05,0x00,0x01,0xff,0x72,0x9c,0x52,0x67,0x00,0x00,0x00,0x00,0x49,0x45,
        0x4e,0x44,0xae,0x42,0x60,0x82};
    auto corruptPng = png;
    corruptPng[45] ^= 1;
    auto corrupt = prepareImageImport({identity("image.corrupt"), "corrupt.png", corruptPng,
                                       ImageColorSpace::Srgb, "color", {}});
    REQUIRE(!corrupt.ok());
    CHECK_EQ(corrupt.error()->code(), DiagnosticCode::HashMismatch);
    auto prepared = prepareImageImport({identity("image.test"), "pixel.png", png,
                                        ImageColorSpace::Srgb, "color", {}});
    REQUIRE(prepared.ok());
    REQUIRE_EQ(prepared.value().manifest.assets.size(), std::size_t(1));
    REQUIRE(entry(prepared.value(), "reports/import.json") != nullptr);
    CHECK_EQ(prepared.value().sourceMappings.size(), std::size_t(1));
    CHECK(prepared.value().manifest.provenance.contains("importKey"));
    auto evaBytes = buildEvaArchive(prepared.value().manifest, prepared.value().entries);
    REQUIRE(evaBytes.ok());
    auto eva = parseEvaArchive(evaBytes.value());
    REQUIRE(eva.ok());
    auto cooked = cookEvaToEvpack(eva.value(), linuxProfile());
    REQUIRE(cooked.ok());
    auto runtime = parseEvpack(cooked.value().bytes);
    REQUIRE(runtime.ok());
    CHECK_EQ(runtime.value().chunks().size(), std::size_t(2));
    auto runtimeDefinition = runtime.value().decodeChunk(0, 4096);
    auto runtimePixels = runtime.value().decodeChunk(1, 4096);
    REQUIRE(runtimeDefinition.ok());
    REQUIRE(runtimePixels.ok());
    auto definition = decodeRuntimeDefinition(runtimeDefinition.value());
    REQUIRE(definition.ok());
    const auto* definitionObject = definition.value().getIf<Value::Object>();
    REQUIRE(definitionObject != nullptr);
    CHECK_EQ(definitionObject->at("encoding"), Value("rgba8"));
    REQUIRE_EQ(runtimePixels.value().size(), std::size_t(28));
    CHECK_EQ(std::string(reinterpret_cast<const char*>(runtimePixels.value().data()), 5),
             std::string("EVIMG"));

    auto admitted = std::make_shared<const Evpack>(std::move(runtime).takeValue());
    EvpackResourceReader reader(admitted);
    VerticalImageFactory imageFactory;
    eve::asset_graphics::EvpackImageLoader imageLoader(reader, imageFactory);
    auto uploaded = imageLoader.load(
        prepared.value().manifest.assets.front().asset,
        {"linux", "x86_64", "vulkan", {"rgba8"}, {"spirv-1.6"}, {"high"}, {}});
    REQUIRE(uploaded.ok());
    CHECK(uploaded.value().texture != nullptr);
    CHECK_EQ(imageFactory.width, std::uint32_t(1));
    CHECK_EQ(imageFactory.height, std::uint32_t(1));
    CHECK(!imageFactory.srgb);
    CHECK_EQ(imageFactory.firstPixel.size(), std::size_t(4));

    auto unsupportedProfile = linuxProfile();
    unsupportedProfile.variant.textureFamilies = {"bc"};
    auto unsupported = cookEvaToEvpack(eva.value(), unsupportedProfile);
    REQUIRE(!unsupported.ok());
    CHECK_EQ(unsupported.error()->code(), DiagnosticCode::Unsupported);
}

TEST_CASE("asset.import.realGlbDecodesCanonicalMeshAndTraversesRuntimePack") {
    const std::string path = "test/assets/housegen/kenney-modular-buildings/building-block.glb";
    auto bytes = fileBytes(path);
    REQUIRE(!bytes.empty());
    GltfImportRequest request{identity("gltf.test"), "building-block.glb", std::move(bytes), {}, {}};
    auto prepared = prepareGltfImport(request);
    REQUIRE(prepared.ok());
    REQUIRE(!prepared.value().manifest.assets.empty());
    REQUIRE(entry(prepared.value(), "reports/import.json") != nullptr);
    CHECK_EQ(prepared.value().sourceMappings.size(), prepared.value().manifest.assets.size());
    CHECK_EQ(prepared.value().manifest.assets.front().type, std::string("eve.mesh"));
    auto evaBytes = buildEvaArchive(prepared.value().manifest, prepared.value().entries);
    REQUIRE(evaBytes.ok());
    auto eva = parseEvaArchive(evaBytes.value());
    REQUIRE(eva.ok());
    auto cooked = cookEvaToEvpack(eva.value(), linuxProfile());
    REQUIRE(cooked.ok());
    auto runtime = parseEvpack(cooked.value().bytes);
    REQUIRE(runtime.ok());
    CHECK(runtime.value().chunks().size() >= prepared.value().manifest.assets.size() * 2);
    auto meshBlob = runtime.value().decodeChunk(1, runtime.value().chunks()[1].decodedSize);
    REQUIRE(meshBlob.ok());
    REQUIRE(meshBlob.value().size() >= 8);
    CHECK_EQ(std::string(reinterpret_cast<const char*>(meshBlob.value().data()), 6), std::string("EVMESH"));

    const auto entrypoint = prepared.value().manifest.entrypoints.find("default");
    REQUIRE(entrypoint != prepared.value().manifest.entrypoints.end());
    auto admitted = std::make_shared<const Evpack>(std::move(runtime).takeValue());
    EvpackResourceReader reader(admitted);
    VerticalMeshFactory factory;
    eve::asset_graphics::EvpackGraphicsLoader loader(reader, factory);
    auto uploaded = loader.loadMesh(
        entrypoint->second,
        {"linux", "x86_64", "vulkan", {"rgba8"}, {"spirv-1.6"}, {"high"}, {}});
    REQUIRE(uploaded.ok());
    CHECK(uploaded.value().mesh != nullptr);
    CHECK(factory.vertexCount > 0);
    CHECK(factory.indexCount > 0);
}

TEST_CASE("asset.import.gltfRejectsNetworkAndTraversalUris") {
    const std::string json = R"json({"asset":{"version":"2.0"},"buffers":[
      {"uri":"../outside.bin","byteLength":4}],"bufferViews":[],"accessors":[],"meshes":[]})json";
    GltfImportRequest request{identity("gltf.unsafe"), "unsafe.gltf",
                              {json.begin(), json.end()}, {}, {}};
    auto rejected = prepareGltfImport(request);
    REQUIRE(!rejected.ok());
    CHECK_EQ(rejected.error()->code(), DiagnosticCode::Unsupported);
}

TEST_CASE("asset.import.tamperedStructuredReportIsRejectedBeforePublication") {
    const std::vector<std::uint8_t> png = {
        0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,0x49,0x48,0x44,0x52,
        0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x08,0x06,0x00,0x00,0x00,0x1f,0x15,0xc4,
        0x89,0x00,0x00,0x00,0x0d,0x49,0x44,0x41,0x54,0x08,0xd7,0x63,0xf8,0xcf,0xc0,0xf0,
        0x1f,0x00,0x05,0x00,0x01,0xff,0x72,0x9c,0x52,0x67,0x00,0x00,0x00,0x00,0x49,0x45,
        0x4e,0x44,0xae,0x42,0x60,0x82};
    auto prepared = prepareImageImport({identity("image.report-security"), "pixel.png", png,
                                        ImageColorSpace::Srgb, "color", {}});
    REQUIRE(prepared.ok());
    auto report = std::find_if(prepared.value().entries.begin(), prepared.value().entries.end(),
                               [](const auto& value) { return value.path == "reports/import.json"; });
    REQUIRE(report != prepared.value().entries.end());
    std::string reportText(report->bytes.begin(), report->bytes.end());
    const auto source = reportText.find("\"sourceEngine\":\"image\"");
    REQUIRE(source != std::string::npos);
    reportText.replace(source, std::string("\"sourceEngine\":\"image\"").size(),
                       "\"sourceEngine\":\"evil!\"");
    report->bytes.assign(reportText.begin(), reportText.end());
    auto rejected = buildEvaArchive(prepared.value().manifest, prepared.value().entries);
    REQUIRE(!rejected.ok());
    CHECK_EQ(rejected.error()->code(), DiagnosticCode::HashMismatch);
}
