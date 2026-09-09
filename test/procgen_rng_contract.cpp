#include <DTL/Shape/CellularAutomatonIsland.hpp>
#include <future>
#include "procgen/GeneratorRegistry.h"
#include "procgen/Semantic.h"
#include "zeroerr/unittest.h"

using namespace eve::procgen;

namespace {
bool sameCells(const Grid2D& a, const Grid2D& b) {
    if (a.getWidth() != b.getWidth() || a.getHeight() != b.getHeight()) return false;
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
            if (a.getCell(x, y) != b.getCell(x, y)) return false;
    return true;
}
}  // namespace

TEST_CASE("procgen.rng.cellularPreservesLegacySeedOutputs") {
    auto& registry = GeneratorRegistry::instance();
    registry.registerBuiltins();
    for (uint32_t seed : {0u, 1u, 42u, 1234567u}) {
        for (float fill : {0.f, 0.25f, 0.45f, 1.f}) {
            Params params;
            params.setSeed(seed);
            params.setSize(31, 24);
            params.setInt("loops", 5);
            params.setFloat("fill", fill);
            std::vector<std::vector<std::uint_fast8_t>> matrix(24, std::vector<std::uint_fast8_t>(31));
            // Test-only legacy oracle; production must not touch this RNG.
            DTL_RANDOM_ENGINE.seed(params.getSeed());
            DTL_RANDOM_ENGINE.clear();
            dtl::shape::CellularAutomatonIsland<std::uint_fast8_t> legacy(1, 0, 5, double(fill));
            REQUIRE(legacy.draw(matrix));
            Grid2D expected;
            expected.resize(31, 24);
            for (int y = 0; y < 24; ++y)
                for (int x = 0; x < 31; ++x)
                    expected.setCell(x, y, matrix[size_t(y)][size_t(x)] == 1 ? Semantic::Floor : Semantic::Wall);
            Grid2D      actual;
            std::string error;
            REQUIRE(registry.generate("cave.cellular", params, actual, error));
            CHECK(sameCells(expected, actual));
        }
    }
}

TEST_CASE("procgen.rng.cellularDoesNotConsumeImplicitStream") {
    auto& registry = GeneratorRegistry::instance();
    registry.registerBuiltins();
    DTL_RANDOM_ENGINE.seed(77u);
    DTL_RANDOM_ENGINE.clear();
    const auto expected = DTL_RANDOM_ENGINE.get();
    DTL_RANDOM_ENGINE.seed(77u);
    DTL_RANDOM_ENGINE.clear();
    Params params;
    params.setSeed(42);
    params.setSize(32, 24);
    Grid2D      actual;
    std::string error;
    REQUIRE(registry.generate("cave.cellular", params, actual, error));
    CHECK(DTL_RANDOM_ENGINE.get() == expected);
}

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
