#include "procgen/GridGraphAlgorithms.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <queue>
#include <random>
#include <simplesquirrel/simplesquirrel.hpp>
#include <sstream>
#include "common/Diagnostic.h"
#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"
#include "procgen/BuildLayerStack.h"
#include "procgen/GridGraph.h"
#include "procgen/GridMeshGraphScript.h"
#include "procgen/IncrementalBuild.h"
#include "procgen/MeshGraph.h"
#include "procgen/ObjectBuildLayer.h"
#include "procgen/PointGraph.h"
#include "procgen/Semantic.h"

// GridGraphAlgorithms.cpp
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

// GridMeshGraphScript.cpp
namespace eve::procgen {

void exposeGridMeshGraphs(ssq::Table& table) {
    auto              grid   = table.addClass<GridGraph>("ProcgenGridGraph",
                                                         std::function<GridGraph*()>([]() -> GridGraph* { return nullptr; }), true);
    const HSQUIRRELVM gridVm = grid.getHandle();
    grid.addFunc("addNode", [gridVm](GridGraph* self, const std::string& id, const std::string& operation) {
        return eve::script::projectResult(gridVm, self->addNode(id, operation));
    });
    grid.addFunc("connect", [gridVm](GridGraph* self, const std::string& from, const std::string& to, int slot) {
        return eve::script::projectResult(gridVm, self->connect(from, to, slot));
    });
    grid.addFunc("setNodeGrid", [gridVm](GridGraph* self, const std::string& id, Grid2D* value) {
        if (!value)
            return eve::script::projectResult(
                gridVm, Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "grid is null")));
        return eve::script::projectResult(gridVm, self->setNodeGrid(id, *value));
    });
    grid.addFunc("setNodeInt", [gridVm](GridGraph* self, const std::string& id, const std::string& key, int value) {
        return eve::script::projectResult(gridVm, self->setNodeInt(id, key, value));
    });
    grid.addFunc("setNodeFloat", [gridVm](GridGraph* self, const std::string& id, const std::string& key, float value) {
        return eve::script::projectResult(gridVm, self->setNodeFloat(id, key, value));
    });
    grid.addFunc("setNodeString",
                 [gridVm](GridGraph* self, const std::string& id, const std::string& key, const std::string& value) {
                     return eve::script::projectResult(gridVm, self->setNodeString(id, key, value));
                 });
    grid.addFunc("setNodePointSubgraph", [gridVm](GridGraph* self, const std::string& id, PointGraph* nested,
                                                  const std::string& input, const std::string& output) {
        if (!nested)
            return eve::script::projectResult(gridVm, Result<void>::failure(Diagnostic::error(
                                                          DiagnosticCode::InvalidArgument, "point graph is null")));
        return eve::script::projectResult(gridVm, self->setNodePointSubgraph(id, *nested, input, output));
    });
    grid.addFunc("execute", [gridVm](GridGraph* self, const std::string& output) -> ssq::Table {
        auto result = self->execute(output);
        if (!result.ok()) return eve::script::projectStatusResult(gridVm, result.status());
        auto        value = std::move(result).takeValue();
        ssq::Object object;
        if (auto* outputGrid = std::get_if<Grid2D>(&value)) {
            auto instance =
                eve::script::makeOwnedSquirrelInstance<Grid2D>(gridVm, std::make_unique<Grid2D>(*outputGrid));
            if (!instance.ok()) return eve::script::projectStatusResult(gridVm, instance.status());
            object = std::move(instance).takeValue();
        } else {
            auto instance = eve::script::makeOwnedSquirrelInstance<PointSet>(
                gridVm, std::make_unique<PointSet>(std::get<PointSet>(std::move(value))));
            if (!instance.ok()) return eve::script::projectStatusResult(gridVm, instance.status());
            object = std::move(instance).takeValue();
        }
        return eve::script::projectStatusResult(gridVm, Status::success(), std::move(object));
    });
    grid.addFunc("validate", [gridVm](GridGraph* self, const std::string& output) {
        return eve::script::projectResult(gridVm, self->validate(output));
    });
    grid.addFunc("clearCache", &GridGraph::clearCache);
    grid.addFunc("getRevision", [](GridGraph* self) { return self->revision(); });
    grid.addFunc("serializeDefinition", &GridGraph::serializeDefinition);
    grid.addFunc("deserializeDefinition", [gridVm](GridGraph* self, const std::string& definition) {
        return eve::script::projectResult(gridVm, self->deserializeDefinition(definition));
    });

    auto              mesh   = table.addClass<MeshGraph>("ProcgenMeshGraph",
                                                         std::function<MeshGraph*()>([]() -> MeshGraph* { return nullptr; }), true);
    const HSQUIRRELVM meshVm = mesh.getHandle();
    mesh.addFunc("addNode", [meshVm](MeshGraph* self, const std::string& id, const std::string& operation) {
        return eve::script::projectResult(meshVm, self->addNode(id, operation));
    });
    mesh.addFunc("connect", [meshVm](MeshGraph* self, const std::string& from, const std::string& to, int slot) {
        return eve::script::projectResult(meshVm, self->connect(from, to, slot));
    });
    mesh.addFunc("setNodeGrid", [meshVm](MeshGraph* self, const std::string& id, Grid2D* value) {
        if (!value)
            return eve::script::projectResult(
                meshVm, Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "grid is null")));
        return eve::script::projectResult(meshVm, self->setNodeGrid(id, *value));
    });
    mesh.addFunc("setNodePoints", [meshVm](MeshGraph* self, const std::string& id, PointSet* value) {
        if (!value)
            return eve::script::projectResult(
                meshVm, Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "points are null")));
        return eve::script::projectResult(meshVm, self->setNodePoints(id, *value));
    });
    mesh.addFunc("setNodeMesh", [meshVm](MeshGraph* self, const std::string& id, MeshBuild* value) {
        if (!value)
            return eve::script::projectResult(
                meshVm, Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "mesh is null")));
        return eve::script::projectResult(meshVm, self->setNodeMesh(id, *value));
    });
    mesh.addFunc("setNodeFloat", [meshVm](MeshGraph* self, const std::string& id, const std::string& key, float value) {
        return eve::script::projectResult(meshVm, self->setNodeFloat(id, key, value));
    });
    mesh.addFunc("setNodeString",
                 [meshVm](MeshGraph* self, const std::string& id, const std::string& key, const std::string& value) {
                     return eve::script::projectResult(meshVm, self->setNodeString(id, key, value));
                 });
    mesh.addFunc("execute", [meshVm](MeshGraph* self, const std::string& output) -> ssq::Table {
        auto result = self->execute(output);
        if (!result.ok()) return eve::script::projectStatusResult(meshVm, result.status());
        auto instance = eve::script::makeOwnedSquirrelInstance<MeshBuild>(
            meshVm, std::make_unique<MeshBuild>(std::move(result).takeValue()));
        if (!instance.ok()) return eve::script::projectStatusResult(meshVm, instance.status());
        return eve::script::projectStatusResult(meshVm, Status::success(), std::move(instance).takeValue());
    });
    mesh.addFunc("validate", [meshVm](MeshGraph* self, const std::string& output) {
        return eve::script::projectResult(meshVm, self->validate(output));
    });
    mesh.addFunc("clearCache", &MeshGraph::clearCache);
    mesh.addFunc("getRevision", [](MeshGraph* self) { return self->revision(); });
    mesh.addFunc("serializeDefinition", &MeshGraph::serializeDefinition);
    mesh.addFunc("deserializeDefinition", [meshVm](MeshGraph* self, const std::string& definition) {
        return eve::script::projectResult(meshVm, self->deserializeDefinition(definition));
    });

    auto objects = table.addClass<ObjectBuildLayer>("ProcgenObjectBuildLayer", ssq::Class::Ctor<ObjectBuildLayer()>());
    const HSQUIRRELVM objectsVm = objects.getHandle();
    objects.addFunc("addAsset", [objectsVm](ObjectBuildLayer* self, const std::string& asset, float weight) {
        return eve::script::projectResult(objectsVm, self->addAsset(asset, weight));
    });
    objects.addFunc("clearAssets", &ObjectBuildLayer::clearAssets);
    objects.addFunc("setSeed", [](ObjectBuildLayer* self, std::uint32_t seed) { self->setSeed(seed); });
    objects.addFunc("setLayerOffset",
                    [](ObjectBuildLayer* self, float x, float y, float z) { self->setLayerOffset(x, y, z); });
    objects.addFunc("setLayerScale", [objectsVm](ObjectBuildLayer* self, float x, float y, float z) {
        return eve::script::projectResult(objectsVm, self->setLayerScale(x, y, z));
    });
    objects.addFunc("setPositionRadius", [objectsVm](ObjectBuildLayer* self, float radius) {
        return eve::script::projectResult(objectsVm, self->setPositionRadius(radius));
    });
    objects.addFunc("setRandomRotation", [objectsVm](ObjectBuildLayer* self, float minPitch, float maxPitch,
                                                     float minYaw, float maxYaw, float minRoll, float maxRoll) {
        return eve::script::projectResult(
            objectsVm, self->setRandomRotation(minPitch, maxPitch, minYaw, maxYaw, minRoll, maxRoll));
    });
    objects.addFunc("setRandomScale", [objectsVm](ObjectBuildLayer* self, float minX, float maxX, float minY,
                                                  float maxY, float minZ, float maxZ, bool uniform) {
        return eve::script::projectResult(objectsVm, self->setRandomScale(minX, maxX, minY, maxY, minZ, maxZ, uniform));
    });
    objects.addFunc("setOrientation",
                    [objectsVm](ObjectBuildLayer* self, float cellSize, float yawOffset, bool invert) {
                        return eve::script::projectResult(objectsVm, self->setOrientation(cellSize, yawOffset, invert));
                    });
    objects.addFunc("disableOrientation", [](ObjectBuildLayer* self) { self->disableOrientation(); });
    objects.addFunc("setPlaceOnTop", [](ObjectBuildLayer* self, bool enabled, bool useLowest, float topOffset) {
        self->setPlaceOnTop(enabled, useLowest, topOffset);
    });
    objects.addFunc("addChild", [objectsVm](ObjectBuildLayer* self, const std::string& asset, int count, float radius,
                                            float minScale, float maxScale, float minYaw, float maxYaw) {
        return eve::script::projectResult(objectsVm,
                                          self->addChild(asset, count, radius, minScale, maxScale, minYaw, maxYaw));
    });
    objects.addFunc("clearChildRules", &ObjectBuildLayer::clearChildRules);
    objects.addFunc("serializeDefinition", &ObjectBuildLayer::serializeDefinition);
    objects.addFunc("deserializeDefinition", [objectsVm](ObjectBuildLayer* self, const std::string& definition) {
        return eve::script::projectResult(objectsVm, self->deserializeDefinition(definition));
    });
    objects.addFunc("build", [objectsVm](ObjectBuildLayer* self, PointSet* source) -> ssq::Table {
        if (!source)
            return eve::script::projectStatusResult(
                objectsVm,
                Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "source points are null"))
                    .status());
        auto result = self->build(*source);
        if (!result.ok()) return eve::script::projectStatusResult(objectsVm, result.status());
        auto instance = eve::script::makeOwnedSquirrelInstance<PointSet>(
            objectsVm, std::make_unique<PointSet>(std::move(result).takeValue()));
        if (!instance.ok()) return eve::script::projectStatusResult(objectsVm, instance.status());
        return eve::script::projectStatusResult(objectsVm, Status::success(), std::move(instance).takeValue());
    });
    objects.addFunc(
        "buildOriented", [objectsVm](ObjectBuildLayer* self, PointSet* source, PointSet* orientation) -> ssq::Table {
            if (!source || !orientation)
                return eve::script::projectStatusResult(
                    objectsVm, Result<void>::failure(
                                   Diagnostic::error(DiagnosticCode::InvalidArgument, "source or orientation is null"))
                                   .status());
            auto result = self->build(*source, orientation);
            if (!result.ok()) return eve::script::projectStatusResult(objectsVm, result.status());
            auto instance = eve::script::makeOwnedSquirrelInstance<PointSet>(
                objectsVm, std::make_unique<PointSet>(std::move(result).takeValue()));
            if (!instance.ok()) return eve::script::projectStatusResult(objectsVm, instance.status());
            return eve::script::projectStatusResult(objectsVm, Status::success(), std::move(instance).takeValue());
        });

    auto execution = table.addClass<BuildLayerExecution>(
        "ProcgenBuildLayerExecution",
        std::function<BuildLayerExecution*()>([]() -> BuildLayerExecution* { return nullptr; }), true);
    const HSQUIRRELVM executionVm = execution.getHandle();
    execution.addFunc("getCount", [](BuildLayerExecution* self) { return self->getCount(); });
    execution.addFunc("getId", &BuildLayerExecution::getId);
    execution.addFunc("getType", &BuildLayerExecution::getType);
    execution.addFunc("getMesh", [executionVm](BuildLayerExecution* self, int index) -> ssq::Table {
        auto result = self->getMesh(index);
        if (!result.ok()) return eve::script::projectStatusResult(executionVm, result.status());
        auto instance = eve::script::makeOwnedSquirrelInstance<MeshBuild>(
            executionVm, std::make_unique<MeshBuild>(std::move(result).takeValue()));
        if (!instance.ok()) return eve::script::projectStatusResult(executionVm, instance.status());
        return eve::script::projectStatusResult(executionVm, Status::success(), std::move(instance).takeValue());
    });
    execution.addFunc("getPoints", [executionVm](BuildLayerExecution* self, int index) -> ssq::Table {
        auto result = self->getPoints(index);
        if (!result.ok()) return eve::script::projectStatusResult(executionVm, result.status());
        auto instance = eve::script::makeOwnedSquirrelInstance<PointSet>(
            executionVm, std::make_unique<PointSet>(std::move(result).takeValue()));
        if (!instance.ok()) return eve::script::projectStatusResult(executionVm, instance.status());
        return eve::script::projectStatusResult(executionVm, Status::success(), std::move(instance).takeValue());
    });

    auto stack = table.addClass<BuildLayerStack>("ProcgenBuildLayerStack", ssq::Class::Ctor<BuildLayerStack()>());
    const HSQUIRRELVM stackVm = stack.getHandle();
    stack.addFunc("addTileLayer", [stackVm](BuildLayerStack* self, const std::string& id, bool enabled, float cellSize,
                                            float height, const std::string& group) {
        return eve::script::projectResult(stackVm, self->addTileLayer(id, enabled, cellSize, height, group));
    });
    stack.addFunc("addObjectLayer", [stackVm](BuildLayerStack* self, const std::string& id, bool enabled,
                                              ObjectBuildLayer* layer) {
        if (!layer)
            return eve::script::projectResult(stackVm, Result<void>::failure(Diagnostic::error(
                                                           DiagnosticCode::InvalidArgument, "object layer is null")));
        return eve::script::projectResult(stackVm, self->addObjectLayer(id, enabled, *layer));
    });
    stack.addFunc("setEnabled", [stackVm](BuildLayerStack* self, const std::string& id, bool enabled) {
        return eve::script::projectResult(stackVm, self->setEnabled(id, enabled));
    });
    stack.addFunc("clear", &BuildLayerStack::clear);
    stack.addFunc("getLayerCount", [](BuildLayerStack* self) { return self->getLayerCount(); });
    stack.addFunc("getLayerId", &BuildLayerStack::getLayerId);
    stack.addFunc("getLayerType", &BuildLayerStack::getLayerType);
    stack.addFunc("isLayerEnabled", [](BuildLayerStack* self, int index) { return self->isLayerEnabled(index); });
    stack.addFunc("serializeDefinition", &BuildLayerStack::serializeDefinition);
    stack.addFunc("deserializeDefinition", [stackVm](BuildLayerStack* self, const std::string& definition) {
        return eve::script::projectResult(stackVm, self->deserializeDefinition(definition));
    });
    stack.addFunc("execute", [stackVm](BuildLayerStack* self, Grid2D* grid, PointSet* points) -> ssq::Table {
        if (!grid || !points)
            return eve::script::projectStatusResult(
                stackVm, Result<void>::failure(
                             Diagnostic::error(DiagnosticCode::InvalidArgument, "grid or points input is null"))
                             .status());
        auto result = self->execute(*grid, *points);
        if (!result.ok()) return eve::script::projectStatusResult(stackVm, result.status());
        auto instance = eve::script::makeOwnedSquirrelInstance<BuildLayerExecution>(
            stackVm, std::make_unique<BuildLayerExecution>(std::move(result).takeValue()));
        if (!instance.ok()) return eve::script::projectStatusResult(stackVm, instance.status());
        return eve::script::projectStatusResult(stackVm, Status::success(), std::move(instance).takeValue());
    });
    stack.addFunc(
        "executeOriented",
        [stackVm](BuildLayerStack* self, Grid2D* grid, PointSet* points, PointSet* orientation) -> ssq::Table {
            if (!grid || !points || !orientation)
                return eve::script::projectStatusResult(
                    stackVm, Result<void>::failure(
                                 Diagnostic::error(DiagnosticCode::InvalidArgument, "build-layer input is null"))
                                 .status());
            auto result = self->execute(*grid, *points, orientation);
            if (!result.ok()) return eve::script::projectStatusResult(stackVm, result.status());
            auto instance = eve::script::makeOwnedSquirrelInstance<BuildLayerExecution>(
                stackVm, std::make_unique<BuildLayerExecution>(std::move(result).takeValue()));
            if (!instance.ok()) return eve::script::projectStatusResult(stackVm, instance.status());
            return eve::script::projectStatusResult(stackVm, Status::success(), std::move(instance).takeValue());
        });

    auto delta = table.addClass<IncrementalBuildDelta>(
        "ProcgenIncrementalBuildDelta",
        std::function<IncrementalBuildDelta*()>([]() -> IncrementalBuildDelta* { return nullptr; }), true);
    const HSQUIRRELVM deltaVm = delta.getHandle();
    delta.addFunc("getCount", [](IncrementalBuildDelta* self) { return self->getCount(); });
    delta.addFunc("getClusterX", [](IncrementalBuildDelta* self, int index) { return self->getClusterX(index); });
    delta.addFunc("getClusterZ", [](IncrementalBuildDelta* self, int index) { return self->getClusterZ(index); });
    delta.addFunc("isRemoved", [](IncrementalBuildDelta* self, int index) { return self->isRemoved(index); });
    delta.addFunc("getArtifacts", [deltaVm](IncrementalBuildDelta* self, int index) -> ssq::Table {
        auto result = self->getArtifacts(index);
        if (!result.ok()) return eve::script::projectStatusResult(deltaVm, result.status());
        auto instance = eve::script::makeOwnedSquirrelInstance<BuildLayerExecution>(
            deltaVm, std::make_unique<BuildLayerExecution>(std::move(result).takeValue()));
        if (!instance.ok()) return eve::script::projectStatusResult(deltaVm, instance.status());
        return eve::script::projectStatusResult(deltaVm, Status::success(), std::move(instance).takeValue());
    });

    auto              incremental   = table.addClass<IncrementalBuildExecutor>("ProcgenIncrementalBuildExecutor",
                                                                               ssq::Class::Ctor<IncrementalBuildExecutor()>());
    const HSQUIRRELVM incrementalVm = incremental.getHandle();
    incremental.addFunc("clear", &IncrementalBuildExecutor::clear);
    incremental.addFunc("getCachedClusterCount",
                        [](IncrementalBuildExecutor* self) { return self->getCachedClusterCount(); });
    incremental.addFunc(
        "getCachedArtifacts", [incrementalVm](IncrementalBuildExecutor* self, int x, int z) -> ssq::Table {
            auto result = self->getCachedArtifacts(x, z);
            if (!result.ok()) return eve::script::projectStatusResult(incrementalVm, result.status());
            auto instance = eve::script::makeOwnedSquirrelInstance<BuildLayerExecution>(
                incrementalVm, std::make_unique<BuildLayerExecution>(std::move(result).takeValue()));
            if (!instance.ok()) return eve::script::projectStatusResult(incrementalVm, instance.status());
            return eve::script::projectStatusResult(incrementalVm, Status::success(), std::move(instance).takeValue());
        });
    incremental.addFunc(
        "update",
        [incrementalVm](IncrementalBuildExecutor* self, BuildLayerStack* buildStack, Grid2D* grid, PointSet* points,
                        int clusterSizeCells, float cellSizeWorld) -> ssq::Table {
            if (!buildStack || !grid || !points)
                return eve::script::projectStatusResult(
                    incrementalVm, Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                           "incremental build input is null"))
                                       .status());
            auto result = self->update(*buildStack, *grid, *points, clusterSizeCells, cellSizeWorld);
            if (!result.ok()) return eve::script::projectStatusResult(incrementalVm, result.status());
            auto instance = eve::script::makeOwnedSquirrelInstance<IncrementalBuildDelta>(
                incrementalVm, std::make_unique<IncrementalBuildDelta>(std::move(result).takeValue()));
            if (!instance.ok()) return eve::script::projectStatusResult(incrementalVm, instance.status());
            return eve::script::projectStatusResult(incrementalVm, Status::success(), std::move(instance).takeValue());
        });
    incremental.addFunc(
        "updateOriented",
        [incrementalVm](IncrementalBuildExecutor* self, BuildLayerStack* buildStack, Grid2D* grid, PointSet* points,
                        int clusterSizeCells, float cellSizeWorld, PointSet* orientation) -> ssq::Table {
            if (!buildStack || !grid || !points || !orientation)
                return eve::script::projectStatusResult(
                    incrementalVm, Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                           "incremental build input is null"))
                                       .status());
            auto result = self->update(*buildStack, *grid, *points, clusterSizeCells, cellSizeWorld, orientation);
            if (!result.ok()) return eve::script::projectStatusResult(incrementalVm, result.status());
            auto instance = eve::script::makeOwnedSquirrelInstance<IncrementalBuildDelta>(
                incrementalVm, std::make_unique<IncrementalBuildDelta>(std::move(result).takeValue()));
            if (!instance.ok()) return eve::script::projectStatusResult(incrementalVm, instance.status());
            return eve::script::projectStatusResult(incrementalVm, Status::success(), std::move(instance).takeValue());
        });
}

}  // namespace eve::procgen
