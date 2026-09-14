#include "procgen/PointSet.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>
#include <unordered_map>

namespace eve::procgen {
namespace {

Result<void> invalidChannel(std::string message, std::string path) {
    return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), std::move(path)));
}

Result<float> invalidChannelFloat(std::string message, std::string path) {
    return Result<float>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), std::move(path)));
}

bool isMetadataName(std::string_view name) noexcept { return !name.empty() && name.front() != '$'; }

void appendRow(PointSet& output, const PointSet& input, std::size_t index) {
    output.appendPointFrom(input, index).expect("PointSet attribute op requires compatible schemas");
}

}  // namespace

bool isPointFloatSelector(std::string_view name) noexcept {
    return name == "$Density" || name == "$Seed" || name == "$Steepness" || name == "$Position.X" ||
           name == "$Position.Y" || name == "$Position.Z" || name == "$Normal.X" || name == "$Normal.Y" ||
           name == "$Normal.Z" || name == "$Rotation.Pitch" || name == "$Rotation.Yaw" ||
           name == "$Rotation.Roll" || name == "$Scale.X" || name == "$Scale.Y" || name == "$Scale.Z" ||
           name == "$Color.R" || name == "$Color.G" || name == "$Color.B" || name == "$Color.A" ||
           name == "$BoundsMin.X" || name == "$BoundsMin.Y" || name == "$BoundsMin.Z" || name == "$BoundsMax.X" ||
           name == "$BoundsMax.Y" || name == "$BoundsMax.Z";
}

bool isPointFloatChannel(std::string_view name) noexcept {
    return isPointFloatSelector(name) || isMetadataName(name);
}

Result<float> readPointFloatChannel(const PointSet& points, int index, std::string_view name, float defaultValue) {
    if (index < 0 || index >= points.getCount())
        return invalidChannelFloat("point index is out of range", "index");
    if (name.empty()) return invalidChannelFloat("attribute channel must not be empty", "name");
    if (name.front() != '$')
        return Result<float>::success(points.getFloatAttribute(index, std::string(name), defaultValue));
    if (!isPointFloatSelector(name))
        return invalidChannelFloat("unknown float selector '" + std::string(name) + "'", "name");

    const ProcgenPoint& point = points.points()[std::size_t(index)];
    if (name == "$Density") return Result<float>::success(point.density);
    if (name == "$Seed") return Result<float>::success(float(point.seed));
    if (name == "$Steepness") return Result<float>::success(point.steepness);
    if (name == "$Position.X") return Result<float>::success(point.x);
    if (name == "$Position.Y") return Result<float>::success(point.y);
    if (name == "$Position.Z") return Result<float>::success(point.z);
    if (name == "$Normal.X") return Result<float>::success(point.normalX);
    if (name == "$Normal.Y") return Result<float>::success(point.normalY);
    if (name == "$Normal.Z") return Result<float>::success(point.normalZ);
    if (name == "$Rotation.Pitch") return Result<float>::success(point.pitch);
    if (name == "$Rotation.Yaw") return Result<float>::success(point.yaw);
    if (name == "$Rotation.Roll") return Result<float>::success(point.roll);
    if (name == "$Scale.X") return Result<float>::success(point.scaleX);
    if (name == "$Scale.Y") return Result<float>::success(point.scaleY);
    if (name == "$Scale.Z") return Result<float>::success(point.scaleZ);
    if (name == "$Color.R") return Result<float>::success(point.colorR);
    if (name == "$Color.G") return Result<float>::success(point.colorG);
    if (name == "$Color.B") return Result<float>::success(point.colorB);
    if (name == "$Color.A") return Result<float>::success(point.colorA);
    if (name == "$BoundsMin.X") return Result<float>::success(point.boundsMinX);
    if (name == "$BoundsMin.Y") return Result<float>::success(point.boundsMinY);
    if (name == "$BoundsMin.Z") return Result<float>::success(point.boundsMinZ);
    if (name == "$BoundsMax.X") return Result<float>::success(point.boundsMaxX);
    if (name == "$BoundsMax.Y") return Result<float>::success(point.boundsMaxY);
    if (name == "$BoundsMax.Z") return Result<float>::success(point.boundsMaxZ);
    return invalidChannelFloat("unknown float selector '" + std::string(name) + "'", "name");
}

Result<void> writePointFloatChannel(PointSet& points, int index, std::string_view name, float value) {
    if (index < 0 || index >= points.getCount()) return invalidChannel("point index is out of range", "index");
    if (name.empty()) return invalidChannel("attribute channel must not be empty", "name");
    if (name.front() != '$') return points.trySetFloatAttribute(index, std::string(name), value);
    if (!isPointFloatSelector(name))
        return invalidChannel("unknown float selector '" + std::string(name) + "'", "name");

    ProcgenPoint& point = points.mutablePoint(std::size_t(index));
    if (name == "$Density") {
        point.density = value;
        return Result<void>::success();
    }
    if (name == "$Seed") {
        point.seed = value <= 0.f ? 0u : uint32_t(value);
        return Result<void>::success();
    }
    if (name == "$Steepness") {
        point.steepness = std::clamp(value, 0.f, 1.f);
        return Result<void>::success();
    }
    if (name == "$Position.X") {
        point.x = value;
        return Result<void>::success();
    }
    if (name == "$Position.Y") {
        point.y = value;
        return Result<void>::success();
    }
    if (name == "$Position.Z") {
        point.z = value;
        return Result<void>::success();
    }
    if (name == "$Normal.X") {
        point.normalX = value;
        return Result<void>::success();
    }
    if (name == "$Normal.Y") {
        point.normalY = value;
        return Result<void>::success();
    }
    if (name == "$Normal.Z") {
        point.normalZ = value;
        return Result<void>::success();
    }
    if (name == "$Rotation.Pitch") {
        point.pitch = value;
        return Result<void>::success();
    }
    if (name == "$Rotation.Yaw") {
        point.yaw = value;
        return Result<void>::success();
    }
    if (name == "$Rotation.Roll") {
        point.roll = value;
        return Result<void>::success();
    }
    if (name == "$Scale.X") {
        point.scaleX = value;
        return Result<void>::success();
    }
    if (name == "$Scale.Y") {
        point.scaleY = value;
        return Result<void>::success();
    }
    if (name == "$Scale.Z") {
        point.scaleZ = value;
        return Result<void>::success();
    }
    if (name == "$Color.R") {
        point.colorR = value;
        return Result<void>::success();
    }
    if (name == "$Color.G") {
        point.colorG = value;
        return Result<void>::success();
    }
    if (name == "$Color.B") {
        point.colorB = value;
        return Result<void>::success();
    }
    if (name == "$Color.A") {
        point.colorA = value;
        return Result<void>::success();
    }
    if (name == "$BoundsMin.X") {
        point.boundsMinX = value;
        return Result<void>::success();
    }
    if (name == "$BoundsMin.Y") {
        point.boundsMinY = value;
        return Result<void>::success();
    }
    if (name == "$BoundsMin.Z") {
        point.boundsMinZ = value;
        return Result<void>::success();
    }
    if (name == "$BoundsMax.X") {
        point.boundsMaxX = value;
        return Result<void>::success();
    }
    if (name == "$BoundsMax.Y") {
        point.boundsMaxY = value;
        return Result<void>::success();
    }
    if (name == "$BoundsMax.Z") {
        point.boundsMaxZ = value;
        return Result<void>::success();
    }
    return invalidChannel("unknown float selector '" + std::string(name) + "'", "name");
}

PointSet mathPointFloatAttribute(const PointSet& input, const std::string& attribute,
                                 const std::string& outputAttribute, const std::string& operation, float operand,
                                 float defaultValue) {
    PointSet result = input;
    for (int index = 0; index < result.getCount(); ++index) {
        const float value =
            readPointFloatChannel(result, index, attribute, defaultValue).expect("mathPointFloatAttribute input");
        float output = value;
        if (operation == "add")
            output += operand;
        else if (operation == "subtract")
            output -= operand;
        else if (operation == "multiply")
            output *= operand;
        else if (operation == "divide")
            output /= operand;
        else if (operation == "min")
            output = std::min(output, operand);
        else if (operation == "max")
            output = std::max(output, operand);
        writePointFloatChannel(result, index, outputAttribute, output).expect("mathPointFloatAttribute output");
    }
    return result;
}

PointSet filterPointFloatAttribute(const PointSet& input, const std::string& name, float minValue, float maxValue,
                                   bool invert) {
    if (minValue > maxValue) std::swap(minValue, maxValue);
    PointSet output;
    for (int index = 0; index < input.getCount(); ++index) {
        bool keep = false;
        if (isMetadataName(name)) {
            const auto stored = input.attributes().getFloat(std::size_t(index), name);
            keep              = stored && *stored >= minValue && *stored <= maxValue;
        } else {
            const float value =
                readPointFloatChannel(input, index, name, 0.f).expect("filterPointFloatAttribute channel");
            keep = value >= minValue && value <= maxValue;
        }
        if (keep != invert) appendRow(output, input, std::size_t(index));
    }
    return output;
}

PointSet filterPointIntAttribute(const PointSet& input, const std::string& name, std::int64_t minValue,
                                 std::int64_t maxValue, bool invert) {
    if (minValue > maxValue) std::swap(minValue, maxValue);
    PointSet output;
    for (std::size_t index = 0; index < input.points().size(); ++index) {
        const auto value   = input.attributes().getInt(index, name);
        const bool matches = value && *value >= minValue && *value <= maxValue;
        if (matches != invert) appendRow(output, input, index);
    }
    return output;
}

PointSet filterPointBoolAttribute(const PointSet& input, const std::string& name, bool value, bool invert) {
    PointSet output;
    for (std::size_t index = 0; index < input.points().size(); ++index) {
        const auto found   = input.attributes().getBool(index, name);
        const bool matches = found && *found == value;
        if (matches != invert) appendRow(output, input, index);
    }
    return output;
}

Result<PointSet> copyPointAttribute(const PointSet& input, const std::string& source, const std::string& target) {
    if (source.empty() || target.empty())
        return Result<PointSet>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "attribute copy requires source and target", "name"));
    if (source == target) return Result<PointSet>::success(input);

    const bool sourceSelector = isPointFloatSelector(source);
    const bool targetSelector = isPointFloatSelector(target);
    if (!source.empty() && source.front() == '$' && !sourceSelector)
        return Result<PointSet>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "unknown float selector '" + source + "'", "source"));
    if (!target.empty() && target.front() == '$' && !targetSelector)
        return Result<PointSet>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "unknown float selector '" + target + "'", "target"));

    if (!sourceSelector && !targetSelector) {
        PointSet result = input;
        auto     copied = result.tryCopyAttribute(source, target);
        if (!copied.ok()) return Result<PointSet>::failure(copied.status());
        return Result<PointSet>::success(std::move(result));
    }

    if (!isPointFloatChannel(source) || !isPointFloatChannel(target))
        return Result<PointSet>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "attribute copy with selectors requires float channels", "name"));

    PointSet result = input;
    for (int index = 0; index < result.getCount(); ++index) {
        if (isMetadataName(source) && !result.attributes().has(std::size_t(index), source)) continue;
        const float value =
            readPointFloatChannel(result, index, source, 0.f).expect("copyPointAttribute source channel");
        auto written = writePointFloatChannel(result, index, target, value);
        if (!written.ok()) return Result<PointSet>::failure(written.status());
    }
    return Result<PointSet>::success(std::move(result));
}

Result<PointSet> renamePointAttribute(const PointSet& input, const std::string& from, const std::string& to) {
    PointSet result  = input;
    auto     renamed = result.tryRenameAttribute(from, to);
    if (!renamed.ok()) return Result<PointSet>::failure(renamed.status());
    return Result<PointSet>::success(std::move(result));
}

Result<PointSet> deletePointAttribute(const PointSet& input, const std::string& name) {
    PointSet result  = input;
    auto     deleted = result.tryDeleteAttribute(name);
    if (!deleted.ok()) return Result<PointSet>::failure(deleted.status());
    return Result<PointSet>::success(std::move(result));
}

Result<PointSet> transferPointAttribute(const PointSet& target, const PointSet& source, const std::string& attribute,
                                        const std::string& outputAttribute, const std::string& mode) {
    const std::string output = outputAttribute.empty() ? attribute : outputAttribute;
    if (attribute.empty() || output.empty())
        return Result<PointSet>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "attribute transfer requires attribute names", "attribute"));
    if (mode != "index" && mode != "id" && mode != "nearest")
        return Result<PointSet>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "attribute transfer mode must be index, id, or nearest", "mode"));

    const bool sourceSelector = isPointFloatSelector(attribute);
    const bool targetSelector = isPointFloatSelector(output);
    if (!attribute.empty() && attribute.front() == '$' && !sourceSelector)
        return Result<PointSet>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "unknown float selector '" + attribute + "'", "attribute"));
    if (!output.empty() && output.front() == '$' && !targetSelector)
        return Result<PointSet>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "unknown float selector '" + output + "'", "outputAttribute"));

    std::optional<ProcgenAttributeType> metadataType;
    if (!sourceSelector) {
        metadataType = source.attributes().typeOf(attribute);
        if (!metadataType)
            return Result<PointSet>::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "source attribute '" + attribute + "' does not exist", "attribute"));
        if (targetSelector && *metadataType != ProcgenAttributeType::Float)
            return Result<PointSet>::failure(Diagnostic::error(
                DiagnosticCode::TypeMismatch, "only float metadata can transfer into selectors", "attribute"));
    }

    std::unordered_map<std::uint64_t, int> ids;
    if (mode == "id") {
        for (int index = 0; index < source.getCount(); ++index) {
            const std::uint64_t id = source.getPointId(index);
            if (id != 0) ids.emplace(id, index);
        }
    }

    auto nearestSource = [&](int targetIndex) -> int {
        const ProcgenPoint& point = target.points()[std::size_t(targetIndex)];
        int                 best  = -1;
        float               bestD = std::numeric_limits<float>::infinity();
        for (int index = 0; index < source.getCount(); ++index) {
            const ProcgenPoint& other = source.points()[std::size_t(index)];
            const float         dx    = point.x - other.x;
            const float         dy    = point.y - other.y;
            const float         dz    = point.z - other.z;
            const float         d     = dx * dx + dy * dy + dz * dz;
            if (d < bestD) {
                bestD = d;
                best  = index;
            }
        }
        return best;
    };

    PointSet result = target;
    for (int targetIndex = 0; targetIndex < result.getCount(); ++targetIndex) {
        int sourceIndex = -1;
        if (mode == "index") {
            if (targetIndex < source.getCount()) sourceIndex = targetIndex;
        } else if (mode == "id") {
            const std::uint64_t id = result.getPointId(targetIndex);
            if (id != 0) {
                const auto found = ids.find(id);
                if (found != ids.end()) sourceIndex = found->second;
            }
        } else {
            sourceIndex = nearestSource(targetIndex);
        }
        if (sourceIndex < 0) continue;

        if (sourceSelector || targetSelector ||
            (metadataType && *metadataType == ProcgenAttributeType::Float && isPointFloatChannel(output))) {
            if (isMetadataName(attribute) && !source.attributes().has(std::size_t(sourceIndex), attribute)) continue;
            const float value =
                readPointFloatChannel(source, sourceIndex, attribute, 0.f).expect("transfer float source");
            auto written = writePointFloatChannel(result, targetIndex, output, value);
            if (!written.ok()) return Result<PointSet>::failure(written.status());
            continue;
        }

        switch (*metadataType) {
            case ProcgenAttributeType::Float: {
                const auto value = source.attributes().getFloat(std::size_t(sourceIndex), attribute);
                if (!value) break;
                auto written = result.trySetFloatAttribute(targetIndex, output, *value);
                if (!written.ok()) return Result<PointSet>::failure(written.status());
                break;
            }
            case ProcgenAttributeType::Int: {
                const auto value = source.attributes().getInt(std::size_t(sourceIndex), attribute);
                if (!value) break;
                auto written = result.trySetIntAttribute(targetIndex, output, *value);
                if (!written.ok()) return Result<PointSet>::failure(written.status());
                break;
            }
            case ProcgenAttributeType::Bool: {
                const auto value = source.attributes().getBool(std::size_t(sourceIndex), attribute);
                if (!value) break;
                auto written = result.trySetBoolAttribute(targetIndex, output, *value);
                if (!written.ok()) return Result<PointSet>::failure(written.status());
                break;
            }
            case ProcgenAttributeType::Vector: {
                const auto value = source.attributes().getVector(std::size_t(sourceIndex), attribute);
                if (!value) break;
                auto written = result.trySetVectorAttribute(targetIndex, output, value->x, value->y, value->z);
                if (!written.ok()) return Result<PointSet>::failure(written.status());
                break;
            }
            case ProcgenAttributeType::String: {
                const auto value = source.attributes().getString(std::size_t(sourceIndex), attribute);
                if (!value) break;
                auto written = result.trySetStringAttribute(targetIndex, output, std::string(*value));
                if (!written.ok()) return Result<PointSet>::failure(written.status());
                break;
            }
        }
    }
    return Result<PointSet>::success(std::move(result));
}

PointSet setPointIntAttribute(const PointSet& input, const std::string& attribute, std::int64_t value) {
    PointSet result = input;
    for (int index = 0; index < result.getCount(); ++index)
        result.trySetIntAttribute(index, attribute, value).expect("setPointIntAttribute schema");
    return result;
}

PointSet setPointBoolAttribute(const PointSet& input, const std::string& attribute, bool value) {
    PointSet result = input;
    for (int index = 0; index < result.getCount(); ++index)
        result.trySetBoolAttribute(index, attribute, value).expect("setPointBoolAttribute schema");
    return result;
}

PointSet setPointVectorAttribute(const PointSet& input, const std::string& attribute, float x, float y, float z) {
    PointSet result = input;
    for (int index = 0; index < result.getCount(); ++index)
        result.trySetVectorAttribute(index, attribute, x, y, z).expect("setPointVectorAttribute schema");
    return result;
}

Result<PointSet> comparePointFloatAttribute(const PointSet& input, const std::string& attribute,
                                            const std::string& comparison, float operand,
                                            const std::string& outputAttribute, float defaultValue) {
    if (!isPointFloatChannel(attribute))
        return Result<PointSet>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "compare requires a float channel", "attribute"));
    if (outputAttribute.empty() || !isMetadataName(outputAttribute))
        return Result<PointSet>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "compare output must be a metadata bool attribute", "outputAttribute"));
    if (comparison != "eq" && comparison != "ne" && comparison != "lt" && comparison != "le" && comparison != "gt" &&
        comparison != "ge")
        return Result<PointSet>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "compare comparison must be eq/ne/lt/le/gt/ge", "comparison"));

    PointSet result = input;
    for (int index = 0; index < result.getCount(); ++index) {
        const float value =
            readPointFloatChannel(result, index, attribute, defaultValue).expect("comparePointFloatAttribute");
        bool matches = false;
        if (comparison == "eq")
            matches = value == operand;
        else if (comparison == "ne")
            matches = value != operand;
        else if (comparison == "lt")
            matches = value < operand;
        else if (comparison == "le")
            matches = value <= operand;
        else if (comparison == "gt")
            matches = value > operand;
        else
            matches = value >= operand;
        auto written = result.trySetBoolAttribute(index, outputAttribute, matches);
        if (!written.ok()) return Result<PointSet>::failure(written.status());
    }
    return Result<PointSet>::success(std::move(result));
}

Result<PointSet> selectPointFloatAttribute(const PointSet& input, const std::string& conditionAttribute,
                                           const std::string& trueAttribute, const std::string& falseAttribute,
                                           const std::string& outputAttribute, float trueDefault,
                                           float falseDefault) {
    if (conditionAttribute.empty() || !isMetadataName(conditionAttribute))
        return Result<PointSet>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "select condition must be a metadata bool attribute",
            "conditionAttribute"));
    if (!isPointFloatChannel(trueAttribute) || !isPointFloatChannel(falseAttribute) ||
        !isPointFloatChannel(outputAttribute))
        return Result<PointSet>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "select float channels are invalid", "attribute"));

    PointSet result = input;
    for (int index = 0; index < result.getCount(); ++index) {
        const bool  takeTrue = result.getBoolAttribute(index, conditionAttribute, false);
        const float value    = readPointFloatChannel(result, index, takeTrue ? trueAttribute : falseAttribute,
                                                     takeTrue ? trueDefault : falseDefault)
                                .expect("selectPointFloatAttribute channel");
        auto written = writePointFloatChannel(result, index, outputAttribute, value);
        if (!written.ok()) return Result<PointSet>::failure(written.status());
    }
    return Result<PointSet>::success(std::move(result));
}

}  // namespace eve::procgen
