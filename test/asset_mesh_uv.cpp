#include <bit>
#include "asset/AssetCooker.h"
#include "asset/CanonicalMesh.h"
#include "asset/EvpackResourceReader.h"
#include "asset/import/AssetImporter.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
#ifdef EVE_TEST_MESH_UPLOAD
#include "asset/graphics/EvpackGraphicsLoader.h"
#endif
using namespace eve;
namespace {
asset_import::GltfImportRequest uvMesh() {
    asset_import::GltfImportRequest r;
    r.package    = {*PersistentId::parse("11111111-2222-4333-8444-555555555555"), "uv.test", "1.0.0", {}};
    r.sourceName = "uv.gltf";
    auto& b      = r.externalResources["data.bin"];
    b.resize(78);
    // Three distinct UV representations, including sparse channel numbers.
    for (size_t i = 0; i < 6; ++i) {
        auto bits = std::bit_cast<uint32_t>(float(i) / 2.f);
        for (size_t k = 0; k < 4; ++k) b[36 + i * 4 + k] = uint8_t(bits >> (k * 8));
        uint16_t value = (i % 2) ? 65535 : 0;
        b[60 + i * 2]  = uint8_t(value);
        b[61 + i * 2]  = uint8_t(value >> 8);
        b[72 + i]      = uint8_t(i * 51);
    }
    std::string json =
        R"({"asset":{"version":"2.0"},"buffers":[{"uri":"data.bin","byteLength":78}],"bufferViews":[{"buffer":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":24},{"buffer":0,"byteOffset":60,"byteLength":12},{"buffer":0,"byteOffset":72,"byteLength":6}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":1,"componentType":5126,"count":3,"type":"VEC2"},{"bufferView":2,"componentType":5123,"normalized":true,"count":3,"type":"VEC2"},{"bufferView":3,"componentType":5121,"normalized":true,"count":3,"type":"VEC2"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"TEXCOORD_0":1,"TEXCOORD_2":2,"TEXCOORD_7":3}}]}]})";
    r.documentBytes.assign(json.begin(), json.end());
    return r;
}
std::vector<uint8_t> meshBytes(const asset_import::PreparedAssetImport& imported) {
    for (const auto& e : imported.entries)
        if (e.path.ends_with("mesh.bin")) return e.bytes;
    return {};
}
}  // namespace
TEST_CASE("asset.mesh.multipleUvSetsSurviveImportAndCook") {
    auto imported = asset_import::prepareGltfImport(uvMesh());
    REQUIRE(imported.ok());
    REQUIRE_EQ(imported.value().manifest.assets.front().schemaVersion, SchemaVersion(2));
    auto decoded = asset::decodeCanonicalMesh(meshBytes(imported.value()));
    REQUIRE(decoded.ok());
    REQUIRE_EQ(decoded.value().texcoords.size(), size_t(3));
    REQUIRE_EQ(decoded.value().texcoords.at(0)[5], 2.5f);
    REQUIRE_EQ(decoded.value().texcoords.at(2)[1], 1.f);
    REQUIRE_EQ(decoded.value().texcoords.at(7)[5], 1.f);
    auto archiveBytes = asset::buildEvaArchive(imported.value().manifest, imported.value().entries);
    REQUIRE(archiveBytes.ok());
    auto archive = asset::parseEvaArchive(archiveBytes.value());
    REQUIRE(archive.ok());
    auto profile = asset::assetCookProfileForTarget("windows-x86_64-vulkan");
    REQUIRE(profile.ok());
    auto cooked = asset::cookEvaToEvpack(archive.value(), profile.value());
    REQUIRE(cooked.ok());
    auto pack = asset::parseEvpack(cooked.value().bytes);
    REQUIRE(pack.ok());
    asset::EvpackResourceReader reader(std::make_shared<const asset::Evpack>(std::move(pack).takeValue()));
    asset::EvpackCapabilities   caps{"windows", "x86_64", "vulkan", {"rgba8"}, {"spirv-1.6"}, {"high"}, {}};
    auto payload = reader.read(imported.value().manifest.entrypoints.at("default"), "eve.mesh/2", caps, 1024 * 1024);
    REQUIRE(payload.ok());
    bool checked = false;
    for (const auto& c : payload.value().chunks)
        if (c.kind == asset::EvpackChunkKind::Bulk) {
            auto actual = asset::decodeCanonicalMesh(c.bytes);
            REQUIRE(actual.ok());
            REQUIRE_EQ(actual.value().texcoords, decoded.value().texcoords);
            checked = true;
        }
    REQUIRE(checked);
}
TEST_CASE("asset.mesh.rejectsMalformedUvStreamsAndBudgets") {
    auto imported = asset_import::prepareGltfImport(uvMesh());
    REQUIRE(imported.ok());
    const auto original = meshBytes(imported.value());
    auto       bad      = original;
    bad[28]             = 0;  // Duplicate UV descriptor (0 instead of 2).
    REQUIRE(!asset::decodeCanonicalMesh(bad).ok());
    bad = original;
    bad.pop_back();
    REQUIRE(!asset::decodeCanonicalMesh(bad).ok());
    bad     = original;
    bad[20] = 255;
    REQUIRE(!asset::decodeCanonicalMesh(bad).ok());
    bad     = original;
    bad[48] = 0;
    bad[49] = 0;
    bad[50] = 128;
    bad[51] = 127;
    REQUIRE(!asset::decodeCanonicalMesh(bad).ok());
    asset::CanonicalMeshLimits limits;
    limits.maximumDecodedBytes = 32;
    REQUIRE(!asset::decodeCanonicalMesh(original, limits).ok());
    auto request                       = uvMesh();
    request.limits.maximumDecodedBytes = 32;
    REQUIRE(!asset_import::prepareGltfImport(request).ok());
}
#ifdef EVE_TEST_MESH_UPLOAD
TEST_CASE("asset.mesh.graphicsSelectsUvSetWithoutSilentRemap") {
    auto imported = asset_import::prepareGltfImport(uvMesh());
    REQUIRE(imported.ok());
    auto bytes = asset::buildEvaArchive(imported.value().manifest, imported.value().entries);
    REQUIRE(bytes.ok());
    auto archive = asset::parseEvaArchive(bytes.value());
    REQUIRE(archive.ok());
    auto profile = asset::assetCookProfileForTarget("windows-x86_64-vulkan");
    REQUIRE(profile.ok());
    auto cooked = asset::cookEvaToEvpack(archive.value(), profile.value());
    REQUIRE(cooked.ok());
    auto pack = asset::parseEvpack(cooked.value().bytes);
    REQUIRE(pack.ok());
    asset::EvpackResourceReader reader(std::make_shared<const asset::Evpack>(std::move(pack).takeValue()));
    asset::EvpackCapabilities   caps{"windows", "x86_64", "vulkan", {"rgba8"}, {"spirv-1.6"}, {"high"}, {}};
    class Factory final : public graphics::IMeshResourceFactory {
    public:
        int                                    calls = 0, token = 0, releases = 0;
        std::vector<float>                     uv;
        bool                                   multiUv = false;
        std::map<uint32_t, std::vector<float>> channels;
        Result<void> setMeshTexcoords(graphics::Mesh* mesh, uint32_t set, std::span<const float> values) override {
            if (!multiUv) return IMeshResourceFactory::setMeshTexcoords(mesh, set, values);
            channels[set] = {values.begin(), values.end()};
            return Result<void>::success();
        }
        Result<graphics::Mesh*> uploadMesh(const float*, const float*, const float* t, int n, const uint32_t*,
                                           int) override {
            ++calls;
            uv.assign(t, t + n * 2);
            return Result<graphics::Mesh*>::success(reinterpret_cast<graphics::Mesh*>(&token));
        }
        Result<void> releaseMesh(graphics::Mesh*) override {
            ++releases;
            return Result<void>::success();
        }
    } factory;
    asset_graphics::EvpackGraphicsLoader loader(reader, factory);
    const auto                           ref  = imported.value().manifest.entrypoints.at("default");
    auto                                 mesh = loader.loadMesh(ref, caps, {}, 2);
    REQUIRE(mesh.ok());
    REQUIRE_EQ(factory.uv, std::vector<float>({0, 1, 0, 1, 0, 1}));
    REQUIRE_EQ(mesh.value().texcoordSet, uint32_t(2));
    REQUIRE(factory.releaseMesh(mesh.value().mesh).ok());
    auto missing = loader.loadMesh(ref, caps, {}, 1);
    REQUIRE(!missing.ok());
    REQUIRE_EQ(factory.calls, 1);
    auto absent = loader.loadMesh(ref, caps, {}, 0, true);
    REQUIRE(!absent.ok());
    REQUIRE_EQ(factory.releases, 2);
    factory.multiUv = true;
    auto complete   = loader.loadMesh(ref, caps, {}, 0, true);
    REQUIRE(complete.ok());
    REQUIRE_EQ(factory.channels.size(), size_t(2));
    REQUIRE_EQ(factory.channels.at(2), std::vector<float>({0, 1, 0, 1, 0, 1}));
    REQUIRE(factory.releaseMesh(complete.value().mesh).ok());
}
#endif
TEST_CASE("asset.mesh.legacyBinaryDecodesToCanonicalUvSets") {
    auto request = uvMesh();
    auto doc     = Value::fromJson(std::string(request.documentBytes.begin(), request.documentBytes.end()));
    REQUIRE(doc.ok());
    auto& attrs = *doc.value()
                       .getIf<Value::Object>()
                       ->at("meshes")
                       .getIf<Value::Array>()
                       ->at(0)
                       .getIf<Value::Object>()
                       ->at("primitives")
                       .getIf<Value::Array>()
                       ->at(0)
                       .getIf<Value::Object>()
                       ->at("attributes")
                       .getIf<Value::Object>();
    attrs.erase("TEXCOORD_2");
    attrs.erase("TEXCOORD_7");
    auto json = doc.value().toJson();
    REQUIRE(json.ok());
    request.documentBytes.assign(json.value().begin(), json.value().end());
    auto imported = asset_import::prepareGltfImport(request);
    REQUIRE(imported.ok());
    auto bytes = meshBytes(imported.value());
    bytes[7]   = 1;
    bytes[16]  = 2;
    bytes[20]  = 0;
    bytes.erase(bytes.begin() + 24, bytes.begin() + 28);
    auto legacy = asset::decodeCanonicalMesh(bytes);
    REQUIRE(legacy.ok());
    REQUIRE_EQ(legacy.value().texcoords.size(), size_t(1));
    REQUIRE_EQ(legacy.value().texcoords.at(0)[5], 2.5f);
    bytes[7] = 3;
    REQUIRE(!asset::decodeCanonicalMesh(bytes).ok());
}
TEST_CASE("asset.mesh.rejectsInvalidSourceUvAccessors") {
    for (int kind = 0; kind < 3; ++kind) {
        auto request  = uvMesh();
        auto document = Value::fromJson(std::string(request.documentBytes.begin(), request.documentBytes.end()));
        REQUIRE(document.ok());
        auto& root = *document.value().getIf<Value::Object>();
        auto& uv   = *root.at("accessors").getIf<Value::Array>()->at(2).getIf<Value::Object>();
        if (kind == 0) uv["normalized"] = Value(false);
        if (kind == 1) uv["count"] = Value(int64_t(2));
        if (kind == 2) {
            auto& attrs                  = *root.at("meshes")
                                                .getIf<Value::Array>()
                                                ->at(0)
                                                .getIf<Value::Object>()
                                                ->at("primitives")
                                                .getIf<Value::Array>()
                                                ->at(0)
                                                .getIf<Value::Object>()
                                                ->at("attributes")
                                                .getIf<Value::Object>();
            attrs["TEXCOORD_4294967296"] = Value(int64_t(2));
        }
        auto json = document.value().toJson();
        REQUIRE(json.ok());
        request.documentBytes.assign(json.value().begin(), json.value().end());
        REQUIRE(!asset_import::prepareGltfImport(request).ok());
    }
}
