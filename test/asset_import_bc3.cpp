#include "asset/SourceBc3.h"
#include "asset/CanonicalImageCook.h"
#include "asset/import/UnityImporter.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve;
TEST_CASE("asset.import.unityNativeTextureValidatesMipChainAndPreservesSource") {
    asset_import::UnityProjectImportRequest request;
    request.package = {*PersistentId::parse("11111111-2222-4333-8444-555555555555"), "bc3.test", "1.0.0", {}};
    const std::string source =
        "--- !u!28 &2800000\nTexture2D:\n  m_Width: 4\n  m_Height: 4\n"
        "  m_MipCount: 1\n  m_ColorSpace: 1\n  m_TextureFormat: 12\n  m_TextureDimension: 2\n"
        "  m_TextureSettings:\n    m_WrapU: 1\n    m_WrapV: 2\n    m_FilterMode: 2\n"
        "  m_ImageCount: 1\n  m_CompleteImageSize: 16\n  image data: 16\n"
        "  _typelessdata: ff000000000000001f0000f8e4e4e4e4\n  m_StreamData:\n    size: 0\n    path:\n";
    const std::string meta                    = "fileFormatVersion: 2\nguid: 86017fa9aef64ed4d9209b3d2f1e126f\n";
    request.files["Assets/Leaves.asset"]      = {source.begin(), source.end()};
    request.files["Assets/Leaves.asset.meta"] = {meta.begin(), meta.end()};
    auto imported                             = asset_import::prepareUnityProjectImport(request);
    REQUIRE(imported.ok());
    REQUIRE_EQ(imported.value().manifest.assets.size(), std::size_t(1));
    bool retained = false;
    for (const auto& entry : imported.value().entries)
        if (entry.path == "sources/unity/Assets/Leaves.asset") {
            REQUIRE_EQ(entry.bytes, request.files.at("Assets/Leaves.asset"));
            retained = true;
        }
    REQUIRE(retained);
    const std::string material =
        "--- !u!21 &2100000\nMaterial:\n"
        "  m_Shader: {fileID: 4800000, guid: 7befaa6f41d00a6478d5f4af21d66518, type: 3}\n"
        "  m_SavedProperties:\n    m_TexEnvs:\n    - _MainAlbedoTex:\n"
        "        m_Texture: {fileID: 2800000, guid: 86017fa9aef64ed4d9209b3d2f1e126f, type: 2}\n"
        "    m_Floats: []\n    m_Colors: []\n";
    const std::string materialMeta          = "fileFormatVersion: 2\nguid: 1234567890abcdef1234567890abcdef\n";
    request.files["Assets/Leaves.mat"]      = {material.begin(), material.end()};
    request.files["Assets/Leaves.mat.meta"] = {materialMeta.begin(), materialMeta.end()};
    auto bound                              = asset_import::prepareUnityProjectImport(request);
    REQUIRE(bound.ok());
    REQUIRE_EQ(bound.value().manifest.assets.size(), std::size_t(3));
    bool linked = false;
    for (const auto& dependency : bound.value().manifest.dependencies)
        if (dependency.to == imported.value().manifest.assets.front().asset) linked = true;
    REQUIRE(linked);
    bool samplerChecked = false;
    for (const auto& entry : bound.value().entries) {
        if (!entry.path.ends_with("/asset.json")) continue;
        auto json = Value::fromJson(std::string(entry.bytes.begin(), entry.bytes.end()));
        REQUIRE(json.ok());
        const auto& object = *json.value().getIf<Value::Object>();
        if (!object.contains("baseColorTextureSampler")) continue;
        const auto& sampler = *object.at("baseColorTextureSampler").getIf<Value::Object>();
        REQUIRE_EQ(sampler.at("wrapS").asInt(), 33071);
        REQUIRE_EQ(sampler.at("wrapT").asInt(), 33648);
        REQUIRE_EQ(sampler.at("minFilter").asInt(), 9729);
        REQUIRE_EQ(sampler.at("magFilter").asInt(), 9729);
        samplerChecked = true;
    }
    REQUIRE(samplerChecked);
    for (const auto& suffix : {std::string("  m_MipCount: 2\n"), std::string("  m_Width: 4\n")}) {
        auto invalid                         = source + suffix;
        request.files["Assets/Leaves.asset"] = {invalid.begin(), invalid.end()};
        REQUIRE(!asset_import::prepareUnityProjectImport(request).ok());
    }
    auto truncated = source;
    truncated.erase(truncated.find("ff0000"), 2);
    request.files["Assets/Leaves.asset"] = {truncated.begin(), truncated.end()};
    REQUIRE(!asset_import::prepareUnityProjectImport(request).ok());
}
TEST_CASE("asset.import.unityNativeTexturePublishesCompleteMipChain") {
    asset_import::UnityProjectImportRequest request;
    request.package = {*PersistentId::parse("11111111-2222-4333-8444-555555555555"), "bc3.mips", "1.0.0", {}};
    const std::string source =
        "--- !u!28 &2800000\nTexture2D:\n  m_Width: 4\n  m_Height: 4\n"
        "  m_MipCount: 3\n  m_ColorSpace: 1\n  m_TextureFormat: 12\n  m_TextureDimension: 2\n"
        "  m_ImageCount: 1\n  m_CompleteImageSize: 48\n  image data: 48\n"
        "  _typelessdata: ff000000000000001f0000f8e4e4e4e4ff000000000000001f0000f8e4e4e4e4"
        "ff000000000000001f0000f8e4e4e4e4\n  m_StreamData:\n    size: 0\n    path:\n";
    const std::string meta = "fileFormatVersion: 2\nguid: 86017fa9aef64ed4d9209b3d2f1e126f\n";
    request.files["Assets/Leaves.asset"] = {source.begin(), source.end()};
    request.files["Assets/Leaves.asset.meta"] = {meta.begin(), meta.end()};
    auto imported = asset_import::prepareUnityProjectImport(request);
    REQUIRE(imported.ok());
    const asset::EvaArchiveEntry* definition = nullptr;
    const asset::EvaArchiveEntry* blob = nullptr;
    for (const auto& entry : imported.value().entries) {
        if (entry.path.ends_with("/asset.json")) definition = &entry;
        if (entry.path.ends_with("source.rgba8-mips")) blob = &entry;
    }
    REQUIRE(definition != nullptr);
    REQUIRE(blob != nullptr);
    CHECK_EQ(blob->bytes.size(), std::size_t(84));
    auto json = Value::fromJson(std::string(definition->bytes.begin(), definition->bytes.end()));
    REQUIRE(json.ok());
    const auto& object = *json.value().getIf<Value::Object>();
    CHECK_EQ(object.at("encoding").asString(), std::string("rgba8-mips"));
    CHECK_EQ(object.at("mipCount").asInt(), std::int64_t(3));
    auto cooked = asset::cookCanonicalImageRgba8(definition->bytes, blob->bytes, 1024 * 1024);
    REQUIRE(cooked.ok());
    REQUIRE_EQ(cooked.value().bulk.size(), std::size_t(28 + 84));
    const auto& bytes = cooked.value().bulk;
    const auto levels = std::uint32_t(bytes[20]) | (std::uint32_t(bytes[21]) << 8) |
                        (std::uint32_t(bytes[22]) << 16) | (std::uint32_t(bytes[23]) << 24);
    CHECK_EQ(levels, std::uint32_t(3));
}
TEST_CASE("asset.import.bc3EndpointsSelectorsAndPartialBlocks") {
    for (bool descending : {false, true}) {
        std::vector<std::uint8_t> bytes(16);
        bytes[0] = descending ? 255 : 0;
        bytes[1] = descending ? 0 : 255;
        // Ascending color endpoints still require four opaque BC3 colors.
        bytes[8]                = 31;
        bytes[11]               = 248;
        std::uint64_t selectors = 0;
        for (unsigned i = 0; i < 16; ++i) selectors |= std::uint64_t(i % 8) << (3 * i);
        for (unsigned i = 0; i < 6; ++i) bytes[2 + i] = std::uint8_t(selectors >> (8 * i));
        for (unsigned i = 0; i < 4; ++i) bytes[12 + i] = 0xe4;
        auto decoded = asset::detail::decodeBc3Rgba8(bytes, 3, 4, 48);
        REQUIRE(decoded.ok());
        const unsigned alphaDescending[]{255, 0, 218, 182, 145, 109, 72, 36};
        const unsigned alphaAscending[]{0, 255, 51, 102, 153, 204, 0, 255};
        auto           full = asset::detail::decodeBc3Rgba8(bytes, 4, 4, 64);
        REQUIRE(full.ok());
        for (unsigned i = 0; i < 16; ++i)
            REQUIRE_EQ(unsigned(full.value()[i * 4 + 3]), descending ? alphaDescending[i % 8] : alphaAscending[i % 8]);
        REQUIRE_EQ(unsigned(full.value()[12]), 170u);
        REQUIRE_EQ(unsigned(full.value()[14]), 85u);
        for (unsigned y = 0; y < 4; ++y)
            for (unsigned x = 0; x < 3; ++x) {
                const auto at = (y * 3 + x) * 4;
                REQUIRE_EQ(unsigned(decoded.value()[at]), x == 0 ? 0u : x == 1 ? 255u : 85u);
                REQUIRE_EQ(unsigned(decoded.value()[at + 2]), x == 0 ? 255u : x == 1 ? 0u : 170u);
                REQUIRE_EQ(unsigned(decoded.value()[at + 3]),
                           descending ? alphaDescending[(y * 4 + x) % 8] : alphaAscending[(y * 4 + x) % 8]);
            }
        REQUIRE(!asset::detail::decodeBc3Rgba8(bytes, 3, 4, 47).ok());
        REQUIRE(!asset::detail::decodeBc3Rgba8(bytes, 0, 4, 48).ok());
        REQUIRE(!asset::detail::decodeBc3Rgba8(bytes, 0xffffffffu, 0xffffffffu, ~std::uint64_t(0)).ok());
        bytes.pop_back();
        REQUIRE(!asset::detail::decodeBc3Rgba8(bytes, 3, 4, 48).ok());
    }
}
