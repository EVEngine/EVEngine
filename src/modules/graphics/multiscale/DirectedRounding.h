#pragma once
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>

namespace eve::graphics::multiscale::detail {
static_assert(sizeof(double) == sizeof(std::uint64_t) && std::numeric_limits<double>::is_iec559 &&
              std::numeric_limits<double>::digits == 53);
inline double standardNextafter(double value, double direction) {
    // Keep boundary cases on the library path: constant folding can preserve
    // the bits while dropping nextafter's floating-point exception flags.
    using Nextafter = double (*)(double, double);
    Nextafter volatile call = static_cast<Nextafter>(std::nextafter);
    return call(value, direction);
}
/** @brief Module-private next representable binary64 toward positive infinity.
 * @param value Represented scalar, including signed zero and nonfinite values.
 * @return Exactly the standard nextafter result. Boundary/subnormal/nonfinite inputs use the standard library,
 * retaining its exception behavior. Reentrant, no state or callbacks; constant arithmetic/storage.
 */
[[nodiscard]] inline double roundUp(double value) {
    const auto bits      = std::bit_cast<std::uint64_t>(value);
    const auto magnitude = bits & UINT64_C(0x7fffffffffffffff);
    if (magnitude > UINT64_C(0x0010000000000000) && magnitude < UINT64_C(0x7fefffffffffffff))
        return std::bit_cast<double>((bits >> 63) ? bits - 1 : bits + 1);
    return standardNextafter(value, std::numeric_limits<double>::infinity());
}
/** @brief Module-private next representable binary64 toward negative infinity.
 * @param value Represented scalar; all boundary cases retain standard-library handling.
 * @return Exactly nextafter(value, -infinity). Reentrant, constant arithmetic/storage, no state or callbacks.
 */
[[nodiscard]] inline double roundDown(double value) {
    const auto bits      = std::bit_cast<std::uint64_t>(value);
    const auto magnitude = bits & UINT64_C(0x7fffffffffffffff);
    if (magnitude > UINT64_C(0x0010000000000000) && magnitude < UINT64_C(0x7fefffffffffffff))
        return std::bit_cast<double>((bits >> 63) ? bits + 1 : bits - 1);
    return standardNextafter(value, -std::numeric_limits<double>::infinity());
}
}  // namespace eve::graphics::multiscale::detail
