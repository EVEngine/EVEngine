#pragma once

/** @file StrongUint64.h @brief Internal storage primitive for domain-owned unsigned values. */

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>

namespace eve::detail {

template <typename Tag>
class StrongUint64 {
public:
    /** @brief Constructs the zero value. */
    constexpr StrongUint64() noexcept = default;
    /** @brief Constructs a value explicitly from its underlying integer. */
    explicit constexpr StrongUint64(std::uint64_t value) noexcept : value_(value) {}

    /** @brief Returns the zero value for this strong type. */
    [[nodiscard]] static constexpr StrongUint64 zero() noexcept { return {}; }
    /** @brief Returns the underlying value at an explicit protocol boundary. */
    [[nodiscard]] constexpr std::uint64_t value() const noexcept { return value_; }
    /** @brief Returns whether this value is zero. */
    [[nodiscard]] constexpr bool isZero() const noexcept { return value_ == 0; }
    /** @brief Returns the next value, or empty instead of unsigned wraparound. */
    [[nodiscard]] constexpr std::optional<StrongUint64> incremented() const noexcept {
        if (value_ == std::numeric_limits<std::uint64_t>::max()) return std::nullopt;
        return StrongUint64(value_ + 1);
    }

    friend constexpr bool operator==(const StrongUint64&, const StrongUint64&) noexcept = default;
    friend constexpr auto operator<=>(const StrongUint64&, const StrongUint64&) noexcept = default;

private:
    std::uint64_t value_ = 0;
};

}  // namespace eve::detail

namespace std {

template <typename Tag>
struct hash<eve::detail::StrongUint64<Tag>> {
    /** @brief Hashes a strong numeric value for standard hash containers. */
    std::size_t operator()(const eve::detail::StrongUint64<Tag>& value) const noexcept {
        return std::hash<std::uint64_t>{}(value.value());
    }
};

}  // namespace std
