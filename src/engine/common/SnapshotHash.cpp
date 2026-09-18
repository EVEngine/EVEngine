/**
 * @file SnapshotHash.cpp
 * @brief The built-in snapshot hasher and the capability bridge for it.
 */

#include "common/SnapshotHash.h"

#include "common/Capability.h"
#include "common/Diagnostic.h"

#include <array>
#include <cstdint>
#include <utility>

namespace eve {
namespace {

/**
 * @brief The built-in digest: two FNV-1a style 64-bit lanes over the canonical input.
 *
 * Named `fnv1a64x2-noncrypto` and reported as `NonCryptographic` on purpose: it is fast,
 * deterministic and good enough to notice a corrupted or mismatched envelope, and it makes no
 * preimage or collision-resistance claim. A project that needs those properties registers a
 * cryptographic hasher instead, and the algorithm id travels with the data so the difference
 * stays visible.
 */
class BuiltinSnapshotContentHasher final : public ISnapshotContentHasher {
public:
    [[nodiscard]] std::string_view id() const noexcept override { return "fnv1a64x2-noncrypto"; }

    [[nodiscard]] ContentDigestKind digestKind() const noexcept override {
        return ContentDigestKind::NonCryptographic;
    }

    [[nodiscard]] Result<ContentId> hash(std::string_view canonicalInput) const override {
        std::uint64_t left  = 14695981039346656037ull;
        std::uint64_t right = 1099511628211ull;
        for (const unsigned char byte : canonicalInput) {
            left  = (left ^ byte) * 1099511628211ull;
            right = (right ^ (static_cast<std::uint64_t>(byte) + 0x9e3779b97f4a7c15ull)) * 14029467366897019727ull;
        }
        ContentId::Bytes bytes{};
        for (int index = 0; index < 8; ++index) {
            bytes[static_cast<std::size_t>(index)] =
                static_cast<std::uint8_t>(left >> static_cast<unsigned>(56 - index * 8));
            bytes[static_cast<std::size_t>(index + 8)] =
                static_cast<std::uint8_t>(right >> static_cast<unsigned>(56 - index * 8));
        }
        return Result<ContentId>::success(ContentId(bytes));
    }
};

/** @brief Process-wide built-in instance, kept alive for the whole run. */
std::shared_ptr<ISnapshotContentHasher>& builtinHolder() {
    static std::shared_ptr<ISnapshotContentHasher> value = std::make_shared<BuiltinSnapshotContentHasher>();
    return value;
}

/** @brief The hasher a provider call should use: the registered one, else the built-in. */
std::shared_ptr<ISnapshotContentHasher> activeHasher() {
    if (ISnapshotContentHasher* registered = registeredSnapshotContentHasher(); registered != nullptr) {
        // The provider callable only needs the object for the duration of the call; the
        // registering module owns it, so a non-owning alias is deliberate here.
        return std::shared_ptr<ISnapshotContentHasher>(registered, [](ISnapshotContentHasher*) {});
    }
    return builtinHolder();
}

}  // namespace

std::string_view contentDigestKindName(ContentDigestKind kind) noexcept {
    switch (kind) {
        case ContentDigestKind::NonCryptographic: return "non_cryptographic";
        case ContentDigestKind::Cryptographic: return "cryptographic";
    }
    return "non_cryptographic";
}

std::shared_ptr<ISnapshotContentHasher> builtinSnapshotContentHasher() { return builtinHolder(); }

ISnapshotContentHasher* registeredSnapshotContentHasher() noexcept {
    return cap::query<ISnapshotContentHasher>();
}

Result<void> registerBuiltinSnapshotHasher() {
    if (registeredSnapshotContentHasher() != nullptr)
        return Result<void>::success(Status::success(StatusCode::NoOp));
    cap::provide<ISnapshotContentHasher>(builtinHolder().get());
    return Result<void>::success(Status::success(StatusCode::Applied));
}

SnapshotHashProvider snapshotContentHashProvider() {
    std::shared_ptr<ISnapshotContentHasher> hasher = activeHasher();
    return [hasher](std::string_view canonicalInput) -> Result<ContentId> { return hasher->hash(canonicalInput); };
}

std::string_view activeSnapshotHashAlgorithm() noexcept {
    const ISnapshotContentHasher* hasher = registeredSnapshotContentHasher();
    return hasher != nullptr ? hasher->id() : builtinHolder()->id();
}

}  // namespace eve
