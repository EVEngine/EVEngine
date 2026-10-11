#pragma once

#include "common/Result.h"
#include "procgen/Grid2D.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace eve::procgen::gridgraph {

struct GenerateSettings {
    int           width    = 32;
    int           height   = 32;
    int           semantic = 1;
    std::uint64_t seed     = 1;
    int           a        = 0;
    int           b        = 0;
    int           c        = 0;
    float         x        = 0.f;
    float         y        = 0.f;
};

[[nodiscard]] Result<Grid2D> generate(std::string_view operation, const GenerateSettings& settings);
[[nodiscard]] Result<Grid2D> select(const Grid2D& input, std::string_view operation, int mode, int count, float weight,
                                    std::uint64_t seed, std::string_view rule);
[[nodiscard]] Result<Grid2D> findPath(const Grid2D& navigation, const Grid2D& starts, const Grid2D& targets,
                                      int semantic);

}  // namespace eve::procgen::gridgraph
