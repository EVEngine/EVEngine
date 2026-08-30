#include "asset_graphics/EvpackGraphicsLoader.h"
#include "asset_graphics/EvpackImageLoader.h"

#include "graphics/IMeshResourceFactory.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <bit>

using namespace eve;
using namespace eve::asset;
using namespace eve::asset_graphics;

namespace {

PersistentId persistentId(std::string_view text) {
    auto parsed = PersistentId::parse(text);
    REQUIRE(parsed);
    return *parsed;
}

AssetRef assetRef(std::string_view text) {
    auto parsed = AssetRef::parse(text);
    REQUIRE(parsed.ok());
    return std::move(parsed).takeValue();
}

void put32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (unsigned shift = 0; shift != 32; shift += 8)
        bytes.push_back(static_cast<std::uint8_t>(value >> shift));
}

void putFloat(std::vector<std::uint8_t>& bytes, float value) {
    put32(bytes, std::bit_cast<std::uint32_t>(value));
}

std::vector<std::uint8_t> triangleBlob() {
    std::vector<std::uint8_t> bytes = {'E', 'V', 'M', 'E', 'S', 'H', 0, 1};
    put32(bytes, 3);
    put32(bytes, 3);
    put32(bytes, 3);
    put32(bytes, 0);
    const float vertices[][8] = {{0, 0, 0, 0, 0, 1, 0, 0},
                                 {1, 0, 0, 0, 0, 1, 1, 0},
                                 {0, 1, 0, 0, 0, 1, 0, 1}};
    for (const auto& vertex : vertices)
        for (float value : vertex) putFloat(bytes, value);
    put32(bytes, 0);
    put32(bytes, 1);
    put32(bytes, 2);
    return bytes;
}

std::shared_ptr<const Evpack> makePack(std::vector<std::uint8_t> mesh) {
    const auto id = persistentId("550e8400-e29b-41d4-a716-446655440000");
    EvpackBuild build;
    build.packageId = persistentId("018f6f22-2490-7ad2-bf58-4f1dbca31040");
    build.buildId = persistentId("018f6f22-2490-7ad2-bf58-4f1dbca31041");
    build.variants = {{"windows", "x86_64", "vulkan", {"bc"}, "spirv-1.6", "high", {}}};
    build.chunks = {{id, "eve.mesh", SchemaVersion(1), 0, EvpackChunkKind::Definition, 0,
                     EvpackCodec::None, 8, {}, {'{', '}' }},
                    {id, "eve.mesh", SchemaVersion(1), 0, EvpackChunkKind::Bulk, 1,
                     EvpackCodec::None, 16, {}, std::move(mesh)}};
    auto bytes = buildEvpack(std::move(build));
    REQUIRE(bytes.ok());
    auto parsed = parseEvpack(bytes.value());
    REQUIRE(parsed.ok());
    return std::make_shared<const Evpack>(std::move(parsed).takeValue());
}

class RecordingMeshFactory final : public eve::graphics::IMeshResourceFactory {
public:
    Result<eve::graphics::Mesh*> uploadMesh(const float* positions, const float* normals,
                                            const float* texcoords, int vertexCount,
                                            const std::uint32_t* indices, int indexCount) override {
        ++uploads;
        observedPositions.assign(positions, positions + vertexCount * 3);
        observedNormals.assign(normals, normals + vertexCount * 3);
        observedTexcoords.assign(texcoords, texcoords + vertexCount * 2);
        observedIndices.assign(indices, indices + indexCount);
        if (reject)
            return Result<eve::graphics::Mesh*>::failure(
                Diagnostic::error(DiagnosticCode::Failed, "injected upload rejection"));
        return Result<eve::graphics::Mesh*>::success(
            reinterpret_cast<eve::graphics::Mesh*>(&token));
    }

    Result<void> releaseMesh(eve::graphics::Mesh*) override { return Result<void>::success(); }

    bool reject = false;
    int uploads = 0;
    std::vector<float> observedPositions;
    std::vector<float> observedNormals;
    std::vector<float> observedTexcoords;
    std::vector<std::uint32_t> observedIndices;

private:
    std::uint64_t token = 0;
};

class RecordingImageFactory final : public eve::graphics::IImageResourceFactory {
public:
    Result<eve::graphics::Texture*> uploadRgba8(std::uint32_t, std::uint32_t,
                                                const std::uint8_t*, bool) override {
        ++uploads;
        if (reject)
            return Result<eve::graphics::Texture*>::failure(
                Diagnostic::error(DiagnosticCode::Failed, "injected image upload rejection"));
        return Result<eve::graphics::Texture*>::success(
            reinterpret_cast<eve::graphics::Texture*>(&token));
    }
    Result<void> releaseImage(eve::graphics::Texture*) override {
        return Result<void>::success();
    }
    bool reject = false;
    int uploads = 0;

private:
    std::uint64_t token = 0;
};

std::shared_ptr<const Evpack> makeImagePack(std::vector<std::uint8_t> image) {
    const auto id = persistentId("550e8400-e29b-41d4-a716-446655440000");
    EvpackBuild build;
    build.packageId = persistentId("018f6f22-2490-7ad2-bf58-4f1dbca31040");
    build.buildId = persistentId("018f6f22-2490-7ad2-bf58-4f1dbca31041");
    build.variants = {{"windows", "x86_64", "vulkan", {"rgba8"}, "spirv-1.6", "high", {}}};
    build.chunks = {{id, "eve.image", SchemaVersion(2), 0, EvpackChunkKind::Definition, 0,
                     EvpackCodec::None, 8, {}, {'{', '}' }},
                    {id, "eve.image", SchemaVersion(2), 0, EvpackChunkKind::Bulk, 1,
                     EvpackCodec::None, 8, {}, std::move(image)}};
    auto bytes = buildEvpack(std::move(build));
    REQUIRE(bytes.ok());
    auto parsed = parseEvpack(bytes.value());
    REQUIRE(parsed.ok());
    return std::make_shared<const Evpack>(std::move(parsed).takeValue());
}

std::vector<std::uint8_t> onePixelEvimg() {
    std::vector<std::uint8_t> bytes = {'E', 'V', 'I', 'M', 'G', 0, 1, 0};
    put32(bytes, 1);
    put32(bytes, 1);
    put32(bytes, 0);
    put32(bytes, 0);
    bytes.insert(bytes.end(), {10, 20, 30, 255});
    return bytes;
}

const EvpackCapabilities capabilities = {"windows", "x86_64", "vulkan", {"bc"},
                                         {"spirv-1.6"}, {"high"}, {}};

}  // namespace

TEST_CASE("asset.graphics.validatesCanonicalMeshBeforeBackendUpload") {
    EvpackResourceReader reader(makePack(triangleBlob()));
    RecordingMeshFactory factory;
    EvpackGraphicsLoader loader(reader, factory);
    auto loaded = loader.loadMesh(assetRef("asset://550e8400-e29b-41d4-a716-446655440000"),
                                  capabilities);
    REQUIRE(loaded.ok());
    CHECK(loaded.value().mesh != nullptr);
    CHECK_EQ(factory.uploads, 1);
    CHECK_EQ(factory.observedPositions.size(), std::size_t(9));
    CHECK_EQ(factory.observedNormals.size(), std::size_t(9));
    CHECK_EQ(factory.observedTexcoords.size(), std::size_t(6));
    CHECK_EQ(factory.observedIndices, std::vector<std::uint32_t>({0, 1, 2}));
}

TEST_CASE("asset.graphics.invalidMeshNeverReachesBackend") {
    auto bytes = triangleBlob();
    bytes.back() = 9;
    EvpackResourceReader reader(makePack(std::move(bytes)));
    RecordingMeshFactory factory;
    EvpackGraphicsLoader loader(reader, factory);
    auto loaded = loader.loadMesh(assetRef("asset://550e8400-e29b-41d4-a716-446655440000"),
                                  capabilities);
    REQUIRE(!loaded.ok());
    CHECK_EQ(loaded.error()->code(), DiagnosticCode::ParseError);
    CHECK_EQ(factory.uploads, 0);
}

TEST_CASE("asset.graphics.backendRejectionIsStructuredFailure") {
    EvpackResourceReader reader(makePack(triangleBlob()));
    RecordingMeshFactory factory;
    factory.reject = true;
    EvpackGraphicsLoader loader(reader, factory);
    auto loaded = loader.loadMesh(assetRef("asset://550e8400-e29b-41d4-a716-446655440000"),
                                  capabilities);
    REQUIRE(!loaded.ok());
    CHECK_EQ(loaded.error()->code(), DiagnosticCode::Failed);
    CHECK_EQ(factory.uploads, 1);
}

TEST_CASE("asset.graphics.imageValidationPrecedesTextureUpload") {
    auto invalid = onePixelEvimg();
    invalid.pop_back();
    EvpackResourceReader reader(makeImagePack(std::move(invalid)));
    RecordingImageFactory factory;
    EvpackImageLoader loader(reader, factory);
    auto loaded = loader.load(assetRef("asset://550e8400-e29b-41d4-a716-446655440000"),
                              {"windows", "x86_64", "vulkan", {"rgba8"},
                               {"spirv-1.6"}, {"high"}, {}});
    REQUIRE(!loaded.ok());
    CHECK_EQ(loaded.error()->code(), DiagnosticCode::InvalidArgument);
    CHECK_EQ(factory.uploads, 0);
}

TEST_CASE("asset.graphics.imageBackendRejectionIsStructuredFailure") {
    EvpackResourceReader reader(makeImagePack(onePixelEvimg()));
    RecordingImageFactory factory;
    factory.reject = true;
    EvpackImageLoader loader(reader, factory);
    auto loaded = loader.load(assetRef("asset://550e8400-e29b-41d4-a716-446655440000"),
                              {"windows", "x86_64", "vulkan", {"rgba8"},
                               {"spirv-1.6"}, {"high"}, {}});
    REQUIRE(!loaded.ok());
    CHECK_EQ(loaded.error()->code(), DiagnosticCode::Failed);
    CHECK_EQ(factory.uploads, 1);
}
