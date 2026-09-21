#include "procgen/GridGraph.h"

#include "common/Diagnostic.h"
#include "procgen/GridGraphAlgorithms.h"
#include "procgen/GeneratorRegistry.h"
#include "procgen/Params.h"
#include "procgen/PointGraph.h"
#include "procgen/Semantic.h"
#include "procgen/algorithms/RoguelikeGenerator.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <queue>
#include <sstream>
#include <unordered_set>

namespace eve::procgen {
namespace {

struct OperationSpec {
    const char*        id;
    int                inputs;
    GridGraphValueType output;
};

const std::vector<OperationSpec>& specs() {
    static const std::vector<OperationSpec> value = {
        {"grid.input", 0, GridGraphValueType::Grid},
        {"grid.union", 2, GridGraphValueType::Grid},
        {"grid.intersect", 2, GridGraphValueType::Grid},
        {"grid.subtract", 2, GridGraphValueType::Grid},
        {"grid.invert", 1, GridGraphValueType::Grid},
        {"grid.expand", 1, GridGraphValueType::Grid},
        {"grid.shrink", 1, GridGraphValueType::Grid},
        {"grid.smooth", 1, GridGraphValueType::Grid},
        {"grid.autotile", 1, GridGraphValueType::Grid},
        {"generate.fill", 0, GridGraphValueType::Grid},
        {"generate.random_noise", 0, GridGraphValueType::Grid},
        {"generate.checkerboard", 0, GridGraphValueType::Grid},
        {"generate.dot_grid", 0, GridGraphValueType::Grid},
        {"generate.shape", 0, GridGraphValueType::Grid},
        {"generate.cellular", 0, GridGraphValueType::Grid},
        {"generate.random_walk", 0, GridGraphValueType::Grid},
        {"generate.maze", 0, GridGraphValueType::Grid},
        {"generate.poisson", 0, GridGraphValueType::Grid},
        {"generate.registry", 0, GridGraphValueType::Grid},
        {"select.random", 1, GridGraphValueType::Grid},
        {"select.border", 1, GridGraphValueType::Grid},
        {"select.fill", 1, GridGraphValueType::Grid},
        {"select.neighbors", 1, GridGraphValueType::Grid},
        {"select.rule", 1, GridGraphValueType::Grid},
        {"select.islands", 1, GridGraphValueType::Grid},
        {"select.island_centers", 1, GridGraphValueType::Grid},
        {"select.detail_range", 1, GridGraphValueType::Grid},
        {"select.semantic", 1, GridGraphValueType::Grid},
        {"grid.path", 3, GridGraphValueType::Grid},
        {"convert.grid_to_points", 1, GridGraphValueType::PointSet},
        {"point.subgraph", 1, GridGraphValueType::PointSet},
    };
    return value;
}

const OperationSpec* specFor(std::string_view id) {
    const auto found =
        std::find_if(specs().begin(), specs().end(), [id](const OperationSpec& spec) { return id == spec.id; });
    return found == specs().end() ? nullptr : &*found;
}

template <class T>
Result<T> fail(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path), {}, "procgen.gridGraph"));
}

Result<void> failVoid(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<void>::failure(Diagnostic::error(code, std::move(message), std::move(path), {}, "procgen.gridGraph"));
}

bool sameSize(const Grid2D& a, const Grid2D& b) {
    return a.getWidth() == b.getWidth() && a.getHeight() == b.getHeight();
}

bool occupied(const Grid2D& grid, int x, int y) { return grid.getCell(x, y) != int(Semantic::Empty); }

Grid2D combine(const Grid2D& a, const Grid2D& b, std::string_view operation) {
    Grid2D out = a;
    for (int y = 0; y < a.getHeight(); ++y) {
        for (int x = 0; x < a.getWidth(); ++x) {
            const bool left  = occupied(a, x, y);
            const bool right = occupied(b, x, y);
            bool       keep  = false;
            if (operation == "grid.union") keep = left || right;
            if (operation == "grid.intersect") keep = left && right;
            if (operation == "grid.subtract") keep = left && !right;
            if (!keep) {
                out.setCell(x, y, int(Semantic::Empty));
                out.setDetail(x, y, 0);
            } else if (!left) {
                out.setCell(x, y, b.getCell(x, y));
                out.setDetail(x, y, b.getDetail(x, y));
            }
        }
    }
    return out;
}

Grid2D morphology(const Grid2D& input, int radius, bool expand) {
    Grid2D out = input;
    radius     = std::max(1, radius);
    for (int y = 0; y < input.getHeight(); ++y) {
        for (int x = 0; x < input.getWidth(); ++x) {
            bool result = expand ? occupied(input, x, y) : true;
            for (int oy = -radius; oy <= radius && (expand ? !result : result); ++oy) {
                for (int ox = -radius; ox <= radius; ++ox) {
                    const bool sample = occupied(input, x + ox, y + oy);
                    result            = expand ? (result || sample) : (result && sample);
                    if (expand ? result : !result) break;
                }
            }
            if (result) {
                if (!occupied(out, x, y)) out.setCell(x, y, int(Semantic::Floor));
            } else {
                out.setCell(x, y, int(Semantic::Empty));
                out.setDetail(x, y, 0);
            }
        }
    }
    return out;
}

std::uint64_t hashText(std::string_view text) {
    std::uint64_t hash = 1469598103934665603ull;
    for (const unsigned char value : text) {
        hash ^= value;
        hash *= 1099511628211ull;
    }
    return hash == 0 ? 1 : hash;
}

Result<PointSet> gridToPoints(const Grid2D& grid, int semantic, float cellSize, float originX, float originZ,
                              std::string_view nodeId) {
    if (!std::isfinite(cellSize) || cellSize <= 0.f)
        return fail<PointSet>(DiagnosticCode::InvalidArgument, "cellSize must be finite and positive",
                              std::string(nodeId));
    PointSet points;
    points.reserve(grid.cells().size());
    std::uint64_t ordinal = 0;
    for (int y = 0; y < grid.getHeight(); ++y) {
        for (int x = 0; x < grid.getWidth(); ++x) {
            const int cell = grid.getCell(x, y);
            if (cell == int(Semantic::Empty) || (semantic >= 0 && cell != semantic)) continue;
            const int index =
                points.add(originX + (float(x) + 0.5f) * cellSize, 0.f, originZ + (float(y) + 0.5f) * cellSize);
            auto idResult = points.trySetPointId(index, derivePointId(hashText(nodeId), ++ordinal));
            if (!idResult.ok()) return Result<PointSet>::failure(idResult.status());
            auto xResult = points.trySetIntAttribute(index, "cell_x", x);
            if (!xResult.ok()) return Result<PointSet>::failure(xResult.status());
            auto yResult = points.trySetIntAttribute(index, "cell_y", y);
            if (!yResult.ok()) return Result<PointSet>::failure(yResult.status());
            auto semanticResult = points.trySetIntAttribute(index, "semantic", cell);
            if (!semanticResult.ok()) return Result<PointSet>::failure(semanticResult.status());
            auto detailResult = points.trySetIntAttribute(index, "detail", grid.getDetail(x, y));
            if (!detailResult.ok()) return Result<PointSet>::failure(detailResult.status());
        }
    }
    return Result<PointSet>::success(std::move(points));
}

}  // namespace

Result<void> GridGraph::addNode(std::string id, std::string operation) {
    if (id.empty()) return failVoid(DiagnosticCode::InvalidArgument, "node id is empty");
    if (!specFor(operation)) return failVoid(DiagnosticCode::NotFound, "unknown operation: " + operation, id);
    if (nodes_.contains(id)) return failVoid(DiagnosticCode::Conflict, "duplicate node id: " + id, id);
    Node node;
    node.id        = id;
    node.operation = std::move(operation);
    nodes_.emplace(id, std::move(node));
    order_.push_back(std::move(id));
    ++revision_;
    return Result<void>::success();
}

Result<void> GridGraph::connect(std::string_view fromId, std::string_view toId, int inputIndex) {
    const auto from = nodes_.find(std::string(fromId));
    const auto to   = nodes_.find(std::string(toId));
    if (from == nodes_.end() || to == nodes_.end())
        return failVoid(DiagnosticCode::NotFound, "connection references an unknown node", std::string(toId));
    const OperationSpec* target = specFor(to->second.operation);
    if (!target || inputIndex < 0 || inputIndex >= target->inputs)
        return failVoid(DiagnosticCode::InvalidArgument, "input slot is outside the operation contract",
                        std::string(toId));
    const auto sourceType = operationOutputType(from->second.operation);
    if (!sourceType.ok()) return Result<void>::failure(sourceType.status());
    const GridGraphValueType expected =
        to->second.operation == "point.subgraph" ? GridGraphValueType::PointSet : GridGraphValueType::Grid;
    if (sourceType.value() != expected)
        return failVoid(DiagnosticCode::TypeMismatch, "typed graph ports are incompatible", std::string(toId));
    to->second.inputs[inputIndex] = std::string(fromId);
    invalidateFrom(toId);
    ++revision_;
    return Result<void>::success();
}

Result<void> GridGraph::setNodeGrid(std::string_view id, const Grid2D& grid) {
    const auto found = nodes_.find(std::string(id));
    if (found == nodes_.end()) return failVoid(DiagnosticCode::NotFound, "unknown node", std::string(id));
    if (found->second.operation != "grid.input")
        return failVoid(DiagnosticCode::TypeMismatch, "node is not a grid.input", std::string(id));
    found->second.inputGrid    = grid;
    found->second.hasInputGrid = true;
    invalidateFrom(id);
    ++revision_;
    return Result<void>::success();
}

Result<void> GridGraph::setNodeInt(std::string_view id, std::string key, int value) {
    const auto found = nodes_.find(std::string(id));
    if (found == nodes_.end()) return failVoid(DiagnosticCode::NotFound, "unknown node", std::string(id));
    const bool declared =
        found->second.operation.starts_with("generate.") || found->second.operation.starts_with("select.") ||
        found->second.operation == "grid.path" ||
        ((found->second.operation == "grid.expand" || found->second.operation == "grid.shrink") && key == "radius") ||
        (found->second.operation == "grid.smooth" && key == "threshold") ||
        ((found->second.operation == "grid.invert" || found->second.operation == "convert.grid_to_points") &&
         key == "semantic");
    if (!declared)
        return failVoid(DiagnosticCode::InvalidArgument, "unknown integer parameter: " + key, std::string(id));
    found->second.ints[std::move(key)] = value;
    invalidateFrom(id);
    ++revision_;
    return Result<void>::success();
}

Result<void> GridGraph::setNodeFloat(std::string_view id, std::string key, float value) {
    const auto found = nodes_.find(std::string(id));
    if (found == nodes_.end()) return failVoid(DiagnosticCode::NotFound, "unknown node", std::string(id));
    if (!found->second.operation.starts_with("generate.") && !found->second.operation.starts_with("select.") &&
        (found->second.operation != "convert.grid_to_points" ||
         (key != "cellSize" && key != "originX" && key != "originZ")))
        return failVoid(DiagnosticCode::InvalidArgument, "unknown float parameter: " + key, std::string(id));
    found->second.floats[std::move(key)] = value;
    invalidateFrom(id);
    ++revision_;
    return Result<void>::success();
}

Result<void> GridGraph::setNodeString(std::string_view id, std::string key, std::string value) {
    const auto found = nodes_.find(std::string(id));
    if (found == nodes_.end()) return failVoid(DiagnosticCode::NotFound, "unknown node", std::string(id));
    const bool declared = (found->second.operation == "select.rule" && key == "rule") ||
                          found->second.operation == "generate.registry";
    if (!declared)
        return failVoid(DiagnosticCode::InvalidArgument, "unknown string parameter: " + key, std::string(id));
    found->second.strings[std::move(key)] = std::move(value);
    invalidateFrom(id);
    ++revision_;
    return Result<void>::success();
}

Result<void> GridGraph::setNodePointSubgraph(std::string_view id, const PointGraph& graph, std::string inputNode,
                                             std::string outputNode) {
    const auto found = nodes_.find(std::string(id));
    if (found == nodes_.end()) return failVoid(DiagnosticCode::NotFound, "unknown node", std::string(id));
    if (found->second.operation != "point.subgraph")
        return failVoid(DiagnosticCode::TypeMismatch, "node is not a point.subgraph", std::string(id));
    if (inputNode.empty() || outputNode.empty())
        return failVoid(DiagnosticCode::InvalidArgument, "subgraph ports must be non-empty", std::string(id));
    found->second.pointSubgraph   = std::make_shared<PointGraph>(graph);
    found->second.pointInputNode  = std::move(inputNode);
    found->second.pointOutputNode = std::move(outputNode);
    invalidateFrom(id);
    ++revision_;
    return Result<void>::success();
}

ResultRef<const GridGraphValue> GridGraph::evaluate(std::string_view id, std::unordered_map<std::string, int>& states) {
    const auto found = nodes_.find(std::string(id));
    if (found == nodes_.end())
        return fail<std::reference_wrapper<const GridGraphValue>>(DiagnosticCode::NotFound, "unknown node",
                                                                  std::string(id));
    Node& node = found->second;
    if (node.cacheValid) return ResultRef<const GridGraphValue>::success(std::cref(node.cache));
    if (states[node.id] == 1)
        return fail<std::reference_wrapper<const GridGraphValue>>(DiagnosticCode::Conflict, "graph contains a cycle",
                                                                  node.id);
    states[node.id]                                                = 1;
    const OperationSpec*                                      spec = specFor(node.operation);
    std::vector<std::reference_wrapper<const GridGraphValue>> inputs;
    for (int i = 0; i < spec->inputs; ++i) {
        if (node.inputs[i].empty())
            return fail<std::reference_wrapper<const GridGraphValue>>(DiagnosticCode::NotFound,
                                                                      "required input is disconnected", node.id);
        auto input = evaluate(node.inputs[i], states);
        if (!input.ok()) return ResultRef<const GridGraphValue>::failure(input.status());
        inputs.push_back(input.value());
    }

    if (node.operation == "grid.input") {
        if (!node.hasInputGrid)
            return fail<std::reference_wrapper<const GridGraphValue>>(DiagnosticCode::NotFound,
                                                                      "grid.input has no bound value", node.id);
        node.cache = node.inputGrid;
    } else if (node.operation == "generate.registry") {
        const auto algorithm = node.strings.find("algorithm");
        if (algorithm == node.strings.end() || algorithm->second.empty())
            return fail<std::reference_wrapper<const GridGraphValue>>(
                DiagnosticCode::InvalidArgument, "generate.registry requires a non-empty algorithm parameter",
                node.id);
        Params params;
        params.setSize(node.ints.contains("width") ? node.ints["width"] : 32,
                       node.ints.contains("height") ? node.ints["height"] : 32);
        params.setSeed(std::uint32_t(node.ints.contains("seed") ? node.ints["seed"] : 1));
        for (const auto& [key, value] : node.ints)
            if (key != "width" && key != "height" && key != "seed") params.setInt(key, value);
        for (const auto& [key, value] : node.floats) params.setFloat(key, value);
        for (const auto& [key, value] : node.strings)
            if (key != "algorithm") params.setString(key, value);
        auto& registry = GeneratorRegistry::instance();
        registry.registerBuiltins();
        if (!registry.has(algorithm->second))
            return fail<std::reference_wrapper<const GridGraphValue>>(
                DiagnosticCode::NotFound, "registered generator was not found: " + algorithm->second, node.id);
        Grid2D     generated;
        std::string error;
        if (!registry.generate(algorithm->second, params, generated, error))
            return fail<std::reference_wrapper<const GridGraphValue>>(
                DiagnosticCode::Failed, error.empty() ? "registered generator failed" : std::move(error), node.id);
        node.cache = std::move(generated);
    } else if (node.operation.starts_with("generate.")) {
        gridgraph::GenerateSettings settings;
        settings.width    = node.ints.contains("width") ? node.ints["width"] : 32;
        settings.height   = node.ints.contains("height") ? node.ints["height"] : 32;
        settings.semantic = node.ints.contains("semantic") ? node.ints["semantic"] : int(Semantic::Floor);
        settings.seed     = std::uint64_t(node.ints.contains("seed") ? node.ints["seed"] : 1);
        settings.a        = node.ints.contains("a") ? node.ints["a"] : 1;
        settings.b        = node.ints.contains("b") ? node.ints["b"] : 0;
        settings.c        = node.ints.contains("c") ? node.ints["c"] : 1;
        settings.x        = node.floats.contains("x") ? node.floats["x"] : 0.5f;
        settings.y        = node.floats.contains("y") ? node.floats["y"] : 0.f;
        auto generated    = gridgraph::generate(node.operation, settings);
        if (!generated.ok()) return ResultRef<const GridGraphValue>::failure(generated.status());
        node.cache = std::move(generated).takeValue();
    } else if (node.operation == "select.semantic") {
        const auto* source = std::get_if<Grid2D>(&inputs[0].get());
        if (!source)
            return fail<std::reference_wrapper<const GridGraphValue>>(DiagnosticCode::TypeMismatch,
                                                                      "grid input required", node.id);
        Grid2D out;
        out.resize(source->getWidth(), source->getHeight());
        const int semantic = node.ints.contains("semantic") ? node.ints["semantic"] : int(Semantic::Floor);
        for (int y = 0; y < source->getHeight(); ++y)
            for (int x = 0; x < source->getWidth(); ++x)
                if (source->getCell(x, y) == semantic) {
                    out.setCell(x, y, semantic);
                    out.setDetail(x, y, source->getDetail(x, y));
                }
        for (const auto& [key, value] : source->metadata()) out.setMeta(key, value);
        node.cache = std::move(out);
    } else if (node.operation == "select.detail_range") {
        const auto* source = std::get_if<Grid2D>(&inputs[0].get());
        if (!source)
            return fail<std::reference_wrapper<const GridGraphValue>>(DiagnosticCode::TypeMismatch,
                                                                      "grid input required", node.id);
        Grid2D out;
        out.resize(source->getWidth(), source->getHeight());
        const int minimum = node.ints.contains("min") ? node.ints["min"] : 0;
        const int maximum = node.ints.contains("max") ? node.ints["max"] : 255;
        for (int y = 0; y < source->getHeight(); ++y)
            for (int x = 0; x < source->getWidth(); ++x)
                if (occupied(*source, x, y) && source->getDetail(x, y) >= minimum &&
                    source->getDetail(x, y) <= maximum) {
                    out.setCell(x, y, source->getCell(x, y));
                    out.setDetail(x, y, source->getDetail(x, y));
                }
        node.cache = std::move(out);
    } else if (node.operation.starts_with("select.")) {
        const auto* source = std::get_if<Grid2D>(&inputs[0].get());
        if (!source)
            return fail<std::reference_wrapper<const GridGraphValue>>(DiagnosticCode::TypeMismatch,
                                                                      "grid input required", node.id);
        auto selected = gridgraph::select(*source, node.operation, node.ints.contains("mode") ? node.ints["mode"] : 0,
                                          node.ints.contains("count") ? node.ints["count"] : 0,
                                          node.floats.contains("weight") ? node.floats["weight"] : 0.5f,
                                          std::uint64_t(node.ints.contains("seed") ? node.ints["seed"] : 1),
                                          node.strings.contains("rule") ? node.strings["rule"] : "*********");
        if (!selected.ok()) return ResultRef<const GridGraphValue>::failure(selected.status());
        node.cache = std::move(selected).takeValue();
    } else if (node.operation == "grid.path") {
        const auto* navigation = std::get_if<Grid2D>(&inputs[0].get());
        const auto* starts     = std::get_if<Grid2D>(&inputs[1].get());
        const auto* targets    = std::get_if<Grid2D>(&inputs[2].get());
        if (!navigation || !starts || !targets)
            return fail<std::reference_wrapper<const GridGraphValue>>(DiagnosticCode::TypeMismatch,
                                                                      "three grid inputs required", node.id);
        auto path = gridgraph::findPath(*navigation, *starts, *targets,
                                        node.ints.contains("semantic") ? node.ints["semantic"] : int(Semantic::Floor));
        if (!path.ok()) return ResultRef<const GridGraphValue>::failure(path.status());
        node.cache = std::move(path).takeValue();
    } else if (node.operation == "grid.union" || node.operation == "grid.intersect" ||
               node.operation == "grid.subtract") {
        const auto* first  = std::get_if<Grid2D>(&inputs[0].get());
        const auto* second = std::get_if<Grid2D>(&inputs[1].get());
        if (!first || !second || !sameSize(*first, *second))
            return fail<std::reference_wrapper<const GridGraphValue>>(
                DiagnosticCode::TypeMismatch, "grid inputs must have equal dimensions", node.id);
        node.cache = combine(*first, *second, node.operation);
    } else if (node.operation == "grid.invert") {
        const auto* source = std::get_if<Grid2D>(&inputs[0].get());
        if (!source)
            return fail<std::reference_wrapper<const GridGraphValue>>(DiagnosticCode::TypeMismatch,
                                                                      "grid input required", node.id);
        Grid2D    out      = *source;
        const int semantic = node.ints.contains("semantic") ? node.ints["semantic"] : int(Semantic::Floor);
        for (int y = 0; y < out.getHeight(); ++y)
            for (int x = 0; x < out.getWidth(); ++x)
                out.setCell(x, y, occupied(*source, x, y) ? int(Semantic::Empty) : semantic);
        node.cache = std::move(out);
    } else if (node.operation == "grid.expand" || node.operation == "grid.shrink") {
        const auto* source = std::get_if<Grid2D>(&inputs[0].get());
        if (!source)
            return fail<std::reference_wrapper<const GridGraphValue>>(DiagnosticCode::TypeMismatch,
                                                                      "grid input required", node.id);
        node.cache = morphology(*source, node.ints.contains("radius") ? node.ints["radius"] : 1,
                                node.operation == "grid.expand");
    } else if (node.operation == "grid.smooth") {
        const auto* source = std::get_if<Grid2D>(&inputs[0].get());
        if (!source)
            return fail<std::reference_wrapper<const GridGraphValue>>(DiagnosticCode::TypeMismatch,
                                                                      "grid input required", node.id);
        Grid2D    out       = *source;
        const int threshold = node.ints.contains("threshold") ? node.ints["threshold"] : 4;
        for (int y = 0; y < source->getHeight(); ++y) {
            for (int x = 0; x < source->getWidth(); ++x) {
                int neighbors = 0;
                for (int oy = -1; oy <= 1; ++oy)
                    for (int ox = -1; ox <= 1; ++ox)
                        if ((ox != 0 || oy != 0) && occupied(*source, x + ox, y + oy)) ++neighbors;
                out.setCell(x, y, neighbors >= threshold ? int(Semantic::Floor) : int(Semantic::Empty));
            }
        }
        node.cache = std::move(out);
    } else if (node.operation == "grid.autotile") {
        const auto* source = std::get_if<Grid2D>(&inputs[0].get());
        if (!source)
            return fail<std::reference_wrapper<const GridGraphValue>>(DiagnosticCode::TypeMismatch,
                                                                      "grid input required", node.id);
        Grid2D out = *source;
        auto autotiled = autotileOccupiedGridInPlace(out);
        if (!autotiled.ok())
            return ResultRef<const GridGraphValue>::failure(autotiled.status());
        node.cache = std::move(out);
    } else if (node.operation == "convert.grid_to_points") {
        const auto* source = std::get_if<Grid2D>(&inputs[0].get());
        if (!source)
            return fail<std::reference_wrapper<const GridGraphValue>>(DiagnosticCode::TypeMismatch,
                                                                      "grid input required", node.id);
        auto points = gridToPoints(*source, node.ints.contains("semantic") ? node.ints["semantic"] : -1,
                                   node.floats.contains("cellSize") ? node.floats["cellSize"] : 1.f,
                                   node.floats.contains("originX") ? node.floats["originX"] : 0.f,
                                   node.floats.contains("originZ") ? node.floats["originZ"] : 0.f, node.id);
        if (!points.ok()) return ResultRef<const GridGraphValue>::failure(points.status());
        node.cache = std::move(points).takeValue();
    } else if (node.operation == "point.subgraph") {
        const auto* source = std::get_if<PointSet>(&inputs[0].get());
        if (!source || !node.pointSubgraph)
            return fail<std::reference_wrapper<const GridGraphValue>>(
                DiagnosticCode::TypeMismatch, "bound PointGraph and PointSet input required", node.id);
        PointSet copy = *source;
        if (!node.pointSubgraph->setNodePoints(node.pointInputNode, &copy))
            return fail<std::reference_wrapper<const GridGraphValue>>(
                DiagnosticCode::InvalidArgument, "nested PointGraph input binding failed", node.id);
        auto output = node.pointSubgraph->executeResult(node.pointOutputNode);
        if (!output.ok()) return ResultRef<const GridGraphValue>::failure(output.status());
        node.cache = std::move(output).takeValue();
    }
    states[node.id] = 2;
    node.cacheValid = true;
    return ResultRef<const GridGraphValue>::success(std::cref(node.cache));
}

Result<GridGraphValue> GridGraph::execute(std::string_view outputId) {
    std::unordered_map<std::string, int> states;
    auto                                 result = evaluate(outputId, states);
    if (!result.ok()) return Result<GridGraphValue>::failure(result.status());
    return Result<GridGraphValue>::success(result.value().get());
}

Result<void> GridGraph::validate(std::string_view outputId) const {
    std::unordered_map<std::string, int> states;
    const auto                           visit = [&](const auto& self, std::string_view id) -> Result<void> {
        const auto found = nodes_.find(std::string(id));
        if (found == nodes_.end()) return failVoid(DiagnosticCode::NotFound, "unknown node", std::string(id));
        if (states[found->first] == 1)
            return failVoid(DiagnosticCode::Conflict, "graph contains a cycle", found->first);
        if (states[found->first] == 2) return Result<void>::success();
        states[found->first]      = 1;
        const OperationSpec* spec = specFor(found->second.operation);
        for (int index = 0; index < spec->inputs; ++index) {
            if (found->second.inputs[index].empty())
                return failVoid(DiagnosticCode::NotFound, "required input is disconnected", found->first);
            auto input = self(self, found->second.inputs[index]);
            if (!input.ok()) return input;
        }
        if (found->second.operation == "grid.input" && !found->second.hasInputGrid)
            return failVoid(DiagnosticCode::NotFound, "grid.input has no bound value", found->first);
        if (found->second.operation == "point.subgraph" &&
            (!found->second.pointSubgraph || found->second.pointInputNode.empty() ||
             found->second.pointOutputNode.empty()))
            return failVoid(DiagnosticCode::NotFound, "point.subgraph has no complete binding", found->first);
        states[found->first] = 2;
        return Result<void>::success();
    };
    return visit(visit, outputId);
}

void GridGraph::clearCache() {
    for (auto& [id, node] : nodes_) node.cacheValid = false;
}

void GridGraph::invalidateFrom(std::string_view id) {
    std::queue<std::string>         pending;
    std::unordered_set<std::string> visited;
    pending.push(std::string(id));
    while (!pending.empty()) {
        const std::string current = pending.front();
        pending.pop();
        if (!visited.insert(current).second) continue;
        const auto found = nodes_.find(current);
        if (found != nodes_.end()) found->second.cacheValid = false;
        for (const auto& [otherId, node] : nodes_)
            if (node.inputs[0] == current || node.inputs[1] == current || node.inputs[2] == current)
                pending.push(otherId);
    }
}

int GridGraph::operationCount() { return int(specs().size()); }

std::string GridGraph::operationId(int index) {
    return index >= 0 && index < int(specs().size()) ? specs()[std::size_t(index)].id : std::string();
}

Result<GridGraphValueType> GridGraph::operationOutputType(std::string_view operation) {
    const OperationSpec* spec = specFor(operation);
    if (!spec)
        return fail<GridGraphValueType>(DiagnosticCode::NotFound, "unknown operation: " + std::string(operation));
    return Result<GridGraphValueType>::success(spec->output);
}

std::string GridGraph::serializeDefinition() const {
    std::ostringstream out;
    out << "EVPCG_GRID_GRAPH 1\n" << std::setprecision(9);
    for (const auto& id : order_) {
        const auto& node = nodes_.at(id);
        out << "NODE " << std::quoted(id) << ' ' << std::quoted(node.operation) << '\n';
        std::vector<std::string> keys;
        for (const auto& [key, value] : node.ints) keys.push_back(key);
        std::sort(keys.begin(), keys.end());
        for (const auto& key : keys)
            out << "INT " << std::quoted(id) << ' ' << std::quoted(key) << ' ' << node.ints.at(key) << '\n';
        keys.clear();
        for (const auto& [key, value] : node.floats) keys.push_back(key);
        std::sort(keys.begin(), keys.end());
        for (const auto& key : keys)
            out << "FLOAT " << std::quoted(id) << ' ' << std::quoted(key) << ' ' << node.floats.at(key) << '\n';
        keys.clear();
        for (const auto& [key, value] : node.strings) keys.push_back(key);
        std::sort(keys.begin(), keys.end());
        for (const auto& key : keys)
            out << "STRING " << std::quoted(id) << ' ' << std::quoted(key) << ' ' << std::quoted(node.strings.at(key))
                << '\n';
    }
    for (const auto& id : order_) {
        const auto& node = nodes_.at(id);
        for (int input = 0; input < 3; ++input)
            if (!node.inputs[input].empty())
                out << "EDGE " << std::quoted(node.inputs[input]) << ' ' << std::quoted(id) << ' ' << input << '\n';
    }
    out << "END\n";
    return out.str();
}

Result<void> GridGraph::deserializeDefinition(std::string_view definition) {
    GridGraph          replacement;
    std::istringstream input{std::string(definition)};
    std::string        magic;
    int                version = 0;
    if (!(input >> magic >> version) || magic != "EVPCG_GRID_GRAPH" || version != 1)
        return failVoid(DiagnosticCode::ParseError, "invalid grid graph header");
    std::string line;
    std::getline(input, line);
    bool ended = false;
    while (std::getline(input, line)) {
        if (line.empty()) continue;
        std::istringstream record(line);
        std::string        kind;
        record >> kind;
        if (kind == "END") {
            ended = true;
            break;
        }
        std::string id;
        if (kind == "NODE") {
            std::string operation;
            if (!(record >> std::quoted(id) >> std::quoted(operation)))
                return failVoid(DiagnosticCode::ParseError, "invalid NODE record");
            auto result = replacement.addNode(std::move(id), std::move(operation));
            if (!result.ok()) return result;
        } else if (kind == "EDGE") {
            std::string target;
            int         slot = -1;
            if (!(record >> std::quoted(id) >> std::quoted(target) >> slot))
                return failVoid(DiagnosticCode::ParseError, "invalid EDGE record");
            auto result = replacement.connect(id, target, slot);
            if (!result.ok()) return result;
        } else if (kind == "INT") {
            std::string key;
            int         value = 0;
            if (!(record >> std::quoted(id) >> std::quoted(key) >> value))
                return failVoid(DiagnosticCode::ParseError, "invalid INT record");
            auto result = replacement.setNodeInt(id, std::move(key), value);
            if (!result.ok()) return result;
        } else if (kind == "FLOAT") {
            std::string key;
            float       value = 0.f;
            if (!(record >> std::quoted(id) >> std::quoted(key) >> value))
                return failVoid(DiagnosticCode::ParseError, "invalid FLOAT record");
            auto result = replacement.setNodeFloat(id, std::move(key), value);
            if (!result.ok()) return result;
        } else if (kind == "STRING") {
            std::string key;
            std::string value;
            if (!(record >> std::quoted(id) >> std::quoted(key) >> std::quoted(value)))
                return failVoid(DiagnosticCode::ParseError, "invalid STRING record");
            auto result = replacement.setNodeString(id, std::move(key), std::move(value));
            if (!result.ok()) return result;
        } else {
            return failVoid(DiagnosticCode::ParseError, "unknown grid graph record: " + kind);
        }
        record >> std::ws;
        if (!record.eof()) return failVoid(DiagnosticCode::ParseError, "trailing grid graph record data");
    }
    if (!ended) return failVoid(DiagnosticCode::ParseError, "grid graph END record is missing");
    std::unordered_map<std::string, int> states;
    const auto                           visit = [&](const auto& self, const std::string& id) -> Result<void> {
        if (states[id] == 2) return Result<void>::success();
        if (states[id] == 1) return failVoid(DiagnosticCode::Conflict, "cycle in serialized graph", id);
        states[id]       = 1;
        const auto found = replacement.nodes_.find(id);
        if (found == replacement.nodes_.end()) return failVoid(DiagnosticCode::NotFound, "unknown node", id);
        for (const auto& dependency : found->second.inputs) {
            if (dependency.empty()) continue;
            auto result = self(self, dependency);
            if (!result.ok()) return result;
        }
        states[id] = 2;
        return Result<void>::success();
    };
    for (const auto& id : replacement.order_) {
        auto result = visit(visit, id);
        if (!result.ok()) return result;
    }
    *this = std::move(replacement);
    return Result<void>::success();
}

}  // namespace eve::procgen
