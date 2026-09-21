#include "procgen/GridGraphAlgorithms.h"

#include "common/Diagnostic.h"
#include "procgen/Semantic.h"

#include <algorithm>
#include <cmath>
#include <queue>
#include <random>
#include <sstream>

namespace eve::procgen::gridgraph {
namespace {

bool occupied(const Grid2D& grid, int x, int y) { return grid.getCell(x, y) != int(Semantic::Empty); }

Grid2D makeGrid(const GenerateSettings& settings) {
    Grid2D grid;
    grid.resize(settings.width, settings.height);
    return grid;
}

void setOccupied(Grid2D& grid, int x, int y, int semantic) {
    if (x >= 0 && y >= 0 && x < grid.getWidth() && y < grid.getHeight()) grid.setCell(x, y, semantic);
}

std::vector<std::pair<int, int>> cells(const Grid2D& grid) {
    std::vector<std::pair<int, int>> result;
    for (int y = 0; y < grid.getHeight(); ++y)
        for (int x = 0; x < grid.getWidth(); ++x)
            if (occupied(grid, x, y)) result.emplace_back(x, y);
    return result;
}

std::vector<std::vector<std::pair<int, int>>> islands(const Grid2D& grid) {
    std::vector<std::vector<std::pair<int, int>>> result;
    std::vector<unsigned char>                    visited(std::size_t(grid.getWidth() * grid.getHeight()));
    constexpr int                                 dx[] = {-1, 1, 0, 0};
    constexpr int                                 dy[] = {0, 0, -1, 1};
    for (int y = 0; y < grid.getHeight(); ++y) {
        for (int x = 0; x < grid.getWidth(); ++x) {
            const int start = y * grid.getWidth() + x;
            if (!occupied(grid, x, y) || visited[std::size_t(start)]) continue;
            result.emplace_back();
            std::queue<std::pair<int, int>> pending;
            pending.emplace(x, y);
            visited[std::size_t(start)] = 1;
            while (!pending.empty()) {
                const auto current = pending.front();
                pending.pop();
                result.back().push_back(current);
                for (int direction = 0; direction < 4; ++direction) {
                    const int nx = current.first + dx[direction];
                    const int ny = current.second + dy[direction];
                    if (nx < 0 || ny < 0 || nx >= grid.getWidth() || ny >= grid.getHeight()) continue;
                    const int index = ny * grid.getWidth() + nx;
                    if (occupied(grid, nx, ny) && !visited[std::size_t(index)]) {
                        visited[std::size_t(index)] = 1;
                        pending.emplace(nx, ny);
                    }
                }
            }
        }
    }
    return result;
}

Grid2D cellular(const GenerateSettings& settings) {
    Grid2D                                grid = makeGrid(settings);
    std::mt19937_64                       rng(settings.seed);
    std::uniform_real_distribution<float> probability(0.f, 1.f);
    for (int y = 0; y < settings.height; ++y)
        for (int x = 0; x < settings.width; ++x)
            if (probability(rng) < settings.x) grid.setCell(x, y, settings.semantic);
    for (int step = 0; step < settings.a; ++step) {
        Grid2D next = grid;
        for (int y = 0; y < settings.height; ++y) {
            for (int x = 0; x < settings.width; ++x) {
                int neighbors = 0;
                for (int oy = -1; oy <= 1; ++oy)
                    for (int ox = -1; ox <= 1; ++ox)
                        if ((ox != 0 || oy != 0) && occupied(grid, x + ox, y + oy)) ++neighbors;
                next.setCell(
                    x, y,
                    neighbors > 4 ? settings.semantic : (neighbors < 4 ? int(Semantic::Empty) : grid.getCell(x, y)));
            }
        }
        grid = std::move(next);
    }
    return grid;
}

Grid2D randomWalk(const GenerateSettings& settings) {
    Grid2D                             grid = makeGrid(settings);
    std::mt19937_64                    rng(settings.seed);
    std::uniform_int_distribution<int> startX(0, settings.width - 1);
    std::uniform_int_distribution<int> startY(0, settings.height - 1);
    std::uniform_int_distribution<int> direction(0, 3);
    int           x          = settings.b != 0 ? std::clamp(settings.b, 0, settings.width - 1) : startX(rng);
    int           y          = settings.c != 0 ? std::clamp(settings.c, 0, settings.height - 1) : startY(rng);
    const int     iterations = std::max(1, settings.a);
    const int     length     = std::max(1, int(settings.x));
    constexpr int dx[]       = {-1, 1, 0, 0};
    constexpr int dy[]       = {0, 0, -1, 1};
    for (int iteration = 0; iteration < iterations; ++iteration) {
        for (int step = 0; step < length; ++step) {
            setOccupied(grid, x, y, settings.semantic);
            const int d = direction(rng);
            x           = std::clamp(x + dx[d], 0, settings.width - 1);
            y           = std::clamp(y + dy[d], 0, settings.height - 1);
        }
        if (settings.y > 0.5f) {
            x = startX(rng);
            y = startY(rng);
        }
    }
    return grid;
}

Grid2D maze(const GenerateSettings& settings) {
    Grid2D grid = makeGrid(settings);
    if (settings.width < 3 || settings.height < 3) return grid;
    std::mt19937_64                  rng(settings.seed);
    std::vector<std::pair<int, int>> stack{{1, 1}};
    setOccupied(grid, 1, 1, settings.semantic);
    constexpr int dx[] = {-2, 2, 0, 0};
    constexpr int dy[] = {0, 0, -2, 2};
    while (!stack.empty()) {
        const auto       current = stack.back();
        std::vector<int> candidates;
        for (int d = 0; d < 4; ++d) {
            const int nx = current.first + dx[d];
            const int ny = current.second + dy[d];
            if (nx > 0 && ny > 0 && nx < settings.width - 1 && ny < settings.height - 1 && !occupied(grid, nx, ny))
                candidates.push_back(d);
        }
        if (candidates.empty()) {
            stack.pop_back();
            continue;
        }
        std::shuffle(candidates.begin(), candidates.end(), rng);
        const int d  = candidates.front();
        const int nx = current.first + dx[d];
        const int ny = current.second + dy[d];
        setOccupied(grid, current.first + dx[d] / 2, current.second + dy[d] / 2, settings.semantic);
        setOccupied(grid, nx, ny, settings.semantic);
        stack.emplace_back(nx, ny);
    }
    return grid;
}

}  // namespace

Result<Grid2D> generate(std::string_view operation, const GenerateSettings& settings) {
    if (settings.width <= 0 || settings.height <= 0)
        return Result<Grid2D>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "generator dimensions must be positive", {}, {}, "procgen.gridGraph"));
    Grid2D          grid = makeGrid(settings);
    std::mt19937_64 rng(settings.seed);
    if (operation == "generate.fill") {
        grid.fill(settings.semantic);
    } else if (operation == "generate.random_noise") {
        std::uniform_real_distribution<float> probability(0.f, 1.f);
        for (int y = 0; y < settings.height; ++y)
            for (int x = 0; x < settings.width; ++x)
                if (probability(rng) < settings.x) grid.setCell(x, y, settings.semantic);
    } else if (operation == "generate.checkerboard" || operation == "generate.dot_grid") {
        const int spacing = std::max(1, settings.a);
        for (int y = 0; y < settings.height; ++y)
            for (int x = 0; x < settings.width; ++x)
                if (operation == "generate.checkerboard" ? ((x / spacing + y / spacing) % 2 == 0)
                                                         : (x % spacing == 0 && y % spacing == 0))
                    grid.setCell(x, y, settings.semantic);
    } else if (operation == "generate.shape") {
        const int cx     = settings.a;
        const int cy     = settings.b;
        const int radius = std::max(1, settings.c);
        for (int y = 0; y < settings.height; ++y)
            for (int x = 0; x < settings.width; ++x) {
                bool inside = settings.x < 0.5f ? ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= radius * radius)
                                                : (std::abs(x - cx) <= radius && std::abs(y - cy) <= radius);
                if (inside) grid.setCell(x, y, settings.semantic);
            }
    } else if (operation == "generate.cellular") {
        grid = cellular(settings);
    } else if (operation == "generate.random_walk") {
        grid = randomWalk(settings);
    } else if (operation == "generate.maze") {
        grid = maze(settings);
    } else if (operation == "generate.poisson") {
        const float                      radius = std::max(1.f, settings.x);
        std::vector<std::pair<int, int>> accepted;
        std::vector<std::pair<int, int>> candidates;
        for (int y = 0; y < settings.height; ++y)
            for (int x = 0; x < settings.width; ++x) candidates.emplace_back(x, y);
        std::shuffle(candidates.begin(), candidates.end(), rng);
        for (const auto& candidate : candidates) {
            const bool clear = std::none_of(accepted.begin(), accepted.end(), [&](const auto& point) {
                const float dx = float(point.first - candidate.first);
                const float dy = float(point.second - candidate.second);
                return dx * dx + dy * dy < radius * radius;
            });
            if (clear) {
                accepted.push_back(candidate);
                grid.setCell(candidate.first, candidate.second, settings.semantic);
            }
        }
    } else {
        return Result<Grid2D>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                         "unsupported generator: " + std::string(operation), {}, {},
                                                         "procgen.gridGraph"));
    }
    return Result<Grid2D>::success(std::move(grid));
}

Result<Grid2D> select(const Grid2D& input, std::string_view operation, int mode, int count, float weight,
                      std::uint64_t seed, std::string_view rule) {
    Grid2D out;
    out.resize(input.getWidth(), input.getHeight());
    auto active = cells(input);
    if (operation == "select.random") {
        std::mt19937_64 rng(seed);
        std::shuffle(active.begin(), active.end(), rng);
        const int amount = count > 0 ? std::min(count, int(active.size()))
                                     : int(std::round(std::clamp(weight, 0.f, 1.f) * float(active.size())));
        active.resize(std::size_t(amount));
        for (const auto& point : active)
            out.setCell(point.first, point.second, input.getCell(point.first, point.second));
    } else if (operation == "select.border" || operation == "select.fill") {
        for (const auto& point : active) {
            bool          border = false;
            constexpr int dx[]   = {-1, 1, 0, 0};
            constexpr int dy[]   = {0, 0, -1, 1};
            for (int d = 0; d < 4; ++d) border = border || !occupied(input, point.first + dx[d], point.second + dy[d]);
            if ((operation == "select.border" && border) || (operation == "select.fill" && !border))
                out.setCell(point.first, point.second, input.getCell(point.first, point.second));
        }
    } else if (operation == "select.neighbors") {
        for (const auto& point : active) {
            int neighbors = 0;
            for (int oy = -1; oy <= 1; ++oy)
                for (int ox = -1; ox <= 1; ++ox)
                    if ((ox != 0 || oy != 0) && occupied(input, point.first + ox, point.second + oy)) ++neighbors;
            if ((mode == 0 && neighbors == count) || (mode == 1 && neighbors >= count) ||
                (mode == 2 && neighbors <= count))
                out.setCell(point.first, point.second, input.getCell(point.first, point.second));
        }
    } else if (operation == "select.rule") {
        if (rule.size() != 9)
            return Result<Grid2D>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                             "rule must contain exactly nine characters (0/1/*)", {},
                                                             {}, "procgen.gridGraph"));
        for (const auto& point : active) {
            bool match = true;
            for (int oy = -1; oy <= 1 && match; ++oy)
                for (int ox = -1; ox <= 1; ++ox) {
                    const char expected = rule[std::size_t((oy + 1) * 3 + ox + 1)];
                    if (expected != '*' &&
                        (occupied(input, point.first + ox, point.second + oy) != (expected == '1'))) {
                        match = false;
                        break;
                    }
                }
            if (match) out.setCell(point.first, point.second, input.getCell(point.first, point.second));
        }
    } else if (operation == "select.islands" || operation == "select.island_centers") {
        for (const auto& island : islands(input)) {
            const bool keep = mode == 0 ? int(island.size()) < count
                                        : (mode == 1 ? int(island.size()) > count : int(island.size()) == count);
            if (!keep) continue;
            if (operation == "select.island_centers") {
                long long sx = 0;
                long long sy = 0;
                for (const auto& point : island) {
                    sx += point.first;
                    sy += point.second;
                }
                const int x = int(std::llround(double(sx) / double(island.size())));
                const int y = int(std::llround(double(sy) / double(island.size())));
                out.setCell(x, y, input.getCell(island.front().first, island.front().second));
            } else {
                for (const auto& point : island)
                    out.setCell(point.first, point.second, input.getCell(point.first, point.second));
            }
        }
    } else {
        return Result<Grid2D>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                         "unsupported selector: " + std::string(operation), {}, {},
                                                         "procgen.gridGraph"));
    }
    return Result<Grid2D>::success(std::move(out));
}

Result<Grid2D> findPath(const Grid2D& navigation, const Grid2D& starts, const Grid2D& targets, int semantic) {
    if (navigation.getWidth() != starts.getWidth() || navigation.getHeight() != starts.getHeight() ||
        navigation.getWidth() != targets.getWidth() || navigation.getHeight() != targets.getHeight())
        return Result<Grid2D>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                         "pathfinding grids must have equal dimensions", {}, {},
                                                         "procgen.gridGraph"));
    const auto startCells  = cells(starts);
    const auto targetCells = cells(targets);
    if (startCells.empty() || targetCells.empty())
        return Result<Grid2D>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                         "pathfinding requires start and target cells", {}, {},
                                                         "procgen.gridGraph"));
    const int        width  = navigation.getWidth();
    const int        height = navigation.getHeight();
    std::vector<int> previous(std::size_t(width * height), -1);
    std::queue<int>  pending;
    const int        start = startCells.front().second * width + startCells.front().first;
    pending.push(start);
    previous[std::size_t(start)] = start;
    int destination              = -1;
    while (!pending.empty() && destination < 0) {
        const int current = pending.front();
        pending.pop();
        const int x = current % width;
        const int y = current / width;
        if (occupied(targets, x, y)) {
            destination = current;
            break;
        }
        constexpr int dx[] = {-1, 1, 0, 0};
        constexpr int dy[] = {0, 0, -1, 1};
        for (int d = 0; d < 4; ++d) {
            const int nx = x + dx[d];
            const int ny = y + dy[d];
            if (nx < 0 || ny < 0 || nx >= width || ny >= height || !occupied(navigation, nx, ny)) continue;
            const int next = ny * width + nx;
            if (previous[std::size_t(next)] < 0) {
                previous[std::size_t(next)] = current;
                pending.push(next);
            }
        }
    }
    if (destination < 0)
        return Result<Grid2D>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "no path found", {}, {}, "procgen.gridGraph"));
    Grid2D out;
    out.resize(width, height);
    for (int current = destination;; current = previous[std::size_t(current)]) {
        out.setCell(current % width, current / width, semantic);
        if (current == start) break;
    }
    return Result<Grid2D>::success(std::move(out));
}

}  // namespace eve::procgen::gridgraph
