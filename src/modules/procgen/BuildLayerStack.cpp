#include "procgen/BuildLayerStack.h"

#include "common/Diagnostic.h"
#include "procgen/MeshGraph.h"

#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace eve::procgen {
namespace {

template <class T>
Result<T> fail(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path), {}, "procgen.buildLayers"));
}

std::string hexEncode(std::string_view value) {
    constexpr char digits[] = "0123456789abcdef";
    std::string    output;
    output.reserve(value.size() * 2);
    for (const unsigned char byte : value) {
        output.push_back(digits[byte >> 4U]);
        output.push_back(digits[byte & 15U]);
    }
    return output;
}

Result<std::string> hexDecode(std::string_view value) {
    if (value.size() % 2 != 0) return fail<std::string>(DiagnosticCode::ParseError, "odd embedded definition size");
    const auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string output(value.size() / 2, '\0');
    for (std::size_t index = 0; index < value.size(); index += 2) {
        const int high = nibble(value[index]);
        const int low  = nibble(value[index + 1]);
        if (high < 0 || low < 0) return fail<std::string>(DiagnosticCode::ParseError, "invalid embedded definition");
        output[index / 2] = char((high << 4) | low);
    }
    return Result<std::string>::success(std::move(output));
}

}  // namespace

int BuildLayerExecution::getCount() const noexcept { return int(artifacts_.size()); }

std::string BuildLayerExecution::getId(int index) const {
    return index >= 0 && index < int(artifacts_.size()) ? artifacts_[std::size_t(index)].id : std::string();
}

std::string BuildLayerExecution::getType(int index) const {
    if (index < 0 || index >= int(artifacts_.size())) return {};
    return std::holds_alternative<MeshBuild>(artifacts_[std::size_t(index)].value) ? "mesh" : "points";
}

Result<MeshBuild> BuildLayerExecution::getMesh(int index) const {
    if (index < 0 || index >= int(artifacts_.size()))
        return fail<MeshBuild>(DiagnosticCode::InvalidArgument, "artifact index is out of range");
    const auto* value = std::get_if<MeshBuild>(&artifacts_[std::size_t(index)].value);
    if (!value) return fail<MeshBuild>(DiagnosticCode::TypeMismatch, "artifact is not a mesh");
    return Result<MeshBuild>::success(*value);
}

Result<PointSet> BuildLayerExecution::getPoints(int index) const {
    if (index < 0 || index >= int(artifacts_.size()))
        return fail<PointSet>(DiagnosticCode::InvalidArgument, "artifact index is out of range");
    const auto* value = std::get_if<PointSet>(&artifacts_[std::size_t(index)].value);
    if (!value) return fail<PointSet>(DiagnosticCode::TypeMismatch, "artifact is not a point set");
    return Result<PointSet>::success(*value);
}

bool BuildLayerStack::contains(std::string_view id) const {
    for (const auto& layer : layers_)
        if (layer.id == id) return true;
    return false;
}

Result<void> BuildLayerStack::addTileLayer(std::string id, bool enabled, float cellSize, float height,
                                           std::string group) {
    if (id.empty()) return fail<void>(DiagnosticCode::InvalidArgument, "layer id is empty");
    if (contains(id)) return fail<void>(DiagnosticCode::Conflict, "duplicate layer id", id);
    if (!std::isfinite(cellSize) || cellSize <= 0.f || !std::isfinite(height))
        return fail<void>(DiagnosticCode::InvalidArgument, "tile dimensions are invalid", id);
    layers_.push_back({std::move(id), enabled, TileLayer{cellSize, height, std::move(group)}});
    return Result<void>::success();
}

Result<void> BuildLayerStack::addObjectLayer(std::string id, bool enabled, const ObjectBuildLayer& layer) {
    if (id.empty()) return fail<void>(DiagnosticCode::InvalidArgument, "layer id is empty");
    if (contains(id)) return fail<void>(DiagnosticCode::Conflict, "duplicate layer id", id);
    layers_.push_back({std::move(id), enabled, layer});
    return Result<void>::success();
}

Result<void> BuildLayerStack::setEnabled(std::string_view id, bool enabled) {
    for (auto& layer : layers_) {
        if (layer.id != id) continue;
        layer.enabled = enabled;
        return Result<void>::success();
    }
    return fail<void>(DiagnosticCode::NotFound, "unknown layer", std::string(id));
}

void BuildLayerStack::clear() { layers_.clear(); }
int  BuildLayerStack::getLayerCount() const noexcept { return int(layers_.size()); }

std::string BuildLayerStack::getLayerId(int index) const {
    return index >= 0 && index < int(layers_.size()) ? layers_[std::size_t(index)].id : std::string();
}

std::string BuildLayerStack::getLayerType(int index) const {
    if (index < 0 || index >= int(layers_.size())) return {};
    return std::holds_alternative<TileLayer>(layers_[std::size_t(index)].definition) ? "tiles" : "objects";
}

bool BuildLayerStack::isLayerEnabled(int index) const noexcept {
    return index >= 0 && index < int(layers_.size()) && layers_[std::size_t(index)].enabled;
}

Result<BuildLayerExecution> BuildLayerStack::execute(const Grid2D& grid, const PointSet& points,
                                                      const PointSet* orientation) const {
    return executeRegion(grid, points,
                         {0, 0, grid.getWidth(), grid.getHeight(), -std::numeric_limits<float>::infinity(),
                          -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(),
                          std::numeric_limits<float>::infinity()},
                         orientation);
}

Result<BuildLayerExecution> BuildLayerStack::executeRegion(const Grid2D& grid, const PointSet& points,
                                                            const BuildLayerRegion& region,
                                                            const PointSet* orientation) const {
    if (region.minCellX < 0 || region.minCellY < 0 || region.maxCellX < region.minCellX ||
        region.maxCellY < region.minCellY || region.maxCellX > grid.getWidth() || region.maxCellY > grid.getHeight() ||
        !(region.minWorldX < region.maxWorldX) || !(region.minWorldZ < region.maxWorldZ))
        return fail<BuildLayerExecution>(DiagnosticCode::InvalidArgument, "invalid build-layer region");
    PointSet regionPoints;
    regionPoints.reserve(points.points().size());
    for (std::size_t index = 0; index < points.points().size(); ++index) {
        const auto& point = points.points()[index];
        if (point.x < region.minWorldX || point.x >= region.maxWorldX || point.z < region.minWorldZ ||
            point.z >= region.maxWorldZ)
            continue;
        auto appended = regionPoints.appendPointFrom(points, index);
        if (!appended.ok()) return Result<BuildLayerExecution>::failure(appended.status());
    }
    BuildLayerExecution execution;
    execution.artifacts_.reserve(layers_.size());
    for (const auto& layer : layers_) {
        if (!layer.enabled) continue;
        if (const auto* tile = std::get_if<TileLayer>(&layer.definition)) {
            MeshGraph graph;
            auto      result = graph.addNode("grid", "grid.input");
            if (result.ok()) result = graph.addNode("tiles", "mesh.grid_tiles");
            if (result.ok()) result = graph.setNodeGrid("grid", grid);
            if (result.ok()) result = graph.setNodeFloat("tiles", "cellSize", tile->cellSize);
            if (result.ok()) result = graph.setNodeFloat("tiles", "height", tile->height);
            if (result.ok()) result = graph.setNodeFloat("tiles", "minX", float(region.minCellX));
            if (result.ok()) result = graph.setNodeFloat("tiles", "minY", float(region.minCellY));
            if (result.ok()) result = graph.setNodeFloat("tiles", "maxX", float(region.maxCellX));
            if (result.ok()) result = graph.setNodeFloat("tiles", "maxY", float(region.maxCellY));
            if (result.ok()) result = graph.setNodeString("tiles", "group", tile->group);
            if (result.ok()) result = graph.connect("grid", "tiles");
            if (!result.ok()) return Result<BuildLayerExecution>::failure(result.status());
            auto mesh = graph.execute("tiles");
            if (!mesh.ok()) return Result<BuildLayerExecution>::failure(mesh.status());
            execution.artifacts_.push_back({layer.id, std::move(mesh).takeValue()});
        } else {
            auto built = std::get<ObjectBuildLayer>(layer.definition).build(regionPoints, orientation);
            if (!built.ok()) return Result<BuildLayerExecution>::failure(built.status());
            execution.artifacts_.push_back({layer.id, std::move(built).takeValue()});
        }
    }
    return Result<BuildLayerExecution>::success(std::move(execution));
}

std::string BuildLayerStack::serializeDefinition() const {
    std::ostringstream out;
    out << "EVPCG_BUILD_LAYERS 1\n" << std::setprecision(9);
    for (const auto& layer : layers_) {
        if (const auto* tile = std::get_if<TileLayer>(&layer.definition)) {
            out << "TILE " << std::quoted(layer.id) << ' ' << int(layer.enabled) << ' ' << tile->cellSize << ' '
                << tile->height << ' ' << std::quoted(tile->group) << '\n';
        } else {
            const auto& object = std::get<ObjectBuildLayer>(layer.definition);
            out << "OBJECT " << std::quoted(layer.id) << ' ' << int(layer.enabled) << ' '
                << hexEncode(object.serializeDefinition()) << '\n';
        }
    }
    out << "END\n";
    return out.str();
}

Result<void> BuildLayerStack::deserializeDefinition(std::string_view definition) {
    BuildLayerStack    replacement;
    std::istringstream input{std::string(definition)};
    std::string        magic;
    int                version = 0;
    if (!(input >> magic >> version) || magic != "EVPCG_BUILD_LAYERS" || version != 1)
        return fail<void>(DiagnosticCode::ParseError, "invalid build-layer header");
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
        int         enabled = 0;
        if (kind == "TILE") {
            float       cellSize = 0.f, height = 0.f;
            std::string group;
            if (!(record >> std::quoted(id) >> enabled >> cellSize >> height >> std::quoted(group)) ||
                (enabled != 0 && enabled != 1))
                return fail<void>(DiagnosticCode::ParseError, "invalid TILE record");
            auto applied = replacement.addTileLayer(std::move(id), enabled != 0, cellSize, height, std::move(group));
            if (!applied.ok()) return applied;
        } else if (kind == "OBJECT") {
            std::string encoded;
            if (!(record >> std::quoted(id) >> enabled >> encoded) || (enabled != 0 && enabled != 1))
                return fail<void>(DiagnosticCode::ParseError, "invalid OBJECT record");
            auto decoded = hexDecode(encoded);
            if (!decoded.ok()) return Result<void>::failure(decoded.status());
            ObjectBuildLayer object;
            auto             applied = object.deserializeDefinition(decoded.value());
            if (!applied.ok()) return applied;
            auto added = replacement.addObjectLayer(std::move(id), enabled != 0, object);
            if (!added.ok()) return added;
        } else {
            return fail<void>(DiagnosticCode::ParseError, "unknown build-layer record: " + kind);
        }
        record >> std::ws;
        if (!record.eof()) return fail<void>(DiagnosticCode::ParseError, "trailing build-layer record data");
    }
    if (!ended) return fail<void>(DiagnosticCode::ParseError, "build-layer END record is missing");
    *this = std::move(replacement);
    return Result<void>::success();
}

}  // namespace eve::procgen
