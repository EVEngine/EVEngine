// eve-hot-path
#include "spawn/SpawnRandom.h"
namespace eve::spawn {
std::uint64_t nextRandomWord(std::uint64_t& state) noexcept {
    state += 0x9e3779b97f4a7c15ULL;
    auto value = state;
    value      = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value      = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}
double nextRandomDraw(std::uint64_t& state) noexcept {
    return static_cast<double>(nextRandomWord(state) >> 11U) * 0x1.0p-53;
}
}  // namespace eve::spawn
