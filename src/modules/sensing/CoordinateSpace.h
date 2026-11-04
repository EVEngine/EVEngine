#pragma once
#include <cstdint>
namespace eve::sensing {
/** @brief Coordinate spaces are not interchangeable, even when dimensions match. */
enum class CoordinateSpace : std::uint8_t {
    World2D,
    World3D,
    Grid2D,
    Grid3D,
};

}  // namespace eve::sensing
