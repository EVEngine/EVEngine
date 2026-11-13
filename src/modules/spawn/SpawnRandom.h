#pragma once
#include <cstdint>
#include "common/Export.h"
namespace eve::spawn {
/** @brief Advance an injected named stream using SplitMix64/v1; unsigned state wrap is intentional.
 * @ownership Mutates only the borrowed state during this call; no globals or retained references.
 * @thread Reentrant for distinct states; synchronize access to the same state. Constant cost. */
[[nodiscard]] EVENGINE_API_FOUNDATION std::uint64_t nextRandomWord(std::uint64_t& state) noexcept;
/** @brief Advance once and project the high 53 bits to a uniform draw in [0,1); never returns one.
 * @ownership Same state/algorithm contract as nextRandomWord; constant cost. */
[[nodiscard]] EVENGINE_API_FOUNDATION double nextRandomDraw(std::uint64_t& state) noexcept;
}  // namespace eve::spawn
