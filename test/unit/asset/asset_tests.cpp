#include <array>
#include <utility>
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_asset_dependency)
#include "PrivateTestScope.h"
#include "asset/AssetDependency.h"

namespace test_asset_dependency {
using namespace eve;
using namespace eve::asset;
namespace {
AssetRef asset(std::string_view uri) {
    auto parsed = AssetRef::parse(uri);
    REQUIRE(parsed.ok());
    return std::move(parsed).takeValue();
}
EvaAssetEntry entry(AssetRef id, std::string type) {
    return {std::move(id),
            std::move(type),
            SchemaVersion(1),
            "definition.json",
            "sha256:0000000000000000000000000000000000000000000000000000000000000000",
            {}};
}
}  // namespace

TEST_CASE("Asset dependency validation contract") {
    SUB_CASE("Asset dependency required provider must exist with exact type and schema") {
        const auto  root     = asset("asset://550e8400-e29b-41d4-a716-446655440000");
        const auto  provider = asset("asset://550e8400-e29b-41d4-a716-446655440001");
        EvaManifest manifest;
        manifest.assets.push_back(entry(root, "eve.material"));
        manifest.dependencies.push_back(
            {root, provider, EvaDependencyKind::RuntimeRequired, "baseColor", {}, "eve.image/1"});
        auto absent = validateEvaDependencies(manifest);
        REQUIRE(!absent.ok());
        CHECK(absent.error()->code() == DiagnosticCode::NotFound);
        const std::array wrongProviders{AvailableAssetDependency{provider, "eve.mesh", SchemaVersion(1)}};
        auto             wrong = validateEvaDependencies(manifest, wrongProviders);
        REQUIRE(!wrong.ok());
        CHECK(wrong.error()->code() == DiagnosticCode::TypeMismatch);
        const std::array resolvedProviders{AvailableAssetDependency{provider, "eve.image", SchemaVersion(1)}};
        auto             resolved = validateEvaDependencies(manifest, resolvedProviders);
        REQUIRE(resolved.ok());
        CHECK(resolved.value().omittedOptionalAssets.empty());
    };

    SUB_CASE("Asset dependency optional provider requires observable fallback and reports omission") {
        const auto  root   = asset("asset://550e8400-e29b-41d4-a716-446655440000");
        const auto  detail = asset("asset://550e8400-e29b-41d4-a716-446655440002");
        EvaManifest manifest;
        manifest.assets.push_back(entry(root, "eve.material"));
        manifest.dependencies.push_back(
            {root, detail, EvaDependencyKind::RuntimeOptional, "detail", {}, "eve.image/1"});
        auto noPolicy = validateEvaDependencies(manifest);
        REQUIRE(!noPolicy.ok());
        CHECK(noPolicy.error()->code() == DiagnosticCode::InvalidArgument);
        manifest.dependencies.back().fallback = {{"behavior", Value("use-default")},
                                                 {"observableCode", Value("material.detail-defaulted")}};
        auto result                           = validateEvaDependencies(manifest);
        REQUIRE(result.ok());
        REQUIRE_EQ(result.value().omittedOptionalAssets.size(), 1u);
        CHECK(result.value().omittedOptionalAssets.front() == detail.id());
    };

    SUB_CASE("Asset dependency rejects required cycle and foreign source identity") {
        const auto  first  = asset("asset://550e8400-e29b-41d4-a716-446655440000");
        const auto  second = asset("asset://550e8400-e29b-41d4-a716-446655440001");
        EvaManifest manifest;
        manifest.assets = {entry(first, "eve.material"), entry(second, "eve.image")};
        manifest.dependencies.push_back(
            {first, second, EvaDependencyKind::RuntimeRequired, "image", {}, "eve.image/1"});
        manifest.dependencies.push_back(
            {second, first, EvaDependencyKind::RuntimeRequired, "owner", {}, "eve.material/1"});
        auto cycle = validateEvaDependencies(manifest);
        REQUIRE(!cycle.ok());
        CHECK(cycle.error()->code() == DiagnosticCode::Conflict);
        manifest.dependencies.clear();
        const auto foreign = asset("asset://550e8400-e29b-41d4-a716-446655440009");
        manifest.dependencies.push_back(
            {foreign, first, EvaDependencyKind::RuntimeRequired, "bad", {}, "eve.material/1"});
        auto missingSource = validateEvaDependencies(manifest);
        REQUIRE(!missingSource.ok());
        CHECK(missingSource.error()->code() == DiagnosticCode::NotFound);
    };
}
}  // namespace test_asset_dependency
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_asset_diff)
#include "PrivateTestScope.h"
#include "asset/AssetDiff.h"
namespace test_asset_diff {
using namespace eve;
using namespace eve::asset;
namespace {
AssetRef asset(std::string_view uri) {
    auto parsed = AssetRef::parse(uri);
    REQUIRE(parsed.ok());
    return std::move(parsed).takeValue();
}
EvaAssetEntry entry(AssetRef id, std::uint64_t version, char hash) {
    return {std::move(id), "eve.image", SchemaVersion(version), "asset.json", "sha256:" + std::string(64, hash), {}};
}
}  // namespace
TEST_CASE("Asset manifest diff contract") {
    SUB_CASE("Asset manifest diff orders identity changes independently of input order") {
        const auto  a = asset("asset://550e8400-e29b-41d4-a716-446655440000");
        const auto  b = asset("asset://550e8400-e29b-41d4-a716-446655440001");
        const auto  c = asset("asset://550e8400-e29b-41d4-a716-446655440002");
        EvaManifest before, after;
        before.assets = {entry(b, 1, '1'), entry(a, 1, '2')};
        after.assets  = {entry(c, 1, '3'), entry(a, 2, '4')};
        before.entrypoints.emplace("default", a);
        after.entrypoints.emplace("default", c);
        before.dependencies.push_back({a, b, EvaDependencyKind::RuntimeRequired, "base", {}, "eve.image/1"});
        auto diff = diffEvaManifests(before, after);
        REQUIRE(diff.ok());
        REQUIRE_EQ(diff.value().assets.size(), 3u);
        CHECK(diff.value().assets[0].assetId == a.id());
        CHECK(diff.value().assets[0].kind == EvaAssetChangeKind::Changed);
        CHECK(diff.value().assets[1].kind == EvaAssetChangeKind::Removed);
        CHECK(diff.value().assets[2].kind == EvaAssetChangeKind::Added);
        CHECK_EQ(diff.value().removedDependencies, 1u);
        CHECK(diff.value().entrypointsChanged);
        auto identity = diffEvaManifests(before, before);
        REQUIRE(identity.ok());
        CHECK(identity.value().empty());
    };
    SUB_CASE("Asset manifest diff treats optional fallback policy as dependency identity") {
        const auto  a = asset("asset://550e8400-e29b-41d4-a716-446655440000");
        const auto  b = asset("asset://550e8400-e29b-41d4-a716-446655440001");
        EvaManifest before, after;
        before.assets = {entry(a, 1, '1')};
        before.dependencies.push_back(
            {a,
             b,
             EvaDependencyKind::RuntimeOptional,
             "detail",
             {},
             "eve.image/1",
             {{"behavior", Value("omit-feature")}, {"observableCode", Value("detail.omitted")}}});
        after                                           = before;
        after.dependencies.front().fallback["behavior"] = Value("use-default");
        auto diff                                       = diffEvaManifests(before, after);
        REQUIRE(diff.ok());
        CHECK_EQ(diff.value().removedDependencies, 1u);
        CHECK_EQ(diff.value().addedDependencies, 1u);
    };
}
}  // namespace test_asset_diff
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_asset_eva_archive)
#include <algorithm>
#include "PrivateTestScope.h"
#include "asset/EvaArchive.h"
#include "asset/EvaManifest.h"
namespace test_asset_eva_archive {
using namespace eve;
using namespace eve::asset;
namespace {
constexpr std::string_view minimal = R"json({
  "schema":"eve.asset-archive","schemaVersion":1,
  "packageId":"018f6f22-2490-7ad2-bf58-4f1dbca31040",
  "packageName":"test.archive","packageVersion":"1.0.0","unknownFields":"preserve",
  "assets":[],"dependencies":[],"entrypoints":{},"provenance":{}
})json";
EvaManifest                manifest() {
    auto parsed = parseEvaManifest(minimal);
    REQUIRE(parsed.ok());
    return std::move(parsed).takeValue();
}
}  // namespace
TEST_CASE("EVA archive payload and path contract") {
    SUB_CASE("EVA archive output is order independent and round trips sorted payload paths") {
        std::vector<EvaArchiveEntry> entries = {{"sources/terrain.raw", {4, 5, 6}},
                                                {"assets/definition.json", {'{', '}'}}};
        auto                         first   = buildEvaArchive(manifest(), entries);
        std::reverse(entries.begin(), entries.end());
        auto second = buildEvaArchive(manifest(), entries);
        REQUIRE(first.ok());
        REQUIRE(second.ok());
        CHECK_EQ(first.value(), second.value());
        auto parsed = parseEvaArchive(first.value());
        REQUIRE(parsed.ok());
        CHECK_EQ(parsed.value().manifest.packageName, std::string("test.archive"));
        REQUIRE_EQ(parsed.value().entries.size(), 2u);
        CHECK_EQ(parsed.value().entries[0].path, std::string("assets/definition.json"));
    };
    SUB_CASE("EVA archive compresses repeated bytes deterministically and restores decoded content") {
        std::vector<std::uint8_t> repeated(4096, 0x5a);
        auto                      first  = buildEvaArchive(manifest(), {{"payload.bin", repeated}});
        auto                      second = buildEvaArchive(manifest(), {{"payload.bin", repeated}});
        REQUIRE(first.ok());
        REQUIRE(second.ok());
        CHECK_EQ(first.value(), second.value());
        CHECK(first.value().size() < repeated.size());
        auto parsed = parseEvaArchive(first.value());
        REQUIRE(parsed.ok());
        CHECK_EQ(parsed.value().entries[0].bytes, repeated);
    };
    SUB_CASE("EVA archive rejects traversal reserved and duplicate canonical names") {
        auto traversal = buildEvaArchive(manifest(), {{"../secret", {1}}});
        REQUIRE(!traversal.ok());
        CHECK(traversal.error()->code() == DiagnosticCode::InvalidArgument);
        auto reserved = buildEvaArchive(manifest(), {{"manifest.json", {1}}});
        REQUIRE(!reserved.ok());
        CHECK(reserved.error()->code() == DiagnosticCode::Conflict);
        auto duplicate = buildEvaArchive(manifest(), {{"same.bin", {1}}, {"same.bin", {2}}});
        REQUIRE(!duplicate.ok());
        CHECK(duplicate.error()->code() == DiagnosticCode::Conflict);
        const std::string composed   = "sources/caf\xc3\xa9.txt";
        const std::string decomposed = "sources/cafe\xcc\x81.txt";
        REQUIRE(buildEvaArchive(manifest(), {{composed, {1}}}).ok());
        CHECK(!buildEvaArchive(manifest(), {{decomposed, {1}}}).ok());
    };
    SUB_CASE("EVA archive verifies payload hash and decoded-size budget before returning entries") {
        const std::vector<std::uint8_t> marker = {0xde, 0xad, 0xbe, 0xef};
        auto                            built  = buildEvaArchive(manifest(), {{"payload.bin", marker}});
        REQUIRE(built.ok());
        auto corrupt = built.value();
        auto found   = std::search(corrupt.begin(), corrupt.end(), marker.begin(), marker.end());
        REQUIRE(found != corrupt.end());
        *found ^= 0xff;
        auto rejected = parseEvaArchive(corrupt);
        REQUIRE(!rejected.ok());
        CHECK(rejected.error()->code() == DiagnosticCode::HashMismatch);
        EvaArchiveLimits limits;
        limits.maximumEntryBytes = 3;
        CHECK(!parseEvaArchive(built.value(), limits).ok());
        CHECK(!buildEvaArchive(manifest(), {{"payload.bin", marker}}, limits).ok());
    };
    SUB_CASE("EVA content-addressed blob name must match actual bytes") {
        const std::string digest   = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
        auto              matching = buildEvaArchive(manifest(), {{"blobs/sha256/" + digest, {}}});
        REQUIRE(matching.ok());
        REQUIRE(parseEvaArchive(matching.value()).ok());
        auto mismatch = buildEvaArchive(manifest(), {{"blobs/sha256/" + digest, {'x'}}});
        REQUIRE(!mismatch.ok());
        CHECK(mismatch.error()->code() == DiagnosticCode::HashMismatch);
    };
}
TEST_CASE("EVA manifest schema contract") {
    SUB_CASE("EVA manifest preserves extensions and rejects future schema version") {
        std::string source(minimal);
        const auto  end = source.rfind('}');
        REQUIRE(end != std::string::npos);
        source.insert(end, ",\"vendorExtension\":{\"kept\":true}");
        auto parsed = parseEvaManifest(source);
        REQUIRE(parsed.ok());
        CHECK(parsed.value().extensions.contains("vendorExtension"));
        auto encoded = serializeEvaManifest(parsed.value());
        REQUIRE(encoded.ok());
        auto restored = parseEvaManifest(encoded.value());
        REQUIRE(restored.ok());
        CHECK(restored.value().extensions.contains("vendorExtension"));
        const auto version = source.find("\"schemaVersion\":1");
        REQUIRE(version != std::string::npos);
        source.replace(version, 17, "\"schemaVersion\":2");
        auto future = parseEvaManifest(source);
        REQUIRE(!future.ok());
        CHECK(future.error()->code() == DiagnosticCode::UnknownVersion);
    };
    SUB_CASE("EVA manifest requires predicate for platform dependency") {
        std::string source(minimal);
        const auto  dependency = source.find("\"dependencies\":[]");
        REQUIRE(dependency != std::string::npos);
        source.replace(dependency, std::string("\"dependencies\":[]").size(),
                       R"json("dependencies":[{"from":"asset://550e8400-e29b-41d4-a716-446655440000",
                         "to":"asset://550e8400-e29b-41d4-a716-446655440001","kind":"platform"}])json");
        auto rejected = parseEvaManifest(source);
        REQUIRE(!rejected.ok());
        CHECK(rejected.error()->code() == DiagnosticCode::InvalidArgument);
        CHECK_EQ(rejected.error()->path(), std::string("$.dependencies[0].predicate"));
    };
}
}  // namespace test_asset_eva_archive
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_asset_evpack)
#include <algorithm>
#include <memory>
#include "PrivateTestScope.h"
#include "asset/Evpack.h"
#include "asset/EvpackRange.h"
#include "asset/EvpackRegistry.h"
namespace test_asset_evpack {
using namespace eve;
using namespace eve::asset;
namespace {
PersistentId id(std::string_view text) {
    auto parsed = PersistentId::parse(text);
    REQUIRE(parsed.has_value());
    return *parsed;
}
EvpackVariant desktop(std::string quality) {
    return {"windows", "x86_64", "vulkan", {"bc", "rgba8"}, "spirv-1.6", std::move(quality), {}};
}
EvpackBuild package() {
    EvpackBuild build;
    build.packageId = id("018f6f22-2490-7ad2-bf58-4f1dbca31040");
    build.buildId   = id("018f6f22-2490-7ad2-bf58-4f1dbca31041");
    build.variants  = {desktop("high"), desktop("medium")};
    build.chunks    = {{id("550e8400-e29b-41d4-a716-446655440001"),
                        "eve.mesh",
                        SchemaVersion(2),
                        0,
                        EvpackChunkKind::Bulk,
                        0,
                        EvpackCodec::None,
                        16,
                        {},
                        {1, 2, 3, 4}},
                       {id("550e8400-e29b-41d4-a716-446655440000"),
                        "eve.material",
                        SchemaVersion(1),
                        1,
                        EvpackChunkKind::Definition,
                        0,
                        EvpackCodec::None,
                        8,
                        {id("550e8400-e29b-41d4-a716-446655440001")},
                        {'m', 'a', 't'}}};
    return build;
}
class CountingRangeSource final : public EvpackRangeSource {
public:
    explicit CountingRangeSource(std::vector<std::uint8_t> input) : bytes(std::move(input)) {}
    Result<std::uint64_t>             size() const override { return Result<std::uint64_t>::success(bytes.size()); }
    Result<std::vector<std::uint8_t>> read(std::uint64_t offset, std::uint64_t count) const override {
        ++reads;
        if (offset > bytes.size() || count > bytes.size() - offset)
            return Result<std::vector<std::uint8_t>>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "out-of-range test read"));
        return Result<std::vector<std::uint8_t>>::success({bytes.begin() + static_cast<std::size_t>(offset),
                                                           bytes.begin() + static_cast<std::size_t>(offset + count)});
    }
    mutable std::size_t               reads = 0;
    mutable std::vector<std::uint8_t> bytes;
};
class TestSigner final : public EvpackSigner {
public:
    Result<EvpackSignature> sign(std::span<const std::uint8_t, 32> digest) const override {
        EvpackSignature signature;
        signature.algorithm = "test-digest";
        signature.keyId.fill(0x2a);
        for (std::size_t i = 0; i < 64; ++i) signature.bytes[i] = digest[i % 32] ^ 0x5a;
        return Result<EvpackSignature>::success(std::move(signature));
    }
};
class TestVerifier final : public EvpackSignatureVerifier {
public:
    Result<void> verify(std::span<const std::uint8_t, 32> digest, const EvpackSignature& signature) const override {
        if (signature.algorithm != "test-digest" || !std::all_of(signature.keyId.begin(), signature.keyId.end(),
                                                                 [](std::uint8_t value) { return value == 0x2a; }))
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::PreconditionViolation, "untrusted test signature"));
        for (std::size_t i = 0; i < 64; ++i)
            if (signature.bytes[i] != (digest[i % 32] ^ 0x5a))
                return Result<void>::failure(Diagnostic::error(DiagnosticCode::HashMismatch, "signature mismatch"));
        return Result<void>::success();
    }
};
}  // namespace
TEST_CASE("EVPACK build parse selection and range contract") {
    SUB_CASE("EVPACK bytes are deterministic across chunk order and support random-access payload") {
        auto first = package(), reordered = package();
        std::reverse(reordered.chunks.begin(), reordered.chunks.end());
        auto a = buildEvpack(std::move(first)), b = buildEvpack(std::move(reordered));
        REQUIRE(a.ok());
        REQUIRE(b.ok());
        CHECK_EQ(a.value(), b.value());
        auto parsed = parseEvpack(a.value());
        REQUIRE(parsed.ok());
        REQUIRE_EQ(parsed.value().chunks().size(), 2u);
        CHECK_EQ(parsed.value().chunks()[0].type, std::string("eve.material"));
        auto payload = parsed.value().chunkBytes(0);
        REQUIRE(payload.ok());
        CHECK_EQ(std::vector<std::uint8_t>(payload.value().begin(), payload.value().end()),
                 std::vector<std::uint8_t>({'m', 'a', 't'}));
    };
    SUB_CASE("EVPACK rejects duplicate chunk key invalid UTF-8 and byte budget overflow") {
        auto duplicate = package();
        duplicate.chunks.push_back(duplicate.chunks.front());
        auto conflict = buildEvpack(std::move(duplicate));
        REQUIRE(!conflict.ok());
        CHECK(conflict.error()->code() == DiagnosticCode::Conflict);
        EvpackLimits limits;
        limits.maximumChunkBytes = 2;
        CHECK(!buildEvpack(package(), limits).ok());
        auto invalid                = package();
        invalid.variants[0].quality = std::string("\xc0\x80", 2);
        CHECK(!buildEvpack(std::move(invalid)).ok());
    };
    SUB_CASE("EVPACK selection reports quality fallback and rejects unsupported graphics backend") {
        auto bytes = buildEvpack(package());
        REQUIRE(bytes.ok());
        auto parsed = parseEvpack(bytes.value());
        REQUIRE(parsed.ok());
        EvpackCapabilities capability{"windows", "x86_64", "vulkan", {"bc"}, {"spirv-1.6"}, {"medium"}, {}};
        auto               selected = selectEvpackVariant(parsed.value(), capability);
        REQUIRE(selected.ok());
        CHECK_EQ(selected.value().index, 1u);
        CHECK(selected.value().usedFallback);
        capability.graphics = "webgpu";
        auto unsupported    = selectEvpackVariant(parsed.value(), capability);
        REQUIRE(!unsupported.ok());
        CHECK(unsupported.error()->code() == DiagnosticCode::Unsupported);
    };
    SUB_CASE("EVPACK range mount fetches metadata first and verifies each requested chunk") {
        auto bytes = buildEvpack(package());
        REQUIRE(bytes.ok());
        auto source  = std::make_shared<CountingRangeSource>(bytes.value());
        auto mounted = prepareEvpackRangeMount(source);
        REQUIRE(mounted.ok());
        CHECK_EQ(source->reads, 2u);
        CHECK(!mounted.value().index().chunkBytes(0).ok());
        auto chunk = mounted.value().readChunk(0);
        REQUIRE(chunk.ok());
        CHECK_EQ(source->reads, 3u);
        CHECK_EQ(chunk.value(), std::vector<std::uint8_t>({'m', 'a', 't'}));
        const auto& second = mounted.value().index().chunks()[1];
        source->bytes[static_cast<std::size_t>(second.offset)] ^= 0xff;
        auto corrupted = mounted.value().readChunk(1);
        REQUIRE(!corrupted.ok());
        CHECK(corrupted.error()->code() == DiagnosticCode::HashMismatch);
    };
}
TEST_CASE("EVPACK registry publication and callback contract") {
    SUB_CASE("EVPACK registry prepare failure leaves mounted generation and replacement stales old handle") {
        auto bytes = buildEvpack(package());
        REQUIRE(bytes.ok());
        auto candidate = prepareEvpackMount(bytes.value());
        REQUIRE(candidate.ok());
        EvpackRegistry registry;
        auto           first = registry.commit(std::move(candidate).takeValue());
        REQUIRE(first.ok());
        auto corrupt = bytes.value();
        corrupt[30] ^= 0x40;
        CHECK(!prepareEvpackMount(corrupt).ok());
        CHECK(registry.resolve(first.value().handle).ok());
        auto replacement            = package();
        replacement.buildId         = id("018f6f22-2490-7ad2-bf58-4f1dbca31042");
        replacement.chunks[0].bytes = {9, 8, 7};
        auto newer                  = buildEvpack(std::move(replacement));
        REQUIRE(newer.ok());
        auto ready = prepareEvpackMount(newer.value());
        REQUIRE(ready.ok());
        auto second = registry.commit(std::move(ready).takeValue());
        REQUIRE(second.ok());
        CHECK(second.value().replacedExisting);
        auto stale = registry.resolve(first.value().handle);
        REQUIRE(!stale.ok());
        CHECK(stale.error()->code() == DiagnosticCode::StaleHandle);
        CHECK(registry.resolve(second.value().handle).ok());
    };
    SUB_CASE("EVPACK registry requires external provider before consumer commit") {
        const auto external = id("550e8400-e29b-41d4-a716-446655440099");
        auto       consumer = package();
        consumer.chunks[0].dependencies.push_back(external);
        auto consumerBytes = buildEvpack(std::move(consumer));
        REQUIRE(consumerBytes.ok());
        auto candidate = prepareEvpackMount(consumerBytes.value());
        REQUIRE(candidate.ok());
        EvpackRegistry registry;
        auto           missing = registry.commit(std::move(candidate).takeValue());
        REQUIRE(!missing.ok());
        CHECK(missing.error()->code() == DiagnosticCode::NotFound);

        EvpackBuild provider;
        provider.packageId = id("018f6f22-2490-7ad2-bf58-4f1dbca31090");
        provider.buildId   = id("018f6f22-2490-7ad2-bf58-4f1dbca31091");
        provider.variants  = {desktop("high")};
        provider.chunks    = {{external,
                               "eve.image",
                               SchemaVersion(2),
                               0,
                               EvpackChunkKind::Definition,
                               0,
                               EvpackCodec::None,
                               8,
                               {},
                               {'{', '}'}}};
        auto providerBytes = buildEvpack(std::move(provider));
        REQUIRE(providerBytes.ok());
        auto providerCandidate = prepareEvpackMount(providerBytes.value());
        REQUIRE(providerCandidate.ok());
        REQUIRE(registry.commit(std::move(providerCandidate).takeValue()).ok());
        auto retry = prepareEvpackMount(consumerBytes.value());
        REQUIRE(retry.ok());
        REQUIRE(registry.commit(std::move(retry).takeValue()).ok());
        auto externalRef = AssetRef::fromId(external);
        REQUIRE(externalRef.ok());
        auto handle = registry.resolveAsset(externalRef.value(), "eve.image/2",
                                            {"windows", "x86_64", "vulkan", {"bc"}, {"spirv-1.6"}, {"high"}, {}});
        REQUIRE(handle.ok());
        REQUIRE(registry.readAsset(handle.value(), 1024).ok());
    };
    SUB_CASE("EVPACK registry callback may unsubscribe itself and callback failure is reported separately") {
        EvpackRegistry                       registry;
        EvpackRegistrySubscription           self;
        std::vector<EvpackRegistryEventKind> observed;
        auto subscribed = registry.subscribe([&](const EvpackRegistryEvent& event) -> Result<void> {
            observed.push_back(event.kind);
            return registry.unsubscribe(self);
        });
        REQUIRE(subscribed.ok());
        self         = subscribed.value();
        auto failing = registry.subscribe([](const EvpackRegistryEvent&) -> Result<void> {
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::CallbackFailure, "injected subscriber failure"));
        });
        REQUIRE(failing.ok());
        auto bytes = buildEvpack(package());
        REQUIRE(bytes.ok());
        auto candidate = prepareEvpackMount(bytes.value());
        REQUIRE(candidate.ok());
        auto mounted = registry.commit(std::move(candidate).takeValue());
        REQUIRE(mounted.ok());
        REQUIRE_EQ(observed.size(), 1u);
        CHECK(observed[0] == EvpackRegistryEventKind::Mounted);
        REQUIRE_EQ(mounted.value().callbackDiagnostics.size(), 1u);
        CHECK(mounted.value().callbackDiagnostics[0].code() == DiagnosticCode::CallbackFailure);
        REQUIRE(registry.unmount(mounted.value().handle).ok());
        CHECK_EQ(observed.size(), 1u);
        REQUIRE(registry.unsubscribe(failing.value()).ok());
    };
}
TEST_CASE("EVPACK compressed content and signature contract") {
    SUB_CASE("EVPACK compressed chunk requires decoded read and enforces decoded-byte budget") {
        auto                      input = package();
        std::vector<std::uint8_t> decoded(64 * 1024, 0x5a);
        input.chunks[0].codec = EvpackCodec::Zstd;
        input.chunks[0].bytes = decoded;
        auto bytes            = buildEvpack(std::move(input));
        REQUIRE(bytes.ok());
        auto parsed = parseEvpack(bytes.value());
        REQUIRE(parsed.ok());
        const auto found = std::find_if(parsed.value().chunks().begin(), parsed.value().chunks().end(),
                                        [](const EvpackChunk& chunk) { return chunk.codec == EvpackCodec::Zstd; });
        REQUIRE(found != parsed.value().chunks().end());
        CHECK(found->storedSize < found->decodedSize);
        const auto index = static_cast<std::size_t>(found - parsed.value().chunks().begin());
        CHECK(!parsed.value().chunkBytes(index).ok());
        auto restored = parsed.value().decodeChunk(index, decoded.size());
        REQUIRE(restored.ok());
        CHECK_EQ(restored.value(), decoded);
        CHECK(!parsed.value().decodeChunk(index, decoded.size() - 1).ok());
    };
    SUB_CASE("EVPACK signature policy separates content integrity from trusted origin") {
        EvpackTrust trust{EvpackSignaturePolicy::RequireTrustedSignature, std::make_shared<TestVerifier>()};
        auto        unsignedBytes = buildEvpack(package());
        REQUIRE(unsignedBytes.ok());
        auto unsignedRejected = parseEvpack(unsignedBytes.value(), {}, trust);
        REQUIRE(!unsignedRejected.ok());
        CHECK(unsignedRejected.error()->code() == DiagnosticCode::PreconditionViolation);

        auto signedInput   = package();
        signedInput.signer = std::make_shared<TestSigner>();
        auto signedBytes   = buildEvpack(std::move(signedInput));
        REQUIRE(signedBytes.ok());
        CHECK(!parseEvpack(signedBytes.value()).ok());
        auto trusted = parseEvpack(signedBytes.value(), {}, trust);
        REQUIRE(trusted.ok());
        CHECK(trusted.value().hasTrustedSignature());
        REQUIRE(trusted.value().signature().has_value());
        CHECK_EQ(trusted.value().signature()->algorithm, std::string("test-digest"));
        auto source = std::make_shared<CountingRangeSource>(signedBytes.value());
        auto range  = prepareEvpackRangeMount(source, {}, trust);
        REQUIRE(range.ok());
        CHECK(range.value().index().hasTrustedSignature());
    };
}
}  // namespace test_asset_evpack
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_asset_runtime_definition)
#include "PrivateTestScope.h"
#include "asset/RuntimeDefinition.h"
namespace test_asset_runtime_definition {
using namespace eve;
using namespace eve::asset;
TEST_CASE("Runtime definition binary codec contract") {
    SUB_CASE("Runtime definition encoding is deterministic and preserves all Value kinds") {
        Value::Object values;
        values["array"] =
            Value(Value::Array{Value(), Value(false), Value(std::int64_t(-7)), Value(1.25), Value("text")});
        values["enabled"] = Value(true);
        values["nested"]  = Value(Value::Object{{"z", Value(std::int64_t(9))}});
        auto a            = encodeRuntimeDefinition(Value(values));
        auto b            = encodeRuntimeDefinition(Value(values));
        REQUIRE(a.ok());
        REQUIRE(b.ok());
        CHECK_EQ(a.value(), b.value());
        CHECK_EQ(std::string(reinterpret_cast<const char*>(a.value().data()), 5), std::string("EVDEF"));
        auto decoded = decodeRuntimeDefinition(a.value());
        REQUIRE(decoded.ok());
        auto actualJson   = decoded.value().toJson();
        auto expectedJson = Value(std::move(values)).toJson();
        REQUIRE(actualJson.ok());
        REQUIRE(expectedJson.ok());
        CHECK_EQ(actualJson.value(), expectedJson.value());
    };
    SUB_CASE("Runtime definition decoder rejects unknown tag trailing payload invalid UTF-8 and budget overflow") {
        auto encoded = encodeRuntimeDefinition(Value(Value::Object{{"key", Value("value")}}));
        REQUIRE(encoded.ok());
        auto unknown = encoded.value();
        unknown[8]   = 0xff;
        auto badTag  = decodeRuntimeDefinition(unknown);
        REQUIRE(!badTag.ok());
        CHECK(badTag.error()->code() == DiagnosticCode::ParseError);
        auto trailing = encoded.value();
        trailing.push_back(0);
        CHECK(!decodeRuntimeDefinition(trailing).ok());
        RuntimeDefinitionLimits limits;
        limits.maximumBytes = encoded.value().size() - 1;
        CHECK(!decodeRuntimeDefinition(encoded.value(), limits).ok());
        limits                    = {};
        limits.maximumStringBytes = 3;
        CHECK(!decodeRuntimeDefinition(encoded.value(), limits).ok());
        auto invalidUtf8 = encodeRuntimeDefinition(Value("x"));
        REQUIRE(invalidUtf8.ok());
        invalidUtf8.value().back() = 0xff;
        CHECK(!decodeRuntimeDefinition(invalidUtf8.value()).ok());
    };
}
}  // namespace test_asset_runtime_definition
#endif
