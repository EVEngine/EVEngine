#include "asset/Evpack.h"
#include "asset/EvpackRegistry.h"
#include "asset/EvpackRange.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>

using namespace eve;
using namespace eve::asset;

namespace {

PersistentId id(std::string_view text) {
    auto parsed = PersistentId::parse(text);
    REQUIRE(parsed.has_value());
    return *parsed;
}

EvpackVariant desktopVariant(std::string quality) {
    return {"windows", "x86_64", "vulkan", {"bc", "rgba8"}, "spirv-1.6",
            std::move(quality), {}};
}

EvpackBuild buildInput() {
    EvpackBuild build;
    build.packageId = id("018f6f22-2490-7ad2-bf58-4f1dbca31040");
    build.buildId   = id("018f6f22-2490-7ad2-bf58-4f1dbca31041");
    build.variants  = {desktopVariant("high"), desktopVariant("medium")};
    build.chunks = {
        {id("550e8400-e29b-41d4-a716-446655440001"), "eve.mesh", SchemaVersion(2), 0,
         EvpackChunkKind::Bulk, 0, EvpackCodec::None, 16, {}, {1, 2, 3, 4}},
        {id("550e8400-e29b-41d4-a716-446655440000"), "eve.material", SchemaVersion(1), 1,
         EvpackChunkKind::Definition, 0, EvpackCodec::None, 8,
         {id("550e8400-e29b-41d4-a716-446655440001")}, {'m', 'a', 't'}}
    };
    return build;
}

class CountingRangeSource final : public EvpackRangeSource {
public:
    explicit CountingRangeSource(std::vector<std::uint8_t> value) : bytes(std::move(value)) {}
    Result<std::uint64_t> size() const override {
        return Result<std::uint64_t>::success(bytes.size());
    }
    Result<std::vector<std::uint8_t>> read(std::uint64_t offset, std::uint64_t count) const override {
        ++reads;
        ranges.emplace_back(offset, count);
        if (offset > bytes.size() || count > bytes.size() - offset)
            return Result<std::vector<std::uint8_t>>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "test range outside source"));
        return Result<std::vector<std::uint8_t>>::success(
            {bytes.begin() + static_cast<std::size_t>(offset),
             bytes.begin() + static_cast<std::size_t>(offset + count)});
    }
    mutable std::size_t reads = 0;
    mutable std::vector<std::pair<std::uint64_t, std::uint64_t>> ranges;
    mutable std::vector<std::uint8_t> bytes;
};

class TestSigner final : public EvpackSigner {
public:
    Result<EvpackSignature> sign(std::span<const std::uint8_t, 32> digest) const override {
        EvpackSignature signature;
        signature.algorithm = "test-digest";
        signature.keyId.fill(0x2a);
        for (std::size_t index = 0; index < 64; ++index)
            signature.bytes[index] = digest[index % 32] ^ 0x5a;
        return Result<EvpackSignature>::success(std::move(signature));
    }
};

class TestVerifier final : public EvpackSignatureVerifier {
public:
    Result<void> verify(std::span<const std::uint8_t, 32> digest,
                        const EvpackSignature& signature) const override {
        if (signature.algorithm != "test-digest" ||
            !std::all_of(signature.keyId.begin(), signature.keyId.end(),
                         [](std::uint8_t byte) { return byte == 0x2a; }))
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::PreconditionViolation, "untrusted test key"));
        for (std::size_t index = 0; index < 64; ++index)
            if (signature.bytes[index] != (digest[index % 32] ^ 0x5a))
                return Result<void>::failure(Diagnostic::error(
                    DiagnosticCode::HashMismatch, "test signature mismatch"));
        return Result<void>::success();
    }
};

}  // namespace

TEST_CASE("asset.evpack.deterministicBuildAndRandomAccessRoundTrip") {
    auto firstInput = buildInput();
    auto secondInput = buildInput();
    std::reverse(secondInput.chunks.begin(), secondInput.chunks.end());
    auto first = buildEvpack(std::move(firstInput));
    auto second = buildEvpack(std::move(secondInput));
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    CHECK_EQ(first.value(), second.value());

    auto parsed = parseEvpack(first.value());
    REQUIRE(parsed.ok());
    CHECK_EQ(parsed.value().packageId(), id("018f6f22-2490-7ad2-bf58-4f1dbca31040"));
    REQUIRE_EQ(parsed.value().chunks().size(), std::size_t(2));
    CHECK_EQ(parsed.value().chunks().front().type, std::string("eve.material"));
    auto bytes = parsed.value().chunkBytes(0);
    REQUIRE(bytes.ok());
    CHECK_EQ(std::vector<std::uint8_t>(bytes.value().begin(), bytes.value().end()),
             std::vector<std::uint8_t>({'m', 'a', 't'}));
}

TEST_CASE("asset.evpack.rejectsHeaderAndChunkHashCorruption") {
    auto built = buildEvpack(buildInput());
    REQUIRE(built.ok());
    auto headerCorrupt = built.value();
    headerCorrupt[20] ^= 0x80;
    auto badHeader = parseEvpack(headerCorrupt);
    REQUIRE(!badHeader.ok());
    CHECK_EQ(badHeader.error()->code(), DiagnosticCode::HashMismatch);

    auto chunkCorrupt = std::move(built).takeValue();
    auto marker = std::find(chunkCorrupt.begin(), chunkCorrupt.end(), std::uint8_t('m'));
    REQUIRE(marker != chunkCorrupt.end());
    *marker ^= 1;
    auto badChunk = parseEvpack(chunkCorrupt);
    REQUIRE(!badChunk.ok());
}

TEST_CASE("asset.evpack.variantSelectionIsCapabilityBasedAndObservable") {
    auto built = buildEvpack(buildInput());
    REQUIRE(built.ok());
    auto parsed = parseEvpack(built.value());
    REQUIRE(parsed.ok());
    EvpackCapabilities medium{"windows", "x86_64", "vulkan", {"bc"}, {"spirv-1.6"}, {"medium"}, {}};
    auto selected = selectEvpackVariant(parsed.value(), medium);
    REQUIRE(selected.ok());
    CHECK_EQ(selected.value().index, std::uint32_t(1));
    CHECK(selected.value().usedFallback);

    medium.graphics = "webgpu";
    auto unsupported = selectEvpackVariant(parsed.value(), medium);
    REQUIRE(!unsupported.ok());
    CHECK_EQ(unsupported.error()->code(), DiagnosticCode::Unsupported);
}

TEST_CASE("asset.evpack.rejectsDuplicateChunkKeysAndBudgets") {
    auto duplicate = buildInput();
    duplicate.chunks.push_back(duplicate.chunks.front());
    auto conflict = buildEvpack(std::move(duplicate));
    REQUIRE(!conflict.ok());
    CHECK_EQ(conflict.error()->code(), DiagnosticCode::Conflict);

    EvpackLimits limits;
    limits.maximumChunkBytes = 2;
    auto tooLarge = buildEvpack(buildInput(), limits);
    REQUIRE(!tooLarge.ok());

    auto invalidText = buildInput();
    invalidText.variants.front().quality = std::string("\xc0\x80", 2);
    auto badUtf8 = buildEvpack(std::move(invalidText));
    REQUIRE(!badUtf8.ok());
    CHECK_EQ(badUtf8.error()->code(), DiagnosticCode::InvalidArgument);
}

TEST_CASE("asset.evpack.registry.prepareFailurePreservesOldGenerationAndReplacementStalesHandles") {
    auto oldBytes = buildEvpack(buildInput());
    REQUIRE(oldBytes.ok());
    auto oldCandidate = prepareEvpackMount(oldBytes.value());
    REQUIRE(oldCandidate.ok());
    EvpackRegistry registry;
    auto oldReceipt = registry.commit(std::move(oldCandidate).takeValue());
    REQUIRE(oldReceipt.ok());

    auto corrupt = oldBytes.value();
    corrupt[30] ^= 0x40;
    auto rejected = prepareEvpackMount(corrupt);
    REQUIRE(!rejected.ok());
    auto stillCurrent = registry.resolve(oldReceipt.value().handle);
    REQUIRE(stillCurrent.ok());

    auto replacementInput = buildInput();
    replacementInput.buildId = id("018f6f22-2490-7ad2-bf58-4f1dbca31042");
    replacementInput.chunks.front().bytes = {9, 8, 7};
    auto replacementBytes = buildEvpack(std::move(replacementInput));
    REQUIRE(replacementBytes.ok());
    auto replacementCandidate = prepareEvpackMount(replacementBytes.value());
    REQUIRE(replacementCandidate.ok());
    auto replacementReceipt = registry.commit(std::move(replacementCandidate).takeValue());
    REQUIRE(replacementReceipt.ok());
    CHECK(replacementReceipt.value().replacedExisting);

    auto stale = registry.resolve(oldReceipt.value().handle);
    REQUIRE(!stale.ok());
    CHECK_EQ(stale.error()->code(), DiagnosticCode::StaleHandle);
    auto current = registry.resolve(replacementReceipt.value().handle);
    REQUIRE(current.ok());
}

TEST_CASE("asset.evpack.registryDispatchesOutsideLockAndAllowsSelfCancellation") {
    EvpackRegistry registry;
    EvpackRegistrySubscription self;
    std::vector<EvpackRegistryEventKind> observed;
    auto subscribed = registry.subscribe([&](const EvpackRegistryEvent& event) -> Result<void> {
        observed.push_back(event.kind);
        return registry.unsubscribe(self);
    });
    REQUIRE(subscribed.ok());
    self = subscribed.value();
    auto failing = registry.subscribe([](const EvpackRegistryEvent&) -> Result<void> {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::CallbackFailure,
                                                       "injected subscriber failure"));
    });
    REQUIRE(failing.ok());

    auto firstBytes = buildEvpack(buildInput());
    REQUIRE(firstBytes.ok());
    auto firstCandidate = prepareEvpackMount(firstBytes.value());
    REQUIRE(firstCandidate.ok());
    auto first = registry.commit(std::move(firstCandidate).takeValue());
    REQUIRE(first.ok());
    REQUIRE_EQ(observed.size(), std::size_t(1));
    CHECK_EQ(static_cast<int>(observed.front()),
             static_cast<int>(EvpackRegistryEventKind::Mounted));
    REQUIRE_EQ(first.value().callbackDiagnostics.size(), std::size_t(1));
    CHECK_EQ(first.value().callbackDiagnostics.front().code(), DiagnosticCode::CallbackFailure);

    auto replacement = buildInput();
    replacement.buildId = id("018f6f22-2490-7ad2-bf58-4f1dbca31042");
    auto replacementBytes = buildEvpack(std::move(replacement));
    REQUIRE(replacementBytes.ok());
    auto replacementCandidate = prepareEvpackMount(replacementBytes.value());
    REQUIRE(replacementCandidate.ok());
    auto second = registry.commit(std::move(replacementCandidate).takeValue());
    REQUIRE(second.ok());
    CHECK_EQ(observed.size(), std::size_t(1));
    CHECK(second.value().replacedExisting);
    REQUIRE_EQ(second.value().callbackDiagnostics.size(), std::size_t(1));

    auto unmounted = registry.unmount(second.value().handle);
    REQUIRE(unmounted.ok());
    REQUIRE_EQ(unmounted.value().callbackDiagnostics.size(), std::size_t(1));
    auto cancelled = registry.unsubscribe(failing.value());
    REQUIRE(cancelled.ok());
}

TEST_CASE("asset.evpack.rangeMountReadsMetadataThenVerifiesOnlyRequestedChunk") {
    auto built = buildEvpack(buildInput());
    REQUIRE(built.ok());
    auto source = std::make_shared<CountingRangeSource>(built.value());
    auto mount = prepareEvpackRangeMount(source);
    REQUIRE(mount.ok());
    CHECK_EQ(source->reads, std::size_t(2));
    REQUIRE_EQ(mount.value().index().chunks().size(), std::size_t(2));
    auto invalidBorrow = mount.value().index().chunkBytes(0);
    REQUIRE(!invalidBorrow.ok());
    CHECK_EQ(invalidBorrow.error()->code(), DiagnosticCode::PreconditionViolation);

    auto chunk = mount.value().readChunk(0);
    REQUIRE(chunk.ok());
    CHECK_EQ(source->reads, std::size_t(3));
    CHECK_EQ(chunk.value(), std::vector<std::uint8_t>({'m', 'a', 't'}));

    const auto& second = mount.value().index().chunks()[1];
    source->bytes[static_cast<std::size_t>(second.offset)] ^= 0xff;
    auto corrupt = mount.value().readChunk(1);
    REQUIRE(!corrupt.ok());
    CHECK_EQ(corrupt.error()->code(), DiagnosticCode::HashMismatch);
}

TEST_CASE("asset.evpack.zstdIsDeterministicBoundedAndDecodedBeforeExposure") {
    auto input = buildInput();
    std::vector<std::uint8_t> decoded(64 * 1024, 0x5a);
    input.chunks.front().codec = EvpackCodec::Zstd;
    input.chunks.front().bytes = decoded;
    auto first = buildEvpack(input);
    auto second = buildEvpack(std::move(input));
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    CHECK_EQ(first.value(), second.value());

    auto parsed = parseEvpack(first.value());
    REQUIRE(parsed.ok());
    const auto compressed = std::find_if(parsed.value().chunks().begin(), parsed.value().chunks().end(),
                                         [](const EvpackChunk& chunk) {
                                             return chunk.codec == EvpackCodec::Zstd;
                                         });
    REQUIRE(compressed != parsed.value().chunks().end());
    CHECK(compressed->storedSize < compressed->decodedSize);
    const std::size_t index = static_cast<std::size_t>(compressed - parsed.value().chunks().begin());
    auto borrowed = parsed.value().chunkBytes(index);
    REQUIRE(!borrowed.ok());
    auto restored = parsed.value().decodeChunk(index, decoded.size());
    REQUIRE(restored.ok());
    CHECK_EQ(restored.value(), decoded);
    auto overBudget = parsed.value().decodeChunk(index, decoded.size() - 1);
    REQUIRE(!overBudget.ok());

    auto source = std::make_shared<CountingRangeSource>(first.value());
    auto mount = prepareEvpackRangeMount(source);
    REQUIRE(mount.ok());
    auto ranged = mount.value().readChunk(index);
    REQUIRE(ranged.ok());
    CHECK_EQ(ranged.value(), decoded);
}

TEST_CASE("asset.evpack.registryRequiresProviderBeforeConsumerCommit") {
    const auto externalId = id("550e8400-e29b-41d4-a716-446655440099");
    auto consumerInput = buildInput();
    consumerInput.chunks.front().dependencies.push_back(externalId);
    auto consumerBytes = buildEvpack(std::move(consumerInput));
    REQUIRE(consumerBytes.ok());
    auto consumer = prepareEvpackMount(consumerBytes.value());
    REQUIRE(consumer.ok());
    EvpackRegistry registry;
    auto absent = registry.commit(std::move(consumer).takeValue());
    REQUIRE(!absent.ok());
    CHECK_EQ(absent.error()->code(), DiagnosticCode::NotFound);

    EvpackBuild providerInput;
    providerInput.packageId = id("018f6f22-2490-7ad2-bf58-4f1dbca31090");
    providerInput.buildId = id("018f6f22-2490-7ad2-bf58-4f1dbca31091");
    providerInput.variants = {desktopVariant("high")};
    providerInput.chunks = {{externalId, "eve.image", SchemaVersion(2), 0,
                             EvpackChunkKind::Definition, 0, EvpackCodec::None, 8, {}, {'{', '}'}}};
    auto providerBytes = buildEvpack(std::move(providerInput));
    REQUIRE(providerBytes.ok());
    auto provider = prepareEvpackMount(providerBytes.value());
    REQUIRE(provider.ok());
    auto providerReceipt = registry.commit(std::move(provider).takeValue());
    REQUIRE(providerReceipt.ok());

    auto externalRef = AssetRef::fromId(externalId);
    REQUIRE(externalRef.ok());
    auto resolvedAsset = registry.resolveAsset(
        externalRef.value(), "eve.image/2",
        {"windows", "x86_64", "vulkan", {"bc"}, {"spirv-1.6"}, {"high"}, {}});
    REQUIRE(resolvedAsset.ok());
    auto externalPayload = registry.readAsset(resolvedAsset.value(), 1024);
    REQUIRE(externalPayload.ok());
    CHECK_EQ(externalPayload.value().asset, externalRef.value());

    auto retry = prepareEvpackMount(consumerBytes.value());
    REQUIRE(retry.ok());
    auto mounted = registry.commit(std::move(retry).takeValue());
    REQUIRE(mounted.ok());

    EvpackBuild incompatible;
    incompatible.packageId = id("018f6f22-2490-7ad2-bf58-4f1dbca31090");
    incompatible.buildId = id("018f6f22-2490-7ad2-bf58-4f1dbca31092");
    incompatible.variants = {desktopVariant("high")};
    incompatible.chunks = {{id("550e8400-e29b-41d4-a716-446655440098"), "eve.image",
                            SchemaVersion(2), 0, EvpackChunkKind::Definition, 0,
                            EvpackCodec::None, 8, {}, {'{', '}'}}};
    auto incompatibleBytes = buildEvpack(std::move(incompatible));
    REQUIRE(incompatibleBytes.ok());
    auto incompatibleMount = prepareEvpackMount(incompatibleBytes.value());
    REQUIRE(incompatibleMount.ok());
    auto rejectedReplacement = registry.commit(std::move(incompatibleMount).takeValue());
    REQUIRE(!rejectedReplacement.ok());
    CHECK_EQ(rejectedReplacement.error()->code(), DiagnosticCode::NotFound);
    REQUIRE(registry.resolve(providerReceipt.value().handle).ok());
}

TEST_CASE("asset.evpack.assetHandlesDetectProviderReplacementUnloadAndConflicts") {
    EvpackRegistry registry;
    auto initialBytes = buildEvpack(buildInput());
    REQUIRE(initialBytes.ok());
    auto initial = prepareEvpackMount(initialBytes.value());
    REQUIRE(initial.ok());
    auto mounted = registry.commit(std::move(initial).takeValue());
    REQUIRE(mounted.ok());
    auto asset = AssetRef::fromId(id("550e8400-e29b-41d4-a716-446655440001"));
    REQUIRE(asset.ok());
    const EvpackCapabilities capabilities{
        "windows", "x86_64", "vulkan", {"bc"}, {"spirv-1.6"}, {"high"}, {}};
    auto handle = registry.resolveAsset(asset.value(), "eve.mesh/2", capabilities);
    REQUIRE(handle.ok());

    auto replacement = buildInput();
    replacement.buildId = id("018f6f22-2490-7ad2-bf58-4f1dbca31042");
    auto replacementBytes = buildEvpack(std::move(replacement));
    REQUIRE(replacementBytes.ok());
    auto replacementMount = prepareEvpackMount(replacementBytes.value());
    REQUIRE(replacementMount.ok());
    auto replaced = registry.commit(std::move(replacementMount).takeValue());
    REQUIRE(replaced.ok());
    auto stale = registry.readAsset(handle.value(), 1024);
    REQUIRE(!stale.ok());
    CHECK_EQ(stale.error()->code(), DiagnosticCode::StaleHandle);

    auto current = registry.resolveAsset(asset.value(), "eve.mesh/2", capabilities);
    REQUIRE(current.ok());
    auto unmounted = registry.unmount(replaced.value().handle);
    REQUIRE(unmounted.ok());
    auto missing = registry.readAsset(current.value(), 1024);
    REQUIRE(!missing.ok());
    CHECK_EQ(missing.error()->code(), DiagnosticCode::NotFound);

    auto firstBytes = buildEvpack(buildInput());
    REQUIRE(firstBytes.ok());
    auto firstMount = prepareEvpackMount(firstBytes.value());
    REQUIRE(firstMount.ok());
    REQUIRE(registry.commit(std::move(firstMount).takeValue()).ok());
    auto duplicate = buildInput();
    duplicate.packageId = id("018f6f22-2490-7ad2-bf58-4f1dbca31090");
    duplicate.buildId = id("018f6f22-2490-7ad2-bf58-4f1dbca31091");
    auto duplicateBytes = buildEvpack(std::move(duplicate));
    REQUIRE(duplicateBytes.ok());
    auto duplicateMount = prepareEvpackMount(duplicateBytes.value());
    REQUIRE(duplicateMount.ok());
    auto conflict = registry.commit(std::move(duplicateMount).takeValue());
    REQUIRE(!conflict.ok());
    CHECK_EQ(conflict.error()->code(), DiagnosticCode::Conflict);
}

TEST_CASE("asset.evpack.signaturePolicySeparatesTrustFromContentIntegrity") {
    EvpackTrust required{EvpackSignaturePolicy::RequireTrustedSignature,
                         std::make_shared<TestVerifier>()};
    auto unsignedBytes = buildEvpack(buildInput());
    REQUIRE(unsignedBytes.ok());
    auto unsignedRejected = parseEvpack(unsignedBytes.value(), {}, required);
    REQUIRE(!unsignedRejected.ok());
    CHECK_EQ(unsignedRejected.error()->code(), DiagnosticCode::PreconditionViolation);

    auto signedInput = buildInput();
    signedInput.signer = std::make_shared<TestSigner>();
    auto signedBytes = buildEvpack(std::move(signedInput));
    REQUIRE(signedBytes.ok());
    auto noTrust = parseEvpack(signedBytes.value());
    REQUIRE(!noTrust.ok());
    CHECK_EQ(noTrust.error()->code(), DiagnosticCode::PreconditionViolation);
    auto trusted = parseEvpack(signedBytes.value(), {}, required);
    REQUIRE(trusted.ok());
    CHECK(trusted.value().hasTrustedSignature());
    REQUIRE(trusted.value().signature().has_value());
    CHECK_EQ(trusted.value().signature()->algorithm, std::string("test-digest"));

    auto rangeSource = std::make_shared<CountingRangeSource>(signedBytes.value());
    auto range = prepareEvpackRangeMount(rangeSource, {}, required);
    REQUIRE(range.ok());
    CHECK(range.value().index().hasTrustedSignature());

    auto corrupt = signedBytes.value();
    const std::array<std::uint8_t, 8> magic = {'E', 'V', 'S', 'I', 'G', 0, 1, 0};
    const auto block = std::search(corrupt.begin(), corrupt.end(), magic.begin(), magic.end());
    REQUIRE(block != corrupt.end());
    *(block + 56) ^= 1;
    auto rejected = parseEvpack(corrupt, {}, required);
    REQUIRE(!rejected.ok());
    CHECK_EQ(rejected.error()->code(), DiagnosticCode::HashMismatch);
}
