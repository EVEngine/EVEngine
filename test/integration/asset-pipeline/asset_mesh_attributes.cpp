#include <bit>
#include <cmath>
#include <limits>
#include "asset/AssetCooker.h"
#include "asset/CanonicalMesh.h"
#ifdef EVE_TEST_MESH_UPLOAD
#include "asset/graphics/EvpackGraphicsLoader.h"
#include "asset/graphics/VegetationAsset.h"
#include "asset/import/UnityImporter.h"
#include "asset/import/UnitySourceInternal.h"
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/VegetationMotion.h"
#endif
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve;
namespace {
asset::CanonicalMeshData triangle() {
    asset::CanonicalMeshData m;
    m.positions                = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    m.indices                  = {0, 1, 2};
    m.texcoords[0]             = {0, 0, 1, 0, 0, 1};
    m.attributes["_UNITY_UV0"] = {4, {0, 0, 4194303, 1048063, 1, 0, 0, 1, 0, 1, 2, 3}};
    m.attributes["COLOR_0"]    = {4, {1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 1}};
    return m;
}
#ifdef EVE_TEST_MESH_UPLOAD
asset_import::UnityProjectImportRequest unityMesh() {
    asset_import::UnityProjectImportRequest r;
    r.package = {*PersistentId::parse("11111111-2222-4333-8444-555555555555"), "vegetation.test", "1.0.0", {}};
    std::string text =
        "%YAML 1.1\n--- !u!43 &4300000\nMesh:\n  serializedVersion: 11\n"
        "  m_MeshCompression: 0\n  m_BindPose: []\n  m_SubMeshes:\n"
        "  - serializedVersion: 2\n    firstByte: 0\n    indexCount: 3\n    topology: 0\n    baseVertex: 0\n"
        "  m_IndexFormat: 0\n  m_IndexBuffer: 000001000200\n  m_VertexData:\n    m_VertexCount: 3\n    m_Channels:\n";
    const unsigned dimensions[] = {3, 3, 4, 4, 4, 4, 0, 4, 0, 0, 0, 0, 0, 0};
    unsigned       offset       = 0;
    for (auto d : dimensions) {
        text += "    - stream: 0\n      offset: " + std::to_string(d ? offset : 0) +
                "\n      format: 0\n      dimension: " + std::to_string(d) + "\n";
        offset += d * 4;
    }
    std::string hex;
    const char* digits = "0123456789abcdef";
    for (unsigned v = 0; v < 3; ++v) {
        const float values[] = {float(v), 1,   2,   0,       1,       0,   1,   0,   0,   1, .25f, .5f, .75f,
                                1,        .2f, .3f, 4194303, 1048063, .4f, .6f, .8f, .9f, 2, 3,    4,   0};
        for (auto f : values) {
            auto bits = std::bit_cast<uint32_t>(f);
            for (unsigned j = 0; j < 4; ++j) {
                const auto b = uint8_t(bits >> (j * 8));
                hex += digits[b >> 4];
                hex += digits[b & 15];
            }
        }
    }
    text += "    m_DataSize: " + std::to_string(offset * 3) + "\n    _typelessdata: " + hex +
            "\n  m_StreamData:\n    size: 0\n";
    for (auto c : text) {
        if (c == '\n') {
            r.files["Assets/plant.asset"].push_back('\r');
            r.files["Assets/plant.asset"].push_back('\r');
        }
        r.files["Assets/plant.asset"].push_back(uint8_t(c));
    }
    return r;
}
#endif
}  // namespace
TEST_CASE("asset.mesh.attributeCodecPreservesPackedBitsAndRejectsMalformedDescriptors") {
    auto m       = triangle();
    auto encoded = asset::encodeCanonicalMesh(m);
    REQUIRE(encoded.ok());
    REQUIRE_EQ(encoded.value()[7], uint8_t(3));
    auto decoded = asset::decodeCanonicalMesh(encoded.value());
    REQUIRE(decoded.ok());
    REQUIRE_EQ(decoded.value().attributes.at("_UNITY_UV0").values, m.attributes.at("_UNITY_UV0").values);
    REQUIRE_EQ(decoded.value().attributes.at("COLOR_0").components, 4u);
    auto bad     = encoded.value();
    bad[32 + 68] = 1;  // Reserved descriptor word.
    REQUIRE(!asset::decodeCanonicalMesh(bad).ok());
    bad          = encoded.value();
    bad[32 + 64] = 5;
    REQUIRE(!asset::decodeCanonicalMesh(bad).ok());
    bad         = encoded.value();
    bad[32 + 8] = 'X';  // Nonzero bytes after semantic terminator.
    REQUIRE(!asset::decodeCanonicalMesh(bad).ok());
    bad = encoded.value();
    bad.pop_back();
    REQUIRE(!asset::decodeCanonicalMesh(bad).ok());
    REQUIRE(!asset::decodeCanonicalMesh(encoded.value(), {3, 3, 64}).ok());
    m.attributes.at("_UNITY_UV0").values[2] = std::numeric_limits<float>::infinity();
    REQUIRE(!asset::encodeCanonicalMesh(m).ok());
    m.attributes.clear();
    auto legacy = asset::encodeCanonicalMesh(m);
    REQUIRE(legacy.ok());
    REQUIRE_EQ(legacy.value()[7], uint8_t(2));
    REQUIRE(asset::decodeCanonicalMesh(legacy.value()).ok());
}
#ifdef EVE_TEST_MESH_UPLOAD
TEST_CASE("asset.mesh.unityV11AuthoringChannelsSurviveCookAndRuntimeLoad") {
    auto                           request = unityMesh();
    asset_import::UnitySourceAsset source;
    source.path   = "Assets/plant.asset";
    source.guid   = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    auto imported = asset_import::prepareUnityNativeMesh(request, source);
    REQUIRE(imported.ok());
    auto eva = asset::buildEvaArchive(imported.value().manifest, imported.value().entries);
    REQUIRE(eva.ok());
    auto archive = asset::parseEvaArchive(eva.value());
    REQUIRE(archive.ok());
    auto profile = asset::assetCookProfileForTarget("windows-x86_64-vulkan");
    REQUIRE(profile.ok());
    auto cooked = asset::cookEvaToEvpack(archive.value(), profile.value());
    REQUIRE(cooked.ok());
    auto pack = asset::parseEvpack(cooked.value().bytes);
    REQUIRE(pack.ok());
    asset::EvpackResourceReader reader(std::make_shared<const asset::Evpack>(std::move(pack).takeValue()));
    asset::EvpackCapabilities   caps{"windows", "x86_64", "vulkan", {"rgba8"}, {"spirv-1.6"}, {"high"}, {}};
    class Factory               final : public graphics::IMeshResourceFactory {
    public:
        int                token = 0, vertices = 0, indices = 0;
        float              z = 0, t = 0;
        uint32_t           secondIndex    = 0;
        bool               tangentSupport = true;
        int                releases       = 0;
        std::vector<float> tangentFrame, bitangentFrame;
        Result<void>       setMeshTangentFrame(graphics::Mesh* mesh, std::span<const float> t,
                                                             std::span<const float> b) override {
            if (!tangentSupport) return IMeshResourceFactory::setMeshTangentFrame(mesh, t, b);
            tangentFrame.assign(t.begin(), t.end());
            bitangentFrame.assign(b.begin(), b.end());
            return Result<void>::success();
        }
        Result<graphics::Mesh*> uploadMesh(const float* p, const float*, const float* uv, int n, const uint32_t* index,
                                                         int count) override {
            vertices    = n;
            indices     = count;
            z           = p[2];
            t           = uv[1];
            secondIndex = index[1];
            return Result<graphics::Mesh*>::success(reinterpret_cast<graphics::Mesh*>(&token));
        }
        Result<void> releaseMesh(graphics::Mesh*) override {
            ++releases;
            return Result<void>::success();
        }
    } factory;
    auto loaded = asset_graphics::EvpackGraphicsLoader(reader, factory)
                      .loadMesh(imported.value().manifest.assets.front().asset, caps);
    REQUIRE(loaded.ok());
    REQUIRE_EQ(factory.vertices, 3);
    REQUIRE_EQ(factory.indices, 3);
    REQUIRE_EQ(factory.z, -2.f);
    REQUIRE_EQ(factory.t, .7f);
    REQUIRE_EQ(factory.secondIndex, 2u);
    REQUIRE_EQ(factory.tangentFrame.size(), size_t(9));
    REQUIRE_EQ(factory.bitangentFrame.size(), size_t(9));
    REQUIRE_EQ(factory.tangentFrame[0], 1.f);
    REQUIRE_EQ(factory.bitangentFrame[2], 1.f);
    const auto& a = loaded.value().attributes;
    REQUIRE_EQ(a.at("_UNITY_UV0").components, 4u);
    REQUIRE_EQ(a.at("_UNITY_UV0").values[1], .3f);
    REQUIRE_EQ(a.at("_UNITY_UV0").values[2], 4194303.f);
    REQUIRE_EQ(a.at("_UNITY_UV0").values[3], 1048063.f);
    REQUIRE_EQ(a.at("_UNITY_UV3").values[2], 4.f);
    REQUIRE_EQ(a.at("COLOR_0").values[2], .75f);
    REQUIRE_EQ(a.at("TANGENT").values[3], -1.f);
    REQUIRE(factory.releaseMesh(loaded.value().mesh).ok());
    factory.tangentSupport = false;
    auto unsupported       = asset_graphics::EvpackGraphicsLoader(reader, factory)
                           .loadMesh(imported.value().manifest.assets.front().asset, caps);
    REQUIRE(!unsupported.ok());
    REQUIRE_EQ(factory.releases, 2);
    auto vegetation =
        asset_graphics::VegetationAsset::load(reader, imported.value().manifest.assets.front().asset, caps, {});
    REQUIRE(vegetation.ok());
    REQUIRE_EQ(vegetation.value()->vertexCount(), 3u);
    graphics::VegetationField  field;
    graphics::VegetationMotion motion;
    motion.bending = 0;
    motion.branch  = 0;
    motion.rolling = 0;
    motion.flutter = 0;
    auto rest      = vegetation.value()->evaluate(field, motion);
    REQUIRE(rest.ok());
    REQUIRE_EQ(rest.value().positions[0], 0.f);
    REQUIRE_EQ(rest.value().positions[1], 1.f);
    REQUIRE_EQ(rest.value().positions[2], -2.f);
    motion.bending = .4f;
    auto bent      = vegetation.value()->evaluate(field, motion);
    REQUIRE(bent.ok());
    REQUIRE(bent.value().positions != rest.value().positions);
    const float dx = bent.value().positions[0] - 2.f;
    const float dy = bent.value().positions[1] - 4.f;
    const float dz = bent.value().positions[2] + 3.f;
    REQUIRE(std::abs(dx * dx + dy * dy + dz * dz - 14.f) < 0.0001f);
    motion.bending = 0;
    auto restored  = vegetation.value()->evaluate(field, motion);
    REQUIRE(restored.ok());
    REQUIRE_EQ(restored.value().positions, rest.value().positions);
    auto* gfx = graphics::Graphics::create();
    gfx->initHeadless(16, 16);
    auto gpuMesh = vegetation.value()->createGpuFieldMesh(*gfx);
    REQUIRE(gpuMesh.ok());
    REQUIRE_EQ(gpuMesh.value()->vegetationDeformationFactors().size(), size_t(27));
    REQUIRE_EQ(gpuMesh.value()->vegetationDeformationFactors()[0], 2.f);
    REQUIRE_EQ(gpuMesh.value()->vegetationDeformationFactors()[1], 4.f);
    REQUIRE_EQ(gpuMesh.value()->vegetationDeformationFactors()[2], -3.f);
    REQUIRE(gfx->releaseMesh(gpuMesh.value()));
}
TEST_CASE("asset.mesh.vegetationAdmissionRejectsMissingMalformedAndInvalidStreams") {
    auto mesh = triangle();
    REQUIRE(!asset_graphics::VegetationAsset::fromCanonical(mesh).ok());
    mesh.normals                  = {0, 1, 0, 0, 1, 0, 0, 1, 0};
    mesh.attributes["_UNITY_UV1"] = {4, std::vector<float>(12, 0.f)};
    mesh.attributes["_UNITY_UV3"] = {4, std::vector<float>(12, 0.f)};
    auto admitted                 = asset_graphics::VegetationAsset::fromCanonical(mesh);
    REQUIRE(admitted.ok());
    mesh.attributes["_UNITY_UV0"].values[2] = .5f;
    REQUIRE(!asset_graphics::VegetationAsset::fromCanonical(mesh).ok());
    mesh.attributes["_UNITY_UV0"].values[2] = 4194303.f;
    mesh.normals[0]                         = std::numeric_limits<float>::infinity();
    REQUIRE(!asset_graphics::VegetationAsset::fromCanonical(mesh).ok());
    mesh = {};
    graphics::VegetationField field;
    auto                      geometry = admitted.value()->evaluate(field, {});
    REQUIRE(geometry.ok());
    REQUIRE_EQ(geometry.value().positions.size(), size_t(9));
}
#endif
