#include <DTL/Shape/CellularAutomatonIsland.hpp>
#include <future>
#include "procgen/GeneratorRegistry.h"
#include "procgen/Semantic.h"
#include "zeroerr/unittest.h"

using namespace eve::procgen;

namespace {
[[maybe_unused]] bool sameCells(const Grid2D& a, const Grid2D& b) {
    if (a.getWidth() != b.getWidth() || a.getHeight() != b.getHeight()) return false;
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
            if (a.getCell(x, y) != b.getCell(x, y)) return false;
    return true;
}
}  // namespace


TEST_CASE("procgen.cave.cellular.parallelJobsHaveIsolatedRandomStreams") {
    GeneratorRegistry::instance().registerBuiltins();
    auto generate = [](uint32_t seed) {
        Params params;
        params.setSeed(seed);
        params.setSize(48, 36);
        params.setInt("loops", 5);
        params.setFloat("fill", 0.45f);
        Grid2D      grid;
        std::string error;
        const bool  generated = GeneratorRegistry::instance().generate("cave.cellular", params, grid, error);
        return std::make_pair(generated, std::move(grid));
    };
    const auto expectedA = generate(101);
    const auto expectedB = generate(202);
    REQUIRE(expectedA.first);
    REQUIRE(expectedB.first);

    auto       first   = std::async(std::launch::async, generate, uint32_t(101));
    auto       second  = std::async(std::launch::async, generate, uint32_t(202));
    const auto actualA = first.get();
    const auto actualB = second.get();
    REQUIRE(actualA.first);
    REQUIRE(actualB.first);
    CHECK(sameCells(expectedA.second, actualA.second));
    CHECK(sameCells(expectedB.second, actualB.second));
}
