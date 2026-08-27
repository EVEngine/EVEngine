#pragma once

/**
 * @file RuntimeHandle.h
 * @brief Strong generation-qualified handles for non-ECS runtime registries.
 *
 * RuntimeHandle is intentionally separate from ecs::EntityHandle.  It is a
 * compact process-local identity for slot maps owned by modules such as UI,
 * graphics, or physics; it is not a persistent ID and must not cross a
 * process or save boundary.
 */

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>

namespace eve {

/**
 * @brief Process-local slot identity tagged by the registry that owns it.
 *
 * A handle is current only while its owner has an occupied slot at
 * `index()` whose generation equals `generation()`. Generation zero and the
 * all-ones index are reserved for invalid values. Owners must start live
 * slots at generation one, increment the generation on destruction before a
 * slot is reused, and retire a slot instead of wrapping at the generation
 * limit. A handle is therefore stale after destruction, even if its slot is
 * later reused for another object.
 *
 * @tparam Tag An empty owner-specific tag; different tags are never
 *             implicitly interchangeable.
 */
template <typename Tag>
class RuntimeHandle {
public:
    /** @brief Unsigned owner-local slot index type. */
    using index_type      = std::uint32_t;
    /** @brief Unsigned owner-local generation type. */
    using generation_type = std::uint32_t;

    /** @brief Reserved index value shared by all invalid handles. */
    static constexpr index_type invalidIndex = std::numeric_limits<index_type>::max();
    /** @brief Reserved generation value shared by all invalid handles. */
    static constexpr generation_type invalidGeneration = 0;

    /** @brief Constructs an invalid handle. */
    constexpr RuntimeHandle() noexcept = default;

    /**
     * @brief Constructs a handle from its exact slot coordinates.
     * @param index Owner-local slot index.
     * @param generation Owner-local generation; zero is invalid.
     * @remarks This constructor does not prove that the owner currently has
     *          a live object at the coordinates. Resolution is the owner's
     *          responsibility.
     */
    explicit constexpr RuntimeHandle(index_type index, generation_type generation) noexcept
        : index_(index), generation_(generation) {}

    /** @brief Returns the canonical invalid handle. */
    [[nodiscard]] static constexpr RuntimeHandle invalid() noexcept { return {}; }

    /** @brief Returns whether both coordinates are usable handle values. */
    [[nodiscard]] constexpr bool isValid() const noexcept {
        return index_ != invalidIndex && generation_ != invalidGeneration;
    }

    /** @brief Returns whether this value is the invalid sentinel. */
    [[nodiscard]] constexpr bool isInvalid() const noexcept { return !isValid(); }

    /** @brief Returns the owner-local slot index. */
    [[nodiscard]] constexpr index_type index() const noexcept { return index_; }

    /** @brief Returns the owner-local replacement generation. */
    [[nodiscard]] constexpr generation_type generation() const noexcept { return generation_; }

    /**
     * @brief Returns the next generation without unsigned wraparound.
     * @param current The current slot generation.
     * @return The next generation, or empty when the slot must be retired.
     */
    [[nodiscard]] static constexpr std::optional<generation_type> nextGeneration(
        generation_type current) noexcept {
        if (current == std::numeric_limits<generation_type>::max()) return std::nullopt;
        return static_cast<generation_type>(current + 1u);
    }

    /**
     * @brief Returns this slot at its next generation.
     * @return A bumped handle, or empty for an invalid index or overflow.
     */
    [[nodiscard]] constexpr std::optional<RuntimeHandle> nextGeneration() const noexcept {
        const auto next = nextGeneration(generation_);
        if (!next || index_ == invalidIndex) return std::nullopt;
        return RuntimeHandle(index_, *next);
    }

    /**
     * @brief Encodes the two coordinates for a legacy integer boundary.
     * @return A lossless process-local packed representation.
     * @remarks This is an explicit projection only; it is not an integer
     *          conversion operator and must not be used as a persistent ID.
     */
    [[nodiscard]] constexpr std::uint64_t packed() const noexcept {
        return (static_cast<std::uint64_t>(generation_) << 32u) |
               static_cast<std::uint64_t>(index_);
    }

    /**
     * @brief Reconstructs a handle from an explicit packed boundary value.
     * @param value Value previously produced by packed().
     * @return The decoded handle; invalid coordinate values remain invalid.
     */
    [[nodiscard]] static constexpr RuntimeHandle fromPacked(std::uint64_t value) noexcept {
        return RuntimeHandle(static_cast<index_type>(value & 0xffffffffull),
                             static_cast<generation_type>(value >> 32u));
    }

    friend constexpr bool operator==(const RuntimeHandle&, const RuntimeHandle&) noexcept = default;
    friend constexpr auto operator<=>(const RuntimeHandle&, const RuntimeHandle&) noexcept = default;

private:
    index_type      index_      = invalidIndex;
    generation_type generation_ = invalidGeneration;
};

}  // namespace eve

namespace std {

/** @brief Hashes a tagged RuntimeHandle for unordered containers. */
template <typename Tag>
struct hash<eve::RuntimeHandle<Tag>> {
    std::size_t operator()(const eve::RuntimeHandle<Tag>& handle) const noexcept {
        return std::hash<std::uint64_t>{}(handle.packed());
    }
};

}  // namespace std
