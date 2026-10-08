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

namespace detail {

/**
 * @brief Tag-free slot coordinates behind every `RuntimeHandle<Tag>`.
 *
 * Validity, generation bumping without unsigned wraparound and the packed
 * projection are identical for every owner tag, so they are defined once here.
 * `RuntimeHandle<Tag>` keeps the tag purely as a phantom type and inherits
 * nothing, so comparing handles of different tags still does not compile.
 */
struct HandleCoordinates {
    /** @brief Unsigned owner-local slot index type. */
    using index_type = std::uint32_t;
    /** @brief Unsigned owner-local generation type. */
    using generation_type = std::uint32_t;

    /** @brief Reserved index value shared by all invalid handles. */
    static constexpr index_type invalidIndex = std::numeric_limits<index_type>::max();
    /** @brief Reserved generation value shared by all invalid handles. */
    static constexpr generation_type invalidGeneration = 0;

    index_type      index      = invalidIndex;
    generation_type generation = invalidGeneration;

    /** @brief Returns whether both coordinates are usable handle values. */
    [[nodiscard]] constexpr bool isValid() const noexcept {
        return index != invalidIndex && generation != invalidGeneration;
    }

    /** @brief Returns whether this value is the invalid sentinel. */
    [[nodiscard]] constexpr bool isInvalid() const noexcept { return !isValid(); }

    /**
     * @brief Returns the next generation without unsigned wraparound.
     * @param current The current slot generation.
     * @return The next generation, or empty when the slot must be retired.
     */
    [[nodiscard]] static constexpr std::optional<generation_type> nextGeneration(generation_type current) noexcept {
        if (current == std::numeric_limits<generation_type>::max()) return std::nullopt;
        return static_cast<generation_type>(current + 1u);
    }

    /**
     * @brief Returns this slot's next generation.
     * @return The bumped generation, or empty for an invalid index or overflow.
     */
    [[nodiscard]] constexpr std::optional<generation_type> bumpedGeneration() const noexcept {
        if (index == invalidIndex) return std::nullopt;
        return nextGeneration(generation);
    }

    /**
     * @brief Encodes the two coordinates for a legacy integer boundary.
     * @return A lossless process-local packed representation.
     */
    [[nodiscard]] constexpr std::uint64_t packed() const noexcept {
        return (static_cast<std::uint64_t>(generation) << 32u) | static_cast<std::uint64_t>(index);
    }

    /**
     * @brief Reconstructs coordinates from an explicit packed boundary value.
     * @param value Value previously produced by packed().
     * @return The decoded coordinates; invalid coordinate values remain invalid.
     */
    [[nodiscard]] static constexpr HandleCoordinates fromPacked(std::uint64_t value) noexcept {
        return HandleCoordinates{static_cast<index_type>(value & 0xffffffffull),
                                 static_cast<generation_type>(value >> 32u)};
    }

    friend constexpr bool operator==(const HandleCoordinates&, const HandleCoordinates&) noexcept  = default;
    friend constexpr auto operator<=>(const HandleCoordinates&, const HandleCoordinates&) noexcept = default;
};

}  // namespace detail

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
    using index_type = detail::HandleCoordinates::index_type;
    /** @brief Unsigned owner-local generation type. */
    using generation_type = detail::HandleCoordinates::generation_type;

    /** @brief Reserved index value shared by all invalid handles. */
    static constexpr index_type invalidIndex = detail::HandleCoordinates::invalidIndex;
    /** @brief Reserved generation value shared by all invalid handles. */
    static constexpr generation_type invalidGeneration = detail::HandleCoordinates::invalidGeneration;

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
        : coordinates_{index, generation} {}

    /** @brief Returns the canonical invalid handle. */
    [[nodiscard]] static constexpr RuntimeHandle invalid() noexcept { return {}; }

    /** @brief Returns whether both coordinates are usable handle values. */
    [[nodiscard]] constexpr bool isValid() const noexcept { return coordinates_.isValid(); }

    /** @brief Returns whether this value is the invalid sentinel. */
    [[nodiscard]] constexpr bool isInvalid() const noexcept { return coordinates_.isInvalid(); }

    /** @brief Returns the owner-local slot index. */
    [[nodiscard]] constexpr index_type index() const noexcept { return coordinates_.index; }

    /** @brief Returns the owner-local replacement generation. */
    [[nodiscard]] constexpr generation_type generation() const noexcept { return coordinates_.generation; }

    /**
     * @brief Returns the next generation without unsigned wraparound.
     * @param current The current slot generation.
     * @return The next generation, or empty when the slot must be retired.
     */
    [[nodiscard]] static constexpr std::optional<generation_type> nextGeneration(generation_type current) noexcept {
        return detail::HandleCoordinates::nextGeneration(current);
    }

    /**
     * @brief Returns this slot at its next generation.
     * @return A bumped handle, or empty for an invalid index or overflow.
     */
    [[nodiscard]] constexpr std::optional<RuntimeHandle> nextGeneration() const noexcept {
        const auto next = coordinates_.bumpedGeneration();
        if (!next) return std::nullopt;
        return RuntimeHandle(coordinates_.index, *next);
    }

    /**
     * @brief Encodes the two coordinates for a legacy integer boundary.
     * @return A lossless process-local packed representation.
     * @remarks This is an explicit projection only; it is not an integer
     *          conversion operator and must not be used as a persistent ID.
     */
    [[nodiscard]] constexpr std::uint64_t packed() const noexcept { return coordinates_.packed(); }

    /**
     * @brief Reconstructs a handle from an explicit packed boundary value.
     * @param value Value previously produced by packed().
     * @return The decoded handle; invalid coordinate values remain invalid.
     */
    [[nodiscard]] static constexpr RuntimeHandle fromPacked(std::uint64_t value) noexcept {
        const auto coordinates = detail::HandleCoordinates::fromPacked(value);
        return RuntimeHandle(coordinates.index, coordinates.generation);
    }

    friend constexpr bool operator==(const RuntimeHandle& left, const RuntimeHandle& right) noexcept {
        return left.coordinates_ == right.coordinates_;
    }
    friend constexpr auto operator<=>(const RuntimeHandle& left, const RuntimeHandle& right) noexcept {
        return left.coordinates_ <=> right.coordinates_;
    }

private:
    detail::HandleCoordinates coordinates_;
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
