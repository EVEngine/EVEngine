#include "procgen/MeshGraph.h"

#include "common/Diagnostic.h"
#include "procgen/Semantic.h"
#include "procgen/TilePreset.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <queue>
#include <sstream>
#include <unordered_set>

namespace eve::procgen {
namespace {

enum class ValueType { Grid, Points, Mesh };

struct Spec {
    const char* id;
    int         inputs;
    ValueType   input[2];
    ValueType   output;
};

constexpr Spec specs[] = {
    {"grid.input", 0, {}, ValueType::Grid},
    {"point.input", 0, {}, ValueType::Points},
    {"mesh.input", 0, {}, ValueType::Mesh},
    {"mesh.grid_tiles", 1, {ValueType::Grid}, ValueType::Mesh},
    {"mesh.instance_points", 2, {ValueType::Mesh, ValueType::Points}, ValueType::Mesh},
    {"mesh.merge", 2, {ValueType::Mesh, ValueType::Mesh}, ValueType::Mesh},
    {"mesh.transform", 1, {ValueType::Mesh}, ValueType::Mesh},
};

const Spec* specFor(std::string_view id) {
    const auto found =
        std::find_if(std::begin(specs), std::end(specs), [id](const Spec& spec) { return id == spec.id; });
    return found == std::end(specs) ? nullptr : found;
}

template <class T>
Result<T> failure(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path), {}, "procgen.meshGraph"));
}

Result<void> failureVoid(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<void>::failure(Diagnostic::error(code, std::move(message), std::move(path), {}, "procgen.meshGraph"));
}

ValueType typeOf(const MeshGraphValue& value) {
    if (std::holds_alternative<Grid2D>(value)) return ValueType::Grid;
    if (std::holds_alternative<PointSet>(value)) return ValueType::Points;
    return ValueType::Mesh;
}

bool acceptsFloat(std::string_view operation, std::string_view key) {
    if (operation == "mesh.grid_tiles")
        return key == "cellSize" || key == "height" || key == "minX" || key == "minY" || key == "maxX" ||
               key == "maxY";
    if (operation == "mesh.instance_points") return key == "scale";
    if (operation == "mesh.transform")
        return key == "x" || key == "y" || key == "z" || key == "yaw" || key == "sx" || key == "sy" || key == "sz";
    return false;
}

bool acceptsString(std::string_view operation, std::string_view key) {
    return operation == "mesh.grid_tiles" && key == "group";
}

void addQuad(MeshBuild& mesh, float ax, float ay, float az, float bx, float by, float bz, float cx, float cy, float cz,
             float dx, float dy, float dz, float nx, float ny, float nz) {
    const auto base = std::uint32_t(mesh.getVertexCount());
    mesh.addVertex(ax, ay, az, nx, ny, nz, 0.f, 0.f);
    mesh.addVertex(bx, by, bz, nx, ny, nz, 1.f, 0.f);
    mesh.addVertex(cx, cy, cz, nx, ny, nz, 1.f, 1.f);
    mesh.addVertex(dx, dy, dz, nx, ny, nz, 0.f, 1.f);
    mesh.addTriangle(base, base + 2, base + 1);
    mesh.addTriangle(base, base + 3, base + 2);
}

MeshBuild buildTiles(const Grid2D& grid, float cellSize, float height, std::string_view group, int minX, int minY,
                     int maxX, int maxY) {
    MeshBuild mesh;
    minX = std::clamp(minX, 0, grid.getWidth());
    minY = std::clamp(minY, 0, grid.getHeight());
    maxX = std::clamp(maxX, minX, grid.getWidth());
    maxY = std::clamp(maxY, minY, grid.getHeight());
    for (int y = minY; y < maxY; ++y) {
        for (int x = minX; x < maxX; ++x) {
            if (grid.getCell(x, y) == int(Semantic::Empty)) continue;
            const auto        preset = resolveTilePreset(std::uint8_t(grid.getDetail(x, y)));
            const std::string cellGroup =
                (group.empty() ? std::string("tiles") : std::string(group)) + "/" + std::to_string(grid.getCell(x, y)) +
                "/" + std::string(tilePresetKindName(preset.kind)) + "/r" + std::to_string(preset.rotationDegrees) +
                (preset.mirrorX ? "/mx/" : "/normal/") + std::to_string(preset.configuration);
            mesh.setActiveGroup(cellGroup);
            const float x0 = float(x) * cellSize;
            const float x1 = x0 + cellSize;
            const float z0 = float(y) * cellSize;
            const float z1 = z0 + cellSize;
            addQuad(mesh, x0, height, z0, x1, height, z0, x1, height, z1, x0, height, z1, 0.f, 1.f, 0.f);
            if (grid.getCell(x - 1, y) == int(Semantic::Empty))
                addQuad(mesh, x0, 0.f, z1, x0, 0.f, z0, x0, height, z0, x0, height, z1, -1.f, 0.f, 0.f);
            if (grid.getCell(x + 1, y) == int(Semantic::Empty))
                addQuad(mesh, x1, 0.f, z0, x1, 0.f, z1, x1, height, z1, x1, height, z0, 1.f, 0.f, 0.f);
            if (grid.getCell(x, y - 1) == int(Semantic::Empty))
                addQuad(mesh, x0, 0.f, z0, x1, 0.f, z0, x1, height, z0, x0, height, z0, 0.f, 0.f, -1.f);
            if (grid.getCell(x, y + 1) == int(Semantic::Empty))
                addQuad(mesh, x1, 0.f, z1, x0, 0.f, z1, x0, height, z1, x1, height, z1, 0.f, 0.f, 1.f);
        }
    }
    mesh.setMeta("source", "mesh.grid_tiles");
    return mesh;
}

}  // namespace

Result<void> MeshGraph::addNode(std::string id, std::string operation) {
    if (id.empty()) return failureVoid(DiagnosticCode::InvalidArgument, "node id is empty");
    if (!specFor(operation)) return failureVoid(DiagnosticCode::NotFound, "unknown operation: " + operation, id);
    if (nodes_.contains(id)) return failureVoid(DiagnosticCode::Conflict, "duplicate node id", id);
    Node node;
    node.id        = id;
    node.operation = std::move(operation);
    nodes_.emplace(id, std::move(node));
    order_.push_back(std::move(id));
    ++revision_;
    return Result<void>::success();
}

Result<void> MeshGraph::connect(std::string_view fromId, std::string_view toId, int inputIndex) {
    const auto from = nodes_.find(std::string(fromId));
    const auto to   = nodes_.find(std::string(toId));
    if (from == nodes_.end() || to == nodes_.end())
        return failureVoid(DiagnosticCode::NotFound, "connection references an unknown node", std::string(toId));
    const Spec* source = specFor(from->second.operation);
    const Spec* target = specFor(to->second.operation);
    if (inputIndex < 0 || inputIndex >= target->inputs)
        return failureVoid(DiagnosticCode::InvalidArgument, "input slot is outside the operation contract",
                           std::string(toId));
    if (source->output != target->input[inputIndex])
        return failureVoid(DiagnosticCode::TypeMismatch, "typed graph ports are incompatible", std::string(toId));
    to->second.inputs[inputIndex] = std::string(fromId);
    invalidateFrom(toId);
    ++revision_;
    return Result<void>::success();
}

Result<void> MeshGraph::setNodeGrid(std::string_view id, const Grid2D& value) {
    const auto found = nodes_.find(std::string(id));
    if (found == nodes_.end() || found->second.operation != "grid.input")
        return failureVoid(DiagnosticCode::TypeMismatch, "node is not grid.input", std::string(id));
    found->second.value    = value;
    found->second.hasValue = true;
    invalidateFrom(id);
    ++revision_;
    return Result<void>::success();
}

Result<void> MeshGraph::setNodePoints(std::string_view id, const PointSet& value) {
    const auto found = nodes_.find(std::string(id));
    if (found == nodes_.end() || found->second.operation != "point.input")
        return failureVoid(DiagnosticCode::TypeMismatch, "node is not point.input", std::string(id));
    found->second.value    = value;
    found->second.hasValue = true;
    invalidateFrom(id);
    ++revision_;
    return Result<void>::success();
}

Result<void> MeshGraph::setNodeMesh(std::string_view id, const MeshBuild& value) {
    const auto found = nodes_.find(std::string(id));
    if (found == nodes_.end() || found->second.operation != "mesh.input")
        return failureVoid(DiagnosticCode::TypeMismatch, "node is not mesh.input", std::string(id));
    found->second.value    = value;
    found->second.hasValue = true;
    invalidateFrom(id);
    ++revision_;
    return Result<void>::success();
}

Result<void> MeshGraph::setNodeFloat(std::string_view id, std::string key, float value) {
    const auto found = nodes_.find(std::string(id));
    if (found == nodes_.end()) return failureVoid(DiagnosticCode::NotFound, "unknown node", std::string(id));
    if (!acceptsFloat(found->second.operation, key))
        return failureVoid(DiagnosticCode::InvalidArgument, "parameter is not declared by this operation: " + key,
                           std::string(id));
    if (!std::isfinite(value))
        return failureVoid(DiagnosticCode::InvalidArgument, "mesh parameter must be finite", std::string(id));
    found->second.floats[std::move(key)] = value;
    invalidateFrom(id);
    ++revision_;
    return Result<void>::success();
}

Result<void> MeshGraph::setNodeString(std::string_view id, std::string key, std::string value) {
    const auto found = nodes_.find(std::string(id));
    if (found == nodes_.end()) return failureVoid(DiagnosticCode::NotFound, "unknown node", std::string(id));
    if (!acceptsString(found->second.operation, key))
        return failureVoid(DiagnosticCode::InvalidArgument, "parameter is not declared by this operation: " + key,
                           std::string(id));
    found->second.strings[std::move(key)] = std::move(value);
    invalidateFrom(id);
    ++revision_;
    return Result<void>::success();
}

Result<MeshGraphValue> MeshGraph::evaluate(std::string_view id, std::unordered_map<std::string, int>& states) {
    const auto found = nodes_.find(std::string(id));
    if (found == nodes_.end())
        return failure<MeshGraphValue>(DiagnosticCode::NotFound, "unknown node", std::string(id));
    Node& node = found->second;
    if (node.cacheValid) return Result<MeshGraphValue>::success(node.cache);
    if (states[node.id] == 1)
        return failure<MeshGraphValue>(DiagnosticCode::Conflict, "graph contains a cycle", node.id);
    states[node.id]                  = 1;
    const Spec*                 spec = specFor(node.operation);
    std::vector<MeshGraphValue> inputs;
    for (int input = 0; input < spec->inputs; ++input) {
        if (node.inputs[input].empty())
            return failure<MeshGraphValue>(DiagnosticCode::NotFound, "required input is disconnected", node.id);
        auto value = evaluate(node.inputs[input], states);
        if (!value.ok()) return value;
        if (typeOf(value.value()) != spec->input[input])
            return failure<MeshGraphValue>(DiagnosticCode::TypeMismatch, "runtime port type mismatch", node.id);
        inputs.push_back(std::move(value).takeValue());
    }
    if (spec->inputs == 0) {
        if (!node.hasValue)
            return failure<MeshGraphValue>(DiagnosticCode::NotFound, "input node has no value", node.id);
        node.cache = node.value;
    } else if (node.operation == "mesh.grid_tiles") {
        const auto& grid = std::get<Grid2D>(inputs[0]);
        node.cache       = buildTiles(
            grid, node.floats.contains("cellSize") ? node.floats["cellSize"] : 1.f,
            node.floats.contains("height") ? node.floats["height"] : 1.f,
            node.strings.contains("group") ? node.strings["group"] : "tiles",
            node.floats.contains("minX") ? int(node.floats["minX"]) : 0,
            node.floats.contains("minY") ? int(node.floats["minY"]) : 0,
            node.floats.contains("maxX") ? int(node.floats["maxX"]) : grid.getWidth(),
            node.floats.contains("maxY") ? int(node.floats["maxY"]) : grid.getHeight());
    } else if (node.operation == "mesh.merge") {
        MeshBuild        result = std::get<MeshBuild>(inputs[0]);
        const MeshBuild& other  = std::get<MeshBuild>(inputs[1]);
        if (!result.appendTransformed(&other, 0.f, 0.f, 0.f, 0.f, 1.f, 1.f, 1.f))
            return failure<MeshGraphValue>(DiagnosticCode::Failed, "mesh merge failed", node.id);
        node.cache = std::move(result);
    } else if (node.operation == "mesh.transform") {
        MeshBuild        result;
        const MeshBuild& source = std::get<MeshBuild>(inputs[0]);
        if (!result.appendTransformed(&source, node.floats["x"], node.floats["y"], node.floats["z"], node.floats["yaw"],
                                      node.floats.contains("sx") ? node.floats["sx"] : 1.f,
                                      node.floats.contains("sy") ? node.floats["sy"] : 1.f,
                                      node.floats.contains("sz") ? node.floats["sz"] : 1.f))
            return failure<MeshGraphValue>(DiagnosticCode::InvalidArgument, "mesh transform failed", node.id);
        node.cache = std::move(result);
    } else if (node.operation == "mesh.instance_points") {
        MeshBuild        result;
        const MeshBuild& source = std::get<MeshBuild>(inputs[0]);
        const PointSet&  points = std::get<PointSet>(inputs[1]);
        const float      scale  = node.floats.contains("scale") ? node.floats["scale"] : 1.f;
        for (int index = 0; index < points.getCount(); ++index)
            if (!result.appendTransformed(&source, points.getX(index), points.getY(index), points.getZ(index), 0.f,
                                          scale, scale, scale))
                return failure<MeshGraphValue>(DiagnosticCode::InvalidArgument, "point instancing failed", node.id);
        node.cache = std::move(result);
    }
    states[node.id] = 2;
    node.cacheValid = true;
    return Result<MeshGraphValue>::success(node.cache);
}

Result<MeshBuild> MeshGraph::execute(std::string_view outputId) {
    std::unordered_map<std::string, int> states;
    auto                                 value = evaluate(outputId, states);
    if (!value.ok()) return Result<MeshBuild>::failure(value.status());
    const auto* mesh = std::get_if<MeshBuild>(&value.value());
    if (!mesh)
        return failure<MeshBuild>(DiagnosticCode::TypeMismatch, "output node is not a mesh", std::string(outputId));
    return Result<MeshBuild>::success(*mesh);
}

Result<void> MeshGraph::validate(std::string_view outputId) const {
    MeshGraph copy   = *this;
    auto      result = copy.execute(outputId);
    if (!result.ok()) return Result<void>::failure(result.status());
    return Result<void>::success();
}

void MeshGraph::clearCache() {
    for (auto& [id, node] : nodes_) node.cacheValid = false;
}

void MeshGraph::invalidateFrom(std::string_view id) {
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
            if (node.inputs[0] == current || node.inputs[1] == current) pending.push(otherId);
    }
}

std::string MeshGraph::serializeDefinition() const {
    std::ostringstream out;
    out << "EVPCG_MESH_GRAPH 1\n" << std::setprecision(9);
    for (const auto& id : order_) {
        const auto& node = nodes_.at(id);
        out << "NODE " << std::quoted(id) << ' ' << std::quoted(node.operation) << '\n';
        std::vector<std::string> keys;
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
        for (int input = 0; input < 2; ++input)
            if (!node.inputs[input].empty())
                out << "EDGE " << std::quoted(node.inputs[input]) << ' ' << std::quoted(id) << ' ' << input << '\n';
    }
    out << "END\n";
    return out.str();
}

Result<void> MeshGraph::deserializeDefinition(std::string_view definition) {
    MeshGraph          replacement;
    std::istringstream input{std::string(definition)};
    std::string        magic;
    int                version = 0;
    if (!(input >> magic >> version) || magic != "EVPCG_MESH_GRAPH" || version != 1)
        return failureVoid(DiagnosticCode::ParseError, "invalid mesh graph header");
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
        auto        result = [&]() -> Result<void> {
            if (kind == "NODE") {
                std::string operation;
                if (!(record >> std::quoted(id) >> std::quoted(operation)))
                    return failureVoid(DiagnosticCode::ParseError, "invalid NODE record");
                return replacement.addNode(std::move(id), std::move(operation));
            }
            if (kind == "EDGE") {
                std::string target;
                int         slot = -1;
                if (!(record >> std::quoted(id) >> std::quoted(target) >> slot))
                    return failureVoid(DiagnosticCode::ParseError, "invalid EDGE record");
                return replacement.connect(id, target, slot);
            }
            if (kind == "FLOAT") {
                std::string key;
                float       value = 0.f;
                if (!(record >> std::quoted(id) >> std::quoted(key) >> value))
                    return failureVoid(DiagnosticCode::ParseError, "invalid FLOAT record");
                return replacement.setNodeFloat(id, std::move(key), value);
            }
            if (kind == "STRING") {
                std::string key;
                std::string value;
                if (!(record >> std::quoted(id) >> std::quoted(key) >> std::quoted(value)))
                    return failureVoid(DiagnosticCode::ParseError, "invalid STRING record");
                return replacement.setNodeString(id, std::move(key), std::move(value));
            }
            return failureVoid(DiagnosticCode::ParseError, "unknown mesh graph record: " + kind);
        }();
        if (!result.ok()) return result;
        record >> std::ws;
        if (!record.eof()) return failureVoid(DiagnosticCode::ParseError, "trailing mesh graph record data");
    }
    if (!ended) return failureVoid(DiagnosticCode::ParseError, "mesh graph END record is missing");

    std::unordered_map<std::string, int> states;
    const auto                           visit = [&](const auto& self, const std::string& id) -> Result<void> {
        if (states[id] == 2) return Result<void>::success();
        if (states[id] == 1) return failureVoid(DiagnosticCode::Conflict, "cycle in serialized graph", id);
        states[id]       = 1;
        const auto found = replacement.nodes_.find(id);
        if (found == replacement.nodes_.end()) return failureVoid(DiagnosticCode::NotFound, "unknown node", id);
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
