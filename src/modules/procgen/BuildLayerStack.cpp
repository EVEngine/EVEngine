#include "procgen/BuildLayerStack.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include "common/Diagnostic.h"
#include "procgen/MeshGraph.h"
#include "procgen/ObjectBuildLayer.h"

// BuildLayerStack.cpp
namespace eve::procgen {
namespace {

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
    if (value.size() % 2 != 0)
        return Result<std::string>::failure(Diagnostic::error(
            DiagnosticCode::ParseError, "odd embedded definition size", {}, {}, "procgen.buildLayers"));
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
        if (high < 0 || low < 0)
            return Result<std::string>::failure(Diagnostic::error(
                DiagnosticCode::ParseError, "invalid embedded definition", {}, {}, "procgen.buildLayers"));
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
        return Result<MeshBuild>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "artifact index is out of range", {}, {}, "procgen.buildLayers"));
    const auto* value = std::get_if<MeshBuild>(&artifacts_[std::size_t(index)].value);
    if (!value)
        return Result<MeshBuild>::failure(
            Diagnostic::error(DiagnosticCode::TypeMismatch, "artifact is not a mesh", {}, {}, "procgen.buildLayers"));
    return Result<MeshBuild>::success(*value);
}

Result<PointSet> BuildLayerExecution::getPoints(int index) const {
    if (index < 0 || index >= int(artifacts_.size()))
        return Result<PointSet>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "artifact index is out of range", {}, {}, "procgen.buildLayers"));
    const auto* value = std::get_if<PointSet>(&artifacts_[std::size_t(index)].value);
    if (!value)
        return Result<PointSet>::failure(Diagnostic::error(DiagnosticCode::TypeMismatch, "artifact is not a point set",
                                                           {}, {}, "procgen.buildLayers"));
    return Result<PointSet>::success(*value);
}

bool BuildLayerStack::contains(std::string_view id) const {
    for (const auto& layer : layers_)
        if (layer.id == id) return true;
    return false;
}

Result<void> BuildLayerStack::addTileLayer(std::string id, bool enabled, float cellSize, float height,
                                           std::string group) {
    if (id.empty())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "layer id is empty", {}, {}, "procgen.buildLayers"));
    if (contains(id))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "duplicate layer id", id, {}, "procgen.buildLayers"));
    if (!std::isfinite(cellSize) || cellSize <= 0.f || !std::isfinite(height))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "tile dimensions are invalid",
                                                       id, {}, "procgen.buildLayers"));
    layers_.push_back({std::move(id), enabled, TileLayer{cellSize, height, std::move(group)}});
    return Result<void>::success();
}

Result<void> BuildLayerStack::addObjectLayer(std::string id, bool enabled, const ObjectBuildLayer& layer) {
    if (id.empty())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "layer id is empty", {}, {}, "procgen.buildLayers"));
    if (contains(id))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "duplicate layer id", id, {}, "procgen.buildLayers"));
    layers_.push_back({std::move(id), enabled, layer});
    return Result<void>::success();
}

Result<void> BuildLayerStack::setEnabled(std::string_view id, bool enabled) {
    for (auto& layer : layers_) {
        if (layer.id != id) continue;
        layer.enabled = enabled;
        return Result<void>::success();
    }
    return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::NotFound, "unknown layer", std::string(id), {}, "procgen.buildLayers"));
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
                                                           const PointSet*         orientation) const {
    if (region.minCellX < 0 || region.minCellY < 0 || region.maxCellX < region.minCellX ||
        region.maxCellY < region.minCellY || region.maxCellX > grid.getWidth() || region.maxCellY > grid.getHeight() ||
        !(region.minWorldX < region.maxWorldX) || !(region.minWorldZ < region.maxWorldZ))
        return Result<BuildLayerExecution>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "invalid build-layer region", {}, {}, "procgen.buildLayers"));
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
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::ParseError, "invalid build-layer header", {}, {}, "procgen.buildLayers"));
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
                return Result<void>::failure(Diagnostic::error(DiagnosticCode::ParseError, "invalid TILE record", {},
                                                               {}, "procgen.buildLayers"));
            auto applied = replacement.addTileLayer(std::move(id), enabled != 0, cellSize, height, std::move(group));
            if (!applied.ok()) return applied;
        } else if (kind == "OBJECT") {
            std::string encoded;
            if (!(record >> std::quoted(id) >> enabled >> encoded) || (enabled != 0 && enabled != 1))
                return Result<void>::failure(Diagnostic::error(DiagnosticCode::ParseError, "invalid OBJECT record", {},
                                                               {}, "procgen.buildLayers"));
            auto decoded = hexDecode(encoded);
            if (!decoded.ok()) return Result<void>::failure(decoded.status());
            ObjectBuildLayer object;
            auto             applied = object.deserializeDefinition(decoded.value());
            if (!applied.ok()) return applied;
            auto added = replacement.addObjectLayer(std::move(id), enabled != 0, object);
            if (!added.ok()) return added;
        } else {
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::ParseError, "unknown build-layer record: " + kind, {}, {}, "procgen.buildLayers"));
        }
        record >> std::ws;
        if (!record.eof())
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::ParseError, "trailing build-layer record data", {}, {}, "procgen.buildLayers"));
    }
    if (!ended)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::ParseError, "build-layer END record is missing",
                                                       {}, {}, "procgen.buildLayers"));
    *this = std::move(replacement);
    return Result<void>::success();
}

}  // namespace eve::procgen

// ObjectBuildLayer.cpp
namespace eve::procgen {
namespace {

bool finiteRange(float minimum, float maximum) {
    return std::isfinite(minimum) && std::isfinite(maximum) && minimum <= maximum;
}

Result<void> validateRange(const ObjectTransformRange& range) {
    if (!finiteRange(range.minPitch, range.maxPitch) || !finiteRange(range.minYaw, range.maxYaw) ||
        !finiteRange(range.minRoll, range.maxRoll) || !finiteRange(range.minScaleX, range.maxScaleX) ||
        !finiteRange(range.minScaleY, range.maxScaleY) || !finiteRange(range.minScaleZ, range.maxScaleZ))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "random transform ranges must be finite and ordered",
                                                       "transform", {}, "procgen.objectBuild"));
    if (range.minScaleX == 0.f || range.maxScaleX == 0.f || range.minScaleY == 0.f || range.maxScaleY == 0.f ||
        range.minScaleZ == 0.f || range.maxScaleZ == 0.f)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "random scale bounds must be non-zero", "transform.scale", {},
                                                       "procgen.objectBuild"));
    return Result<void>::success();
}

std::uint64_t mix(std::uint64_t value) {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

struct RandomStream {
    std::uint64_t state;
    std::uint64_t next() {
        state = mix(state);
        return state;
    }
    float unit() { return float(next() >> 40U) * (1.f / 16777216.f); }
    float between(float minimum, float maximum) { return minimum + (maximum - minimum) * unit(); }
};

std::uint64_t pointKey(const ProcgenPoint& point, std::size_t index, std::uint32_t seed) {
    if (point.id != 0) return mix(point.id ^ (std::uint64_t(seed) << 32U));
    const auto qx = std::int64_t(std::llround(double(point.x) * 1000.0));
    const auto qy = std::int64_t(std::llround(double(point.y) * 1000.0));
    const auto qz = std::int64_t(std::llround(double(point.z) * 1000.0));
    return mix(std::uint64_t(qx) ^ (mix(std::uint64_t(qy)) << 1U) ^ (mix(std::uint64_t(qz)) << 2U) ^
               std::uint64_t(index) ^ (std::uint64_t(seed) << 32U));
}

float orientedYaw(const ProcgenPoint& point, const PointSet& orientation, float cellSize, bool invert) {
    struct Direction {
        float x;
        float z;
        float yaw;
    };
    const Direction cardinal[] = {
        {0.f, 1.f, invert ? 90.f : 270.f}, {1.f, 0.f, 0.f}, {0.f, -1.f, invert ? 270.f : 90.f}, {-1.f, 0.f, 180.f}};
    const float tolerance = std::max(0.0001f, cellSize * 0.01f);
    const auto  matches   = [&](float x, float z) {
        return std::any_of(orientation.points().begin(), orientation.points().end(), [&](const ProcgenPoint& other) {
            return std::abs(other.x - x) <= tolerance && std::abs(other.z - z) <= tolerance;
        });
    };
    for (const auto& direction : cardinal)
        if (matches(point.x + direction.x * cellSize, point.z + direction.z * cellSize)) return direction.yaw;
    const Direction diagonal[] = {{1.f, 1.f, 90.f}, {1.f, -1.f, 0.f}, {-1.f, -1.f, 180.f}, {-1.f, 1.f, 180.f}};
    for (const auto& direction : diagonal)
        if (matches(point.x + direction.x * cellSize, point.z + direction.z * cellSize)) return direction.yaw;
    return 0.f;
}

void applyRandomTransform(ProcgenPoint& point, const ObjectTransformRange& range, RandomStream& random) {
    point.pitch += random.between(range.minPitch, range.maxPitch);
    point.yaw += random.between(range.minYaw, range.maxYaw);
    point.roll += random.between(range.minRoll, range.maxRoll);
    if (range.uniformScale) {
        const float scale = random.between(range.minScaleX, range.maxScaleX);
        point.scaleX *= scale;
        point.scaleY *= scale;
        point.scaleZ *= scale;
    } else {
        point.scaleX *= random.between(range.minScaleX, range.maxScaleX);
        point.scaleY *= random.between(range.minScaleY, range.maxScaleY);
        point.scaleZ *= random.between(range.minScaleZ, range.maxScaleZ);
    }
}

Result<void> tagPoint(PointSet& output, int index, const std::string& asset, std::string role, std::int64_t parent) {
    auto result = output.trySetStringAttribute(index, "asset", asset);
    if (!result.ok()) return result;
    result = output.trySetStringAttribute(index, "object_role", role);
    if (!result.ok()) return result;
    return output.trySetIntAttribute(index, "parent_index", parent);
}

}  // namespace

Result<void> ObjectBuildLayer::addAsset(std::string asset, float weight) {
    if (asset.empty())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "asset id is empty", "asset",
                                                       {}, "procgen.objectBuild"));
    if (!std::isfinite(weight) || weight <= 0.f)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "asset weight must be finite and positive", asset, {},
                                                       "procgen.objectBuild"));
    assets_.push_back({std::move(asset), weight});
    return Result<void>::success();
}

void ObjectBuildLayer::clearAssets() { assets_.clear(); }
void ObjectBuildLayer::setSeed(std::uint32_t seed) noexcept { seed_ = seed == 0 ? 1 : seed; }
void ObjectBuildLayer::setLayerOffset(float x, float y, float z) noexcept {
    offsetX_ = x;
    offsetY_ = y;
    offsetZ_ = z;
}

Result<void> ObjectBuildLayer::setLayerScale(float x, float y, float z) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || x == 0.f || y == 0.f || z == 0.f)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "layer scale must be finite and non-zero", "scale", {},
                                                       "procgen.objectBuild"));
    scaleX_ = x;
    scaleY_ = y;
    scaleZ_ = z;
    return Result<void>::success();
}

Result<void> ObjectBuildLayer::setPositionRadius(float radius) {
    if (!std::isfinite(radius) || radius < 0.f)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "position radius must be finite and non-negative", {}, {},
                                                       "procgen.objectBuild"));
    positionRadius_ = radius;
    return Result<void>::success();
}

Result<void> ObjectBuildLayer::setRandomTransform(const ObjectTransformRange& range) {
    auto valid = validateRange(range);
    if (!valid.ok()) return valid;
    transform_ = range;
    return Result<void>::success();
}

Result<void> ObjectBuildLayer::setRandomRotation(float minPitch, float maxPitch, float minYaw, float maxYaw,
                                                 float minRoll, float maxRoll) {
    auto candidate     = transform_;
    candidate.minPitch = minPitch;
    candidate.maxPitch = maxPitch;
    candidate.minYaw   = minYaw;
    candidate.maxYaw   = maxYaw;
    candidate.minRoll  = minRoll;
    candidate.maxRoll  = maxRoll;
    return setRandomTransform(candidate);
}

Result<void> ObjectBuildLayer::setRandomScale(float minX, float maxX, float minY, float maxY, float minZ, float maxZ,
                                              bool uniform) {
    auto candidate         = transform_;
    candidate.minScaleX    = minX;
    candidate.maxScaleX    = maxX;
    candidate.minScaleY    = minY;
    candidate.maxScaleY    = maxY;
    candidate.minScaleZ    = minZ;
    candidate.maxScaleZ    = maxZ;
    candidate.uniformScale = uniform;
    return setRandomTransform(candidate);
}

Result<void> ObjectBuildLayer::setOrientation(float cellSize, float yawOffset, bool invert) {
    if (!std::isfinite(cellSize) || cellSize <= 0.f)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "orientation cell size must be positive", {}, {}, "procgen.objectBuild"));
    if (!std::isfinite(yawOffset))
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "orientation yaw offset must be finite", {}, {}, "procgen.objectBuild"));
    orient_               = true;
    orientationCellSize_  = cellSize;
    orientationYawOffset_ = yawOffset;
    invertOrientation_    = invert;
    return Result<void>::success();
}

void ObjectBuildLayer::disableOrientation() noexcept { orient_ = false; }

void ObjectBuildLayer::setPlaceOnTop(bool enabled, bool useLowest, float topOffset) noexcept {
    placeOnTop_ = enabled;
    useLowest_  = useLowest;
    topOffset_  = std::isfinite(topOffset) ? topOffset : 0.f;
}

Result<void> ObjectBuildLayer::addChildRule(std::string asset, int count, float radius,
                                            const ObjectTransformRange& range) {
    if (asset.empty())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "child asset id is empty",
                                                       "child.asset", {}, "procgen.objectBuild"));
    if (count < 0)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "child count must be non-negative", asset, {}, "procgen.objectBuild"));
    if (!std::isfinite(radius) || radius < 0.f)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "child radius must be finite and non-negative", asset, {},
                                                       "procgen.objectBuild"));
    auto valid = validateRange(range);
    if (!valid.ok()) return valid;
    children_.push_back({std::move(asset), count, radius, range});
    return Result<void>::success();
}

Result<void> ObjectBuildLayer::addChild(std::string asset, int count, float radius, float minScale, float maxScale,
                                        float minYaw, float maxYaw) {
    ObjectTransformRange range;
    range.minScaleX    = minScale;
    range.maxScaleX    = maxScale;
    range.minScaleY    = minScale;
    range.maxScaleY    = maxScale;
    range.minScaleZ    = minScale;
    range.maxScaleZ    = maxScale;
    range.uniformScale = true;
    range.minYaw       = minYaw;
    range.maxYaw       = maxYaw;
    return addChildRule(std::move(asset), count, radius, range);
}

void ObjectBuildLayer::clearChildRules() { children_.clear(); }

Result<PointSet> ObjectBuildLayer::build(const PointSet& source, const PointSet* orientation) const {
    if (assets_.empty())
        return Result<PointSet>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "object build layer has no assets", "assets", {}, "procgen.objectBuild"));
    if (orient_ && !orientation)
        return Result<PointSet>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "orientation is enabled but no orientation layer was supplied", {}, {},
            "procgen.objectBuild"));
    float totalWeight = 0.f;
    for (const auto& asset : assets_) totalWeight += asset.weight;
    if (!std::isfinite(totalWeight) || totalWeight <= 0.f)
        return Result<PointSet>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "total asset weight is invalid", {}, {}, "procgen.objectBuild"));

    std::size_t childCount = 0;
    for (const auto& child : children_) childCount += std::size_t(child.count);
    PointSet output;
    output.reserve(source.points().size() * (childCount + 1));
    for (std::size_t sourceIndex = 0; sourceIndex < source.points().size(); ++sourceIndex) {
        const auto&  input = source.points()[sourceIndex];
        RandomStream random{pointKey(input, sourceIndex, seed_)};
        float        selection = random.unit() * totalWeight;
        const Asset* selected  = &assets_.back();
        for (const auto& asset : assets_) {
            selection -= asset.weight;
            if (selection < 0.f) {
                selected = &asset;
                break;
            }
        }

        auto appended = output.appendPointFrom(source, sourceIndex);
        if (!appended.ok()) return Result<PointSet>::failure(appended.status());
        const int parentIndex = appended.value();
        auto&     parent      = output.mutablePoint(std::size_t(parentIndex));
        if (positionRadius_ > 0.f) {
            const float angle  = random.unit() * 6.283185307179586f;
            const float radius = std::sqrt(random.unit()) * positionRadius_;
            parent.x += std::cos(angle) * radius;
            parent.z += std::sin(angle) * radius;
        }
        parent.x += offsetX_;
        parent.y += offsetY_;
        parent.z += offsetZ_;
        if (placeOnTop_) {
            const char* attribute = useLowest_ ? "surface_lowest_y" : "surface_highest_y";
            if (source.hasFloatAttribute(int(sourceIndex), attribute))
                parent.y = source.getFloatAttribute(int(sourceIndex), attribute, parent.y) + topOffset_ + offsetY_;
        }
        applyRandomTransform(parent, transform_, random);
        parent.scaleX *= scaleX_;
        parent.scaleY *= scaleY_;
        parent.scaleZ *= scaleZ_;
        if (orient_)
            parent.yaw = orientedYaw(input, *orientation, orientationCellSize_, invertOrientation_) +
                         orientationYawOffset_ + random.between(transform_.minYaw, transform_.maxYaw);
        auto tagged = tagPoint(output, parentIndex, selected->id, "parent", -1);
        if (!tagged.ok()) return Result<PointSet>::failure(tagged.status());

        for (std::size_t ruleIndex = 0; ruleIndex < children_.size(); ++ruleIndex) {
            const auto&  rule = children_[ruleIndex];
            RandomStream childRandom{mix(pointKey(input, sourceIndex, seed_) ^ (std::uint64_t(ruleIndex + 1) << 32U))};
            for (int childOrdinal = 0; childOrdinal < rule.count; ++childOrdinal) {
                ProcgenPoint child = parent;
                child.id           = derivePointId(parent.id != 0 ? parent.id : pointKey(input, sourceIndex, seed_),
                                         std::uint64_t(ruleIndex) * 1000000ULL + std::uint64_t(childOrdinal + 1));
                const float angle  = childRandom.unit() * 6.283185307179586f;
                const float radius = std::sqrt(childRandom.unit()) * rule.radius;
                child.x += std::cos(angle) * radius;
                child.z += std::sin(angle) * radius;
                child.pitch  = 0.f;
                child.yaw    = 0.f;
                child.roll   = 0.f;
                child.scaleX = 1.f;
                child.scaleY = 1.f;
                child.scaleZ = 1.f;
                applyRandomTransform(child, rule.transform, childRandom);
                const int childIndex = output.appendPoint(child);
                tagged               = tagPoint(output, childIndex, rule.asset, "child", parentIndex);
                if (!tagged.ok()) return Result<PointSet>::failure(tagged.status());
            }
        }
    }
    return Result<PointSet>::success(std::move(output));
}

std::string ObjectBuildLayer::serializeDefinition() const {
    std::ostringstream out;
    out << "EVPCG_OBJECT_LAYER 1\n" << std::setprecision(9);
    out << "SEED " << seed_ << '\n';
    out << "OFFSET " << offsetX_ << ' ' << offsetY_ << ' ' << offsetZ_ << '\n';
    out << "SCALE " << scaleX_ << ' ' << scaleY_ << ' ' << scaleZ_ << '\n';
    out << "POSITION_RADIUS " << positionRadius_ << '\n';
    out << "RANDOM " << transform_.minPitch << ' ' << transform_.maxPitch << ' ' << transform_.minYaw << ' '
        << transform_.maxYaw << ' ' << transform_.minRoll << ' ' << transform_.maxRoll << ' ' << transform_.minScaleX
        << ' ' << transform_.maxScaleX << ' ' << transform_.minScaleY << ' ' << transform_.maxScaleY << ' '
        << transform_.minScaleZ << ' ' << transform_.maxScaleZ << ' ' << int(transform_.uniformScale) << '\n';
    out << "ORIENTATION " << int(orient_) << ' ' << orientationCellSize_ << ' ' << orientationYawOffset_ << ' '
        << int(invertOrientation_) << '\n';
    out << "PLACE " << int(placeOnTop_) << ' ' << int(useLowest_) << ' ' << topOffset_ << '\n';
    for (const auto& asset : assets_) out << "ASSET " << std::quoted(asset.id) << ' ' << asset.weight << '\n';
    for (const auto& child : children_) {
        const auto& t = child.transform;
        out << "CHILD " << std::quoted(child.asset) << ' ' << child.count << ' ' << child.radius << ' ' << t.minPitch
            << ' ' << t.maxPitch << ' ' << t.minYaw << ' ' << t.maxYaw << ' ' << t.minRoll << ' ' << t.maxRoll << ' '
            << t.minScaleX << ' ' << t.maxScaleX << ' ' << t.minScaleY << ' ' << t.maxScaleY << ' ' << t.minScaleZ
            << ' ' << t.maxScaleZ << ' ' << int(t.uniformScale) << '\n';
    }
    out << "END\n";
    return out.str();
}

Result<void> ObjectBuildLayer::deserializeDefinition(std::string_view definition) {
    ObjectBuildLayer   replacement;
    std::istringstream input{std::string(definition)};
    std::string        magic;
    int                version = 0;
    if (!(input >> magic >> version) || magic != "EVPCG_OBJECT_LAYER" || version != 1)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "invalid object layer header",
                                                       {}, {}, "procgen.objectBuild"));
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
        if (kind == "SEED") {
            std::uint32_t seed = 0;
            if (!(record >> seed))
                return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "invalid SEED record",
                                                               {}, {}, "procgen.objectBuild"));
            replacement.setSeed(seed);
        } else if (kind == "OFFSET") {
            float x = 0.f, y = 0.f, z = 0.f;
            if (!(record >> x >> y >> z) || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
                return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "invalid OFFSET record",
                                                               {}, {}, "procgen.objectBuild"));
            replacement.setLayerOffset(x, y, z);
        } else if (kind == "SCALE") {
            float x = 0.f, y = 0.f, z = 0.f;
            if (!(record >> x >> y >> z))
                return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "invalid SCALE record",
                                                               {}, {}, "procgen.objectBuild"));
            auto applied = replacement.setLayerScale(x, y, z);
            if (!applied.ok()) return applied;
        } else if (kind == "POSITION_RADIUS") {
            float radius = 0.f;
            if (!(record >> radius))
                return Result<void>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "invalid POSITION_RADIUS record", {}, {}, "procgen.objectBuild"));
            auto applied = replacement.setPositionRadius(radius);
            if (!applied.ok()) return applied;
        } else if (kind == "RANDOM") {
            ObjectTransformRange range;
            int                  uniform = 0;
            if (!(record >> range.minPitch >> range.maxPitch >> range.minYaw >> range.maxYaw >> range.minRoll >>
                  range.maxRoll >> range.minScaleX >> range.maxScaleX >> range.minScaleY >> range.maxScaleY >>
                  range.minScaleZ >> range.maxScaleZ >> uniform) ||
                (uniform != 0 && uniform != 1))
                return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "invalid RANDOM record",
                                                               {}, {}, "procgen.objectBuild"));
            range.uniformScale = uniform != 0;
            auto applied       = replacement.setRandomTransform(range);
            if (!applied.ok()) return applied;
        } else if (kind == "ORIENTATION") {
            int   enabled = 0, invert = 0;
            float cell = 0.f, offset = 0.f;
            if (!(record >> enabled >> cell >> offset >> invert) || (enabled != 0 && enabled != 1) ||
                (invert != 0 && invert != 1))
                return Result<void>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "invalid ORIENTATION record", {}, {}, "procgen.objectBuild"));
            if (enabled) {
                auto applied = replacement.setOrientation(cell, offset, invert != 0);
                if (!applied.ok()) return applied;
            }
        } else if (kind == "PLACE") {
            int   enabled = 0, lowest = 0;
            float offset = 0.f;
            if (!(record >> enabled >> lowest >> offset) || (enabled != 0 && enabled != 1) ||
                (lowest != 0 && lowest != 1) || !std::isfinite(offset))
                return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "invalid PLACE record",
                                                               {}, {}, "procgen.objectBuild"));
            replacement.setPlaceOnTop(enabled != 0, lowest != 0, offset);
        } else if (kind == "ASSET") {
            std::string asset;
            float       weight = 0.f;
            if (!(record >> std::quoted(asset) >> weight))
                return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "invalid ASSET record",
                                                               {}, {}, "procgen.objectBuild"));
            auto applied = replacement.addAsset(std::move(asset), weight);
            if (!applied.ok()) return applied;
        } else if (kind == "CHILD") {
            std::string          asset;
            int                  count = 0, uniform = 0;
            float                radius = 0.f;
            ObjectTransformRange range;
            if (!(record >> std::quoted(asset) >> count >> radius >> range.minPitch >> range.maxPitch >> range.minYaw >>
                  range.maxYaw >> range.minRoll >> range.maxRoll >> range.minScaleX >> range.maxScaleX >>
                  range.minScaleY >> range.maxScaleY >> range.minScaleZ >> range.maxScaleZ >> uniform) ||
                (uniform != 0 && uniform != 1))
                return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "invalid CHILD record",
                                                               {}, {}, "procgen.objectBuild"));
            range.uniformScale = uniform != 0;
            auto applied       = replacement.addChildRule(std::move(asset), count, radius, range);
            if (!applied.ok()) return applied;
        } else {
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           "unknown object layer record: " + kind, {}, {},
                                                           "procgen.objectBuild"));
        }
        record >> std::ws;
        if (!record.eof())
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "trailing object layer record data", {}, {}, "procgen.objectBuild"));
    }
    if (!ended)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "object layer END record is missing", {}, {}, "procgen.objectBuild"));
    *this = std::move(replacement);
    return Result<void>::success();
}

}  // namespace eve::procgen
