#include "asset/EvpackResourceReader.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve;
using namespace eve::asset;

namespace {
PersistentId id(std::string_view text) { auto value = PersistentId::parse(text); REQUIRE(value); return *value; }
AssetRef ref(std::string_view text) { auto value = AssetRef::parse(text); REQUIRE(value.ok()); return std::move(value).takeValue(); }
}  // namespace

TEST_CASE("asset.resourceReader.selectsBackendVariantAndPreservesCanonicalDefinition") {
    const auto assetId = id("550e8400-e29b-41d4-a716-446655440000");
    EvpackBuild input;
    input.packageId = id("018f6f22-2490-7ad2-bf58-4f1dbca31040");
    input.buildId = id("018f6f22-2490-7ad2-bf58-4f1dbca31041");
    input.variants = {{"windows", "x86_64", "vulkan", {"bc"}, "spirv-1.6", "high", {}},
                      {"web", "wasm32", "webgpu", {"rgba8"}, "wgsl-1", "high", {}}};
    const std::vector<std::uint8_t> canonical = {'{', '"', 'v', '"', ':', '1', '}'};
    input.chunks = {{assetId, "eve.mesh", SchemaVersion(1), 0, EvpackChunkKind::Definition, 0,
                     EvpackCodec::Zstd, 8, {}, canonical},
                    {assetId, "eve.mesh", SchemaVersion(1), 1, EvpackChunkKind::Definition, 0,
                     EvpackCodec::Zstd, 8, {}, canonical}};
    auto bytes = buildEvpack(std::move(input)); REQUIRE(bytes.ok());
    auto parsed = parseEvpack(bytes.value()); REQUIRE(parsed.ok());
    auto pack = std::make_shared<const Evpack>(std::move(parsed).takeValue());
    EvpackResourceReader reader(pack);
    auto windows = reader.read(ref("asset://550e8400-e29b-41d4-a716-446655440000"), "eve.mesh/1",
                               {"windows", "x86_64", "vulkan", {"bc"}, {"spirv-1.6"}, {"high"}, {}}, 1024);
    auto web = reader.read(ref("asset://550e8400-e29b-41d4-a716-446655440000"), "eve.mesh/1",
                           {"web", "wasm32", "webgpu", {"rgba8"}, {"wgsl-1"}, {"high"}, {}}, 1024);
    REQUIRE(windows.ok()); REQUIRE(web.ok());
    CHECK_EQ(windows.value().chunks.front().bytes, canonical);
    CHECK_EQ(web.value().chunks.front().bytes, canonical);
    CHECK_EQ(windows.value().variant.index, std::uint32_t(0));
    CHECK_EQ(web.value().variant.index, std::uint32_t(1));

    auto mismatch = reader.read(ref("asset://550e8400-e29b-41d4-a716-446655440000"), "eve.image/2",
                                {"windows", "x86_64", "vulkan", {"bc"}, {"spirv-1.6"}, {"high"}, {}}, 1024);
    REQUIRE(!mismatch.ok()); CHECK_EQ(mismatch.error()->code(), DiagnosticCode::TypeMismatch);
}
