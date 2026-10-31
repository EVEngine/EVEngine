// clang-format off
/* eve-test-policy:
{"schema":"evengine.test-policy","version":1,"default":"local-logic","module":"procgen"}
*/
// clang-format on
#include <queue>
#include <vector>
#include "procgen/GeneratorRegistry.h"
#include "procgen/Semantic.h"
#include "zeroerr/unittest.h"

TEST_CASE("procgen.roguelike.bothLBendsConnectAllRooms") {
    using namespace eve::procgen;
    auto& registry = GeneratorRegistry::instance();
    registry.registerBuiltins();
    for (unsigned seed : {1u, 7u, 42u, 43u, 435u, 436u, 991u}) {
        Params params;
        params.setSeed(seed);
        params.setSize(25, 19);
        params.setInt("roomCount", 8);
        params.setInt("roomMin", 4);
        params.setInt("roomMax", 7);
        params.setString("layoutStyle", "clustered");
        params.setString("connectionStyle", "nearest");
        params.setString("corridorStyle", "l");
        Grid2D      grid;
        std::string error;
        REQUIRE(registry.generate("level.roguelike", params, grid, error));
        const int         width  = grid.getWidth();
        const int         height = grid.getHeight();
        std::vector<bool> visited(width * height, false);
        std::queue<int>   pending;
        int               total = 0;
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
                const int cell = grid.getCell(x, y);
                if (cell != int(Semantic::Floor) && cell != int(Semantic::Corridor)) continue;
                if (total++ == 0) {
                    pending.push(y * width + x);
                    visited[y * width + x] = true;
                }
            }
        REQUIRE_GT(total, 0);
        int reached = 0;
        while (!pending.empty()) {
            const int cell = pending.front();
            pending.pop();
            ++reached;
            constexpr int dx[] = {-1, 1, 0, 0};
            constexpr int dy[] = {0, 0, -1, 1};
            for (int direction = 0; direction < 4; ++direction) {
                const int x = cell % width + dx[direction], y = cell / width + dy[direction];
                if (x < 0 || x >= width || y < 0 || y >= height) continue;
                const int next = y * width + x;
                const int kind = grid.getCell(x, y);
                if (!visited[next] && (kind == int(Semantic::Floor) || kind == int(Semantic::Corridor))) {
                    visited[next] = true;
                    pending.push(next);
                }
            }
        }
        CHECK_EQ(reached, total);
    }
}
