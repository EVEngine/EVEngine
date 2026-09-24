#pragma once

/**
 * @file SnapshotHash.h
 * @brief Injectable content-digest provider for snapshots, with an honest default.
 *
 * `SnapshotEnvelope::contentHash` is an **integrity and identity** check: it detects a
 * corrupted or mismatched envelope. It is not a security boundary, and the engine must not
 * pretend otherwise, so the algorithm is a named, replaceable provider rather than a hidden
 * default:
 *
 *  - `ISnapshotContentHasher` is a capability. A project that needs a cryptographic digest
 *    registers its own implementation.
 *  - The engine ships one built-in hasher whose **name says what it is**
 *    (`fnv1a64x2-noncrypto`) and whose `digestKind()` reports `NonCryptographic`, so a caller
 *    can tell from the data which guarantee it actually has.
 *  - `snapshotContentHashProvider()` is the bridge used by script and tooling paths that
 *    cannot inject a `std::function`: it uses the registered hasher, and otherwise the
 *    built-in one. That substitution is stated rather than silent, and
 *    `activeSnapshotHashAlgorithm()` lets a caller **observe** which algorithm is in effect
 *    and record it next to the data it produced.
 *
 * The digest input is always the canonical serialization assembled by `common/Snapshot.*`;
 * a hasher never sees ECS state and never defines what is hashed.
 */

#include "common/Identity.h"
#include "common/Result.h"
#include "common/Snapshot.h"

#include <memory>
#include <string>
#include <string_view>

namespace eve {

/** @brief What a digest implementation actually guarantees, stated rather than implied. */
enum class ContentDigestKind : std::uint8_t {
    /** @brief Fast, deterministic, collision-resistant enough for storage identity only. */
    NonCryptographic,
    /** @brief A cryptographic digest; the provider documents its algorithm and properties. */
    Cryptographic,
};

/** @brief Stable protocol spelling of a digest kind. */
[[nodiscard]] EVENGINE_API_FOUNDATION std::string_view contentDigestKindName(ContentDigestKind kind) noexcept;

/**
 * @brief One content-digest implementation usable for snapshot sealing and verification.
 *
 * @remarks Implementations must be pure and deterministic: the same input must always produce
 *          the same id, or a snapshot could never be verified after a restart.
 */
class ISnapshotContentHasher {
public:
    /** @brief Capability name a provider registers under. */
    static constexpr const char* capabilityName = "eve.ISnapshotContentHasher";

    virtual ~ISnapshotContentHasher() = default;

    /** @brief Stable algorithm id, persisted or logged next to the data it hashed. */
    [[nodiscard]] virtual std::string_view id() const noexcept = 0;

    /** @brief What this digest guarantees. */
    [[nodiscard]] virtual ContentDigestKind digestKind() const noexcept = 0;

    /**
     * @brief Digest one canonical snapshot input.
     * @param canonicalInput Canonical JSON assembled by `common/Snapshot.*`.
     * @return A 128-bit content id, or a structured failure.
     */
    [[nodiscard]] virtual Result<ContentId> hash(std::string_view canonicalInput) const = 0;
};

/**
 * @brief The engine's built-in content hasher.
 * @return A process-wide immutable hasher whose id is `fnv1a64x2-noncrypto`.
 * @ownership Shared; the object is immutable and may be handed to `cap::provide`.
 */
[[nodiscard]] EVENGINE_API std::shared_ptr<ISnapshotContentHasher> builtinSnapshotContentHasher();

/**
 * @brief The currently registered hasher, if a module registered one.
 * @return Borrowed pointer to the registered implementation, or `nullptr` when none is.
 * @ownership Borrowed; the registering module owns the object and must outlive its use.
 * @nullable Yes.
 * @lifetime Valid until another module registers a replacement or revokes this one.
 * @thread Safe to call concurrently with queries; registration is not synchronized with use.
 */
[[nodiscard]] EVENGINE_API ISnapshotContentHasher* registeredSnapshotContentHasher() noexcept;

/**
 * @brief Register the built-in hasher, unless a module already registered one.
 * @return Applied when this call registered the built-in, or NoOp when a provider was already
 *         present (an existing, possibly stronger, provider is never replaced).
 * @remarks Host boot calls this once so script and tooling paths have a working default
 *          without every caller having to inject one.
 */
[[nodiscard]] EVENGINE_API Result<void> registerBuiltinSnapshotHasher();

/**
 * @brief A `SnapshotHashProvider` backed by the active hasher.
 * @return A callable suitable for the snapshot/restore entry points.
 * @ownership The returned callable borrows the active hasher; it must not outlive it, and it
 *            must not be stored beyond the registration's lifetime.
 * @remarks Uses the registered hasher when there is one, otherwise
 *          @ref builtinSnapshotContentHasher. Callers that must not accept a weak digest ask
 *          @ref activeSnapshotHashAlgorithm or @ref registeredSnapshotContentHasher first.
 */
[[nodiscard]] EVENGINE_API SnapshotHashProvider snapshotContentHashProvider();

/**
 * @brief Id of the algorithm `snapshotContentHashProvider` currently uses.
 * @return The active hasher's id, so a caller can record what produced a hash.
 */
[[nodiscard]] EVENGINE_API std::string_view activeSnapshotHashAlgorithm() noexcept;

}  // namespace eve
