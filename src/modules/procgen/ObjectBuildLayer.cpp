#include "procgen/ObjectBuildLayer.h"

#include "common/Diagnostic.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace eve::procgen {
namespace {

template <class T>
Result<T> fail(std::string message, std::string path = {}) {
    return Result<T>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), std::move(path),
                                                {}, "procgen.objectBuild"));
}

bool finiteRange(float minimum, float maximum) {
    return std::isfinite(minimum) && std::isfinite(maximum) && minimum <= maximum;
}

Result<void> validateRange(const ObjectTransformRange& range) {
    if (!finiteRange(range.minPitch, range.maxPitch) || !finiteRange(range.minYaw, range.maxYaw) ||
        !finiteRange(range.minRoll, range.maxRoll) || !finiteRange(range.minScaleX, range.maxScaleX) ||
        !finiteRange(range.minScaleY, range.maxScaleY) || !finiteRange(range.minScaleZ, range.maxScaleZ))
        return fail<void>("random transform ranges must be finite and ordered", "transform");
    if (range.minScaleX == 0.f || range.maxScaleX == 0.f || range.minScaleY == 0.f || range.maxScaleY == 0.f ||
        range.minScaleZ == 0.f || range.maxScaleZ == 0.f)
        return fail<void>("random scale bounds must be non-zero", "transform.scale");
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
    if (asset.empty()) return fail<void>("asset id is empty", "asset");
    if (!std::isfinite(weight) || weight <= 0.f) return fail<void>("asset weight must be finite and positive", asset);
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
        return fail<void>("layer scale must be finite and non-zero", "scale");
    scaleX_ = x;
    scaleY_ = y;
    scaleZ_ = z;
    return Result<void>::success();
}

Result<void> ObjectBuildLayer::setPositionRadius(float radius) {
    if (!std::isfinite(radius) || radius < 0.f) return fail<void>("position radius must be finite and non-negative");
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
    if (!std::isfinite(cellSize) || cellSize <= 0.f) return fail<void>("orientation cell size must be positive");
    if (!std::isfinite(yawOffset)) return fail<void>("orientation yaw offset must be finite");
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
    if (asset.empty()) return fail<void>("child asset id is empty", "child.asset");
    if (count < 0) return fail<void>("child count must be non-negative", asset);
    if (!std::isfinite(radius) || radius < 0.f)
        return fail<void>("child radius must be finite and non-negative", asset);
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
    if (assets_.empty()) return fail<PointSet>("object build layer has no assets", "assets");
    if (orient_ && !orientation) return fail<PointSet>("orientation is enabled but no orientation layer was supplied");
    float totalWeight = 0.f;
    for (const auto& asset : assets_) totalWeight += asset.weight;
    if (!std::isfinite(totalWeight) || totalWeight <= 0.f) return fail<PointSet>("total asset weight is invalid");

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
                child.id = derivePointId(parent.id != 0 ? parent.id : pointKey(input, sourceIndex, seed_),
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
        return fail<void>("invalid object layer header");
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
            if (!(record >> seed)) return fail<void>("invalid SEED record");
            replacement.setSeed(seed);
        } else if (kind == "OFFSET") {
            float x = 0.f, y = 0.f, z = 0.f;
            if (!(record >> x >> y >> z) || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
                return fail<void>("invalid OFFSET record");
            replacement.setLayerOffset(x, y, z);
        } else if (kind == "SCALE") {
            float x = 0.f, y = 0.f, z = 0.f;
            if (!(record >> x >> y >> z)) return fail<void>("invalid SCALE record");
            auto applied = replacement.setLayerScale(x, y, z);
            if (!applied.ok()) return applied;
        } else if (kind == "POSITION_RADIUS") {
            float radius = 0.f;
            if (!(record >> radius)) return fail<void>("invalid POSITION_RADIUS record");
            auto applied = replacement.setPositionRadius(radius);
            if (!applied.ok()) return applied;
        } else if (kind == "RANDOM") {
            ObjectTransformRange range;
            int                  uniform = 0;
            if (!(record >> range.minPitch >> range.maxPitch >> range.minYaw >> range.maxYaw >> range.minRoll >>
                  range.maxRoll >> range.minScaleX >> range.maxScaleX >> range.minScaleY >> range.maxScaleY >>
                  range.minScaleZ >> range.maxScaleZ >> uniform) ||
                (uniform != 0 && uniform != 1))
                return fail<void>("invalid RANDOM record");
            range.uniformScale = uniform != 0;
            auto applied       = replacement.setRandomTransform(range);
            if (!applied.ok()) return applied;
        } else if (kind == "ORIENTATION") {
            int   enabled = 0, invert = 0;
            float cell = 0.f, offset = 0.f;
            if (!(record >> enabled >> cell >> offset >> invert) || (enabled != 0 && enabled != 1) ||
                (invert != 0 && invert != 1))
                return fail<void>("invalid ORIENTATION record");
            if (enabled) {
                auto applied = replacement.setOrientation(cell, offset, invert != 0);
                if (!applied.ok()) return applied;
            }
        } else if (kind == "PLACE") {
            int   enabled = 0, lowest = 0;
            float offset = 0.f;
            if (!(record >> enabled >> lowest >> offset) || (enabled != 0 && enabled != 1) ||
                (lowest != 0 && lowest != 1) || !std::isfinite(offset))
                return fail<void>("invalid PLACE record");
            replacement.setPlaceOnTop(enabled != 0, lowest != 0, offset);
        } else if (kind == "ASSET") {
            std::string asset;
            float       weight = 0.f;
            if (!(record >> std::quoted(asset) >> weight)) return fail<void>("invalid ASSET record");
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
                return fail<void>("invalid CHILD record");
            range.uniformScale = uniform != 0;
            auto applied       = replacement.addChildRule(std::move(asset), count, radius, range);
            if (!applied.ok()) return applied;
        } else {
            return fail<void>("unknown object layer record: " + kind);
        }
        record >> std::ws;
        if (!record.eof()) return fail<void>("trailing object layer record data");
    }
    if (!ended) return fail<void>("object layer END record is missing");
    *this = std::move(replacement);
    return Result<void>::success();
}

}  // namespace eve::procgen
