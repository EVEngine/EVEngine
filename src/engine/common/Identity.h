#pragma once

/**
 * @file Identity.h
 * @brief Strong identity values shared by persistence, content and logical-name APIs.
 *
 * The types in this file deliberately do not replace runtime handles.  A
 * PersistentId or ContentId can cross a process boundary, while an ECS or
 * registry handle remains local to its owner and generation domain.
 */

#include "common/Export.h"

#include <array>
#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace eve {

namespace detail {

struct PersistentIdTag {};
struct ContentIdTag {};
struct AssetGuidTag {};
struct DocumentIdTag {};
struct SceneObjectIdTag {};
struct ArtifactIdTag {};
struct EventIdTag {};
struct CommandIdTag {};
struct TransactionIdTag {};
struct OperationIdTag {};
struct EffectIdTag {};

template <typename Tag>
class Id128 {
public:
    using Bytes = std::array<std::uint8_t, 16>;

    /** @brief Constructs the nil value. */
    constexpr Id128() noexcept = default;

    /**
     * @brief Constructs an ID from its exact 128-bit representation.
     * @param bytes The bytes in UUID network order (most significant byte first).
     */
    explicit constexpr Id128(Bytes bytes) noexcept : bytes_(bytes) {}

    /**
     * @brief Re-tags an existing UUID at an explicit domain boundary.
     * @tparam OtherTag The source UUID domain.
     * @param value The source UUID; its bytes are copied without reinterpretation.
     * @return A UUID with the target domain tag.
     * @remarks This is intentionally explicit. Callers must document why a
     *          UUID changes semantic domain; ordinary assignment cannot mix
     *          identities such as an asset and a scene object.
     */
    template <typename OtherTag>
    [[nodiscard]] static constexpr Id128 fromUuid(const Id128<OtherTag>& value) noexcept {
        return Id128(value.bytes());
    }

    /** @brief Returns the all-zero nil value. */
    [[nodiscard]] static constexpr Id128 nil() noexcept { return {}; }

    /**
     * @brief Parses canonical UUID text.
     * @param text A 36-character UUID with 8-4-4-4-12 hexadecimal groups.
     * @return The parsed value, or empty when the text is malformed.
     * @remarks Parsing accepts upper- and lower-case hexadecimal digits but
     *          does not accept braces or non-canonical separators.
     */
    [[nodiscard]] static std::optional<Id128> parse(std::string_view text) noexcept {
        if (text.size() != 36 || text[8] != '-' || text[13] != '-' || text[18] != '-' || text[23] != '-') {
            return std::nullopt;
        }

        Bytes       bytes{};
        std::size_t byteIndex = 0;
        for (std::size_t i = 0; i < text.size();) {
            if (text[i] == '-') {
                ++i;
                continue;
            }

            if (i + 1 >= text.size()) return std::nullopt;
            const auto high = hexValue(text[i]);
            const auto low  = hexValue(text[i + 1]);
            if (!high || !low || byteIndex >= bytes.size()) {
                return std::nullopt;
            }
            bytes[byteIndex++] = static_cast<std::uint8_t>((*high << 4u) | *low);
            i += 2;
        }
        if (byteIndex != bytes.size()) return std::nullopt;
        return Id128(bytes);
    }

    /**
     * @brief Constructs an ID from exactly 16 bytes.
     * @param bytes The bytes in UUID network order.
     * @return The ID, or empty when the span is not exactly 16 bytes.
     */
    [[nodiscard]] static std::optional<Id128> fromBytes(std::span<const std::uint8_t> bytes) noexcept {
        if (bytes.size() != 16) return std::nullopt;
        Bytes result{};
        for (std::size_t i = 0; i < result.size(); ++i) result[i] = bytes[i];
        return Id128(result);
    }

    /**
     * @brief Formats the ID in lower-case canonical UUID text.
     * @return A stable 8-4-4-4-12 textual representation.
     */
    [[nodiscard]] std::string format() const {
        static constexpr char digits[] = "0123456789abcdef";
        std::string           result;
        result.reserve(36);
        for (std::size_t i = 0; i < bytes_.size(); ++i) {
            if (i == 4 || i == 6 || i == 8 || i == 10) result.push_back('-');
            result.push_back(digits[(bytes_[i] >> 4u) & 0x0fu]);
            result.push_back(digits[bytes_[i] & 0x0fu]);
        }
        return result;
    }

    /** @brief Returns whether this value is the all-zero nil ID. */
    [[nodiscard]] constexpr bool isNil() const noexcept {
        for (const auto byte : bytes_) {
            if (byte != 0) return false;
        }
        return true;
    }

    /**
     * @brief Derives a deterministic child UUID for a hierarchical identity.
     * @param role Non-empty, caller-canonicalized role or child key.
     * @return A stable child identity, or nil for a nil parent or empty role.
     * @remarks This is a namespace derivation operation. It is not a source of
     *          new instance entropy; use UUIDv7 generation for top-level
     *          identities. The operation is kept in the common UUID type so
     *          compatibility aliases cannot grow a second identity algorithm.
     */
    [[nodiscard]] Id128 child(std::string_view role) const noexcept {
        if (isNil() || role.empty()) return Id128::nil();

        Bytes         result = bytes_;
        std::uint64_t hash   = 14695981039346656037ull;
        for (const unsigned char byte : role) {
            hash ^= byte;
            hash *= 1099511628211ull;
        }
        for (std::size_t i = 0; i < sizeof(hash); ++i) {
            result[8 + i] ^= static_cast<std::uint8_t>((hash >> (i * 8u)) & 0xffu);
        }
        // Preserve the UUID variant while retaining the parent's namespace.
        result[8] = static_cast<std::uint8_t>((result[8] & 0x3fu) | 0x80u);
        return Id128(result);
    }

    /**
     * @brief Returns the UUID bytes in network order.
     * @return A copy of the owned 128-bit representation.
     */
    [[nodiscard]] constexpr Bytes bytes() const noexcept { return bytes_; }

    /**
     * @brief Returns a stable non-cryptographic hash for hash tables.
     * @return A deterministic 64-bit hash of all 16 bytes.
     * @remarks This is not a content digest and must not be used as a security
     *          primitive or as a replacement for ContentId.
     */
    [[nodiscard]] constexpr std::uint64_t hash() const noexcept {
        std::uint64_t result = 14695981039346656037ull;
        for (const auto byte : bytes_) {
            result ^= byte;
            result *= 1099511628211ull;
        }
        return result;
    }

    friend constexpr bool operator==(const Id128&, const Id128&) noexcept = default;

    /** @brief Orders UUID bytes in network order for ordered containers. */
    friend constexpr auto operator<=>(const Id128& lhs, const Id128& rhs) noexcept { return lhs.bytes_ <=> rhs.bytes_; }

private:
    [[nodiscard]] static constexpr std::optional<std::uint8_t> hexValue(char value) noexcept {
        if (value >= '0' && value <= '9') return static_cast<std::uint8_t>(value - '0');
        if (value >= 'a' && value <= 'f') return static_cast<std::uint8_t>(value - 'a' + 10);
        if (value >= 'A' && value <= 'F') return static_cast<std::uint8_t>(value - 'A' + 10);
        return std::nullopt;
    }

    Bytes bytes_{};
};

}  // namespace detail

/**
 * @brief Public alias for a tag-safe 128-bit UUID identity.
 * @tparam Tag Empty domain tag that prevents accidental cross-domain mixing.
 * @remarks All UUID aliases in the common layer use the same representation,
 *          parser, formatter, hash and child-derivation implementation.
 */
template <typename Tag>
using StrongUuid = detail::Id128<Tag>;

/**
 * @brief Stable instance identity for persistence, networking and process boundaries.
 *
 * PersistentId is a UUID value, not an ECS or registry handle.  It remains
 * meaningful after a runtime object is rebuilt, while the runtime handle may
 * change or become stale.
 */
using PersistentId = StrongUuid<detail::PersistentIdTag>;

/**
 * @brief Deterministic identity of equivalent canonical content.
 *
 * ContentId stores a caller-supplied 128-bit digest.  This common layer does
 * not silently choose a weak hash or pretend to provide cryptographic
 * security; the digest algorithm and canonicalization policy belong to the
 * content producer and must be part of its schema contract.
 */
using ContentId = StrongUuid<detail::ContentIdTag>;

/**
 * @brief Stable asset identity used by sidecars, package manifests and caches.
 *
 * This is a domain tag over the common UUID representation, not a second UUID
 * implementation. It is distinct from a URI, a content digest and every
 * runtime handle.
 */
using AssetGuid = StrongUuid<detail::AssetGuidTag>;

/** @brief Stable document identity for persisted/editor-facing documents. */
using DocumentId = StrongUuid<detail::DocumentIdTag>;

/** @brief Stable persisted scene-object identity; distinct from an ECS handle. */
using SceneObjectId = StrongUuid<detail::SceneObjectIdTag>;

/** @brief Stable generated-artifact identity; content equivalence remains ContentId. */
using ArtifactId = StrongUuid<detail::ArtifactIdTag>;

/** @brief Stable event identity for event envelopes and causal references. */
using EventId = StrongUuid<detail::EventIdTag>;

/** @brief Stable command identity for causal references. */
using CommandId = StrongUuid<detail::CommandIdTag>;

/** @brief Stable transaction identity for cross-module coordination and snapshots. */
using TransactionId = StrongUuid<detail::TransactionIdTag>;

/** @brief Stable operation identity inside a persisted transaction plan. */
using OperationId = StrongUuid<detail::OperationIdTag>;

/** @brief Stable effect-instance identity when an effect crosses a persistence/event boundary. */
using EffectId = StrongUuid<detail::EffectIdTag>;

/**
 * @brief UUID-backed identifier adapter for legacy textual boundaries.
 * @tparam Tag Domain tag for the canonical UUID projection.
 *
 * New values are represented by a common strong UUID. A non-UUID, non-empty
 * constructor or parse input is retained verbatim as a compatibility spelling
 * and receives a deterministic UUID projection for hashing, ordering and
 * explicit cross-boundary conversion. This lets old editor logical IDs keep
 * working without introducing another string-identity implementation.
 *
 * The legacy spelling is not a persistent UUID. Persistence code must use
 * `uuid()`/`canonicalFormat()` and declare its schema migration policy.
 */
template <typename Tag>
class UuidIdAdapter {
public:
    using Uuid  = StrongUuid<Tag>;
    using Bytes = typename Uuid::Bytes;

    /** @brief Constructs an empty compatibility identifier. */
    UuidIdAdapter() = default;

    /** @brief Constructs from a legacy spelling or canonical UUID text. */
    explicit UuidIdAdapter(const char* value) : UuidIdAdapter(std::string_view(value ? value : "")) {}

    /** @brief Constructs from a legacy spelling or canonical UUID text. */
    explicit UuidIdAdapter(std::string value) : UuidIdAdapter(std::string_view(value)) {}

    /** @brief Constructs from a legacy spelling or canonical UUID text. */
    explicit UuidIdAdapter(std::string_view value) { assign(value); }

    /**
     * @brief Parses canonical UUID text or a non-empty legacy spelling.
     * @param value Input from an editor or compatibility boundary.
     * @return Empty only for an empty input; legacy text is deliberately accepted.
     */
    [[nodiscard]] static std::optional<UuidIdAdapter> parse(std::string_view value) {
        if (value.empty()) return std::nullopt;
        return UuidIdAdapter(value);
    }

    /** @brief Builds an adapter from an explicit canonical UUID. */
    [[nodiscard]] static UuidIdAdapter fromUuid(Uuid value) {
        UuidIdAdapter result;
        result.uuid_      = value;
        result.value_     = value.format();
        result.canonical_ = true;
        return result;
    }

    /** @brief Builds an adapter while explicitly retaining a legacy spelling. */
    [[nodiscard]] static UuidIdAdapter fromLegacy(std::string value) {
        if (value.empty()) return {};
        const Uuid projected = projectLegacy(value);
        return UuidIdAdapter(std::move(value), projected, false);
    }

    /** @brief Returns the exact legacy or canonical compatibility spelling. */
    [[nodiscard]] const std::string& value() const noexcept { return value_; }

    /** @brief Returns the compatibility spelling; use canonicalFormat at persistence boundaries. */
    [[nodiscard]] const std::string& format() const noexcept { return value_; }

    /** @brief Returns the canonical UUID projection. */
    [[nodiscard]] Uuid uuid() const noexcept { return uuid_; }

    /** @brief Returns the canonical UUID projection as lower-case UUID text. */
    [[nodiscard]] std::string canonicalFormat() const { return uuid_.format(); }

    /** @brief True when no compatibility spelling has been assigned. */
    [[nodiscard]] bool empty() const noexcept { return value_.empty(); }

    /** @brief True when the value is an empty or UUID nil identity. */
    [[nodiscard]] bool isNil() const noexcept { return value_.empty() || uuid_.isNil(); }

    /** @brief True when the input was canonical UUID text rather than legacy text. */
    [[nodiscard]] bool isCanonicalUuid() const noexcept { return canonical_; }

    /** @brief Stable hash of the canonical UUID projection. */
    [[nodiscard]] std::uint64_t hash() const noexcept { return uuid_.hash(); }

    /** @brief Explicit boolean check for a non-empty identifier. */
    explicit operator bool() const noexcept { return !empty(); }

    friend bool operator==(const UuidIdAdapter& lhs, const UuidIdAdapter& rhs) noexcept {
        return lhs.uuid_ == rhs.uuid_;
    }

    /** @brief Orders adapter values by their canonical UUID projection. */
    friend auto operator<=>(const UuidIdAdapter& lhs, const UuidIdAdapter& rhs) noexcept {
        return lhs.uuid_ <=> rhs.uuid_;
    }

private:
    UuidIdAdapter(std::string value, Uuid uuid, bool canonical)
        : value_(std::move(value)), uuid_(uuid), canonical_(canonical) {}

    void assign(std::string_view value) {
        if (value.empty()) return;
        if (const auto parsed = Uuid::parse(value)) {
            uuid_      = *parsed;
            value_     = parsed->format();
            canonical_ = true;
            return;
        }
        value_     = value;
        uuid_      = projectLegacy(value);
        canonical_ = false;
    }

    [[nodiscard]] static Uuid projectLegacy(std::string_view value) noexcept {
        std::uint64_t first  = 14695981039346656037ull;
        std::uint64_t second = 1099511628211ull;
        for (const unsigned char byte : value) {
            first ^= byte;
            first *= 1099511628211ull;
            second ^= static_cast<std::uint8_t>(byte + 0x9du);
            second *= 14029467366897019727ull;
        }

        Bytes bytes{};
        for (std::size_t i = 0; i < sizeof(first); ++i) {
            bytes[i]     = static_cast<std::uint8_t>((first >> (i * 8u)) & 0xffu);
            bytes[8 + i] = static_cast<std::uint8_t>((second >> (i * 8u)) & 0xffu);
        }
        // Mark the deterministic projection as UUIDv5-shaped and RFC variant.
        bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0fu) | 0x50u);
        bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3fu) | 0x80u);
        return Uuid(bytes);
    }

    std::string value_;
    Uuid        uuid_;
    bool        canonical_ = false;
};

/**
 * @brief Human-readable, scoped identifier in the form `namespace:name`.
 *
 * LogicalId is not a persistent instance identity.  It names a definition,
 * recipe, policy or action and may be reused by many runtime instances. The
 * namespace is canonical lower-case ASCII (`[a-z][a-z0-9_.-]*`); the name is
 * case-sensitive ASCII (`[A-Za-z0-9][A-Za-z0-9_.-]*`). This prevents modules
 * from inventing incompatible namespace spellings at a persistence boundary.
 */
class EVENGINE_API LogicalId {
public:
    /** @brief Constructs an empty, invalid logical ID. */
    LogicalId() = default;

    /**
     * @brief Parses a scoped logical name.
     * @param text A name matching `[a-z][a-z0-9_.-]*:` followed by
     *             `[A-Za-z0-9][A-Za-z0-9_.-]*`.
     * @return The logical ID, or empty when the namespace/name grammar is invalid.
     */
    [[nodiscard]] static std::optional<LogicalId> parse(std::string_view text);

    /**
     * @brief Builds a logical ID from separate namespace and name components.
     * @param namespaceName The stable owner namespace.
     * @param name The name within that namespace.
     * @return The logical ID, or empty when either component is invalid.
     */
    [[nodiscard]] static std::optional<LogicalId> fromParts(std::string_view namespaceName, std::string_view name);

    /** @brief Returns whether this value contains a valid namespace and name. */
    [[nodiscard]] bool isValid() const noexcept { return !value_.empty(); }

    /** @brief Returns the canonical `namespace:name` representation. */
    [[nodiscard]] const std::string& format() const noexcept { return value_; }

    /** @brief Returns the namespace component as a view into this object. */
    [[nodiscard]] std::string_view namespaceName() const noexcept;

    /** @brief Returns the name component as a view into this object. */
    [[nodiscard]] std::string_view name() const noexcept;

    /**
     * @brief Returns a stable non-cryptographic hash for hash tables.
     * @return A deterministic 64-bit hash of the canonical text.
     */
    [[nodiscard]] std::uint64_t hash() const noexcept;

    friend bool operator==(const LogicalId&, const LogicalId&) noexcept = default;

private:
    explicit LogicalId(std::string value) : value_(std::move(value)) {}

    std::string value_;
};

/**
 * @brief Entropy callback used by UUIDv7 generation.
 * @param bytes A writable span that the callback must fill completely on success.
 * @return true only when all requested bytes were filled with suitable entropy.
 * @remarks Callers must provide a seed; no implicit pseudo-random source or
 *          `random_device` path is used.
 *          Security-sensitive callers must inject a cryptographically secure source.
 */
using UuidEntropySource = std::function<bool(std::span<std::uint8_t> bytes)>;

/** @brief Clock callback used by the injectable UUIDv7 generator. */
using UuidClock = std::function<std::chrono::system_clock::time_point()>;

/**
 * @brief Injectable UUIDv7 generator.
 *
 * UUIDv7 contains a Unix-millisecond timestamp and random bits.  This class
 * assembles the value but does not provide entropy itself.  The caller owns
 * the security and availability properties of the injected entropy source.
 */
class EVENGINE_API UuidV7Generator {
public:
    /**
     * @brief Creates a generator with an injected entropy source and optional clock.
     * @param entropy Source for the UUID random portion; an empty source makes generation fail.
     * @param clock Source for the timestamp; an empty source uses system_clock at call time.
     */
    explicit UuidV7Generator(UuidEntropySource entropy, UuidClock clock = {});

    /**
     * @brief Generates a UUIDv7 using the injected clock.
     * @return A generated PersistentId, or empty when entropy/time is unavailable or out of range.
     */
    [[nodiscard]] std::optional<PersistentId> generate() const;

    /**
     * @brief Generates a UUIDv7 for an explicit timestamp.
     * @param timestamp Unix time used for the UUIDv7 millisecond field.
     * @return A generated PersistentId, or empty when entropy/time is unavailable or out of range.
     */
    [[nodiscard]] std::optional<PersistentId> generate(std::chrono::system_clock::time_point timestamp) const;

private:
    UuidEntropySource entropy_;
    UuidClock         clock_;
};

/**
 * @brief Constructs a UUIDv7 without owning a clock or generator object.
 * @param timestamp Unix time used for the UUIDv7 millisecond field.
 * @param entropy Source for the UUID random portion.
 * @return A generated PersistentId, or empty when entropy/time is unavailable or out of range.
 */
[[nodiscard]] EVENGINE_API std::optional<PersistentId> generateUuidV7(std::chrono::system_clock::time_point timestamp,
                                                                      const UuidEntropySource&              entropy);

}  // namespace eve

namespace std {

template <typename Tag>
struct hash<eve::detail::Id128<Tag>> {
    /** @brief Hashes a strong 128-bit identity for standard hash containers. */
    std::size_t operator()(const eve::detail::Id128<Tag>& value) const noexcept {
        return static_cast<std::size_t>(value.hash());
    }
};

template <typename Tag>
struct hash<eve::UuidIdAdapter<Tag>> {
    /** @brief Hashes a compatibility-aware UUID adapter for standard hash containers. */
    std::size_t operator()(const eve::UuidIdAdapter<Tag>& value) const noexcept {
        return static_cast<std::size_t>(value.hash());
    }
};

template <>
struct hash<eve::LogicalId> {
    /** @brief Hashes a logical ID for standard hash containers. */
    std::size_t operator()(const eve::LogicalId& value) const noexcept {
        return static_cast<std::size_t>(value.hash());
    }
};

}  // namespace std
