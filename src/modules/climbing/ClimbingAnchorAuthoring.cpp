#include "climbing/ClimbingAnchorAuthoring.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <limits>
#include <sstream>
#include <unordered_set>

namespace eve::climbing {
namespace {

template <typename T>
eve::Result<T> failure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path), {}, "climbing.anchor_baker"));
}

bool finite(float value) { return std::isfinite(value); }
bool finite(Vec3 value) { return finite(value.x) && finite(value.y) && finite(value.z); }
Vec3 add(Vec3 lhs, Vec3 rhs) { return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z}; }
Vec3 subtract(Vec3 lhs, Vec3 rhs) { return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z}; }
Vec3 multiply(Vec3 value, float scale) { return {value.x * scale, value.y * scale, value.z * scale}; }
float dot(Vec3 lhs, Vec3 rhs) { return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z; }
Vec3 cross(Vec3 lhs, Vec3 rhs) {
    return {lhs.y * rhs.z - lhs.z * rhs.y, lhs.z * rhs.x - lhs.x * rhs.z,
            lhs.x * rhs.y - lhs.y * rhs.x};
}
float length(Vec3 value) { return std::sqrt(dot(value, value)); }

eve::Result<Vec3> normalized(Vec3 value, std::string path) {
    if (!finite(value)) return failure<Vec3>(eve::DiagnosticCode::InvalidArgument, "vector must be finite", path);
    const float magnitude = length(value);
    if (magnitude <= 0.0001f)
        return failure<Vec3>(eve::DiagnosticCode::InvalidArgument, "vector must be non-zero", path);
    return eve::Result<Vec3>::success(multiply(value, 1.f / magnitude));
}

std::string hexHash(const ClimbingAnchorBakeRequest& request) {
    std::uint64_t hash = 1469598103934665603ull;
    const auto append = [&hash](std::string_view value) {
        for (const unsigned char byte : value) {
            hash ^= byte;
            hash *= 1099511628211ull;
        }
    };
    const auto appendNumber = [&append](auto value) {
        std::array<char, 64> buffer{};
        const int count = std::snprintf(buffer.data(), buffer.size(), "%.9g", static_cast<double>(value));
        if (count > 0) append(std::string_view(buffer.data(), static_cast<std::size_t>(count)));
        append("|");
    };
    append("evengine.climbing-anchor-bake-v1|");
    append(request.sourceGeometryContentId);
    append("|");
    appendNumber(request.settings.handSpacing);
    appendNumber(request.settings.hangingFeetDrop);
    appendNumber(request.settings.ladderMountOffset);
    appendNumber(request.settings.occupancySlots);
    std::vector<const ClimbingLedgeBakeSource*> ledges;
    ledges.reserve(request.ledges.size());
    for (const auto& ledge : request.ledges) ledges.push_back(&ledge);
    std::sort(ledges.begin(), ledges.end(), [](const auto* lhs, const auto* rhs) {
        return lhs->id < rhs->id;
    });
    for (const auto* source : ledges) {
        const auto& ledge = *source;
        append(ledge.id);
        appendNumber(ledge.closed ? 1 : 0);
        appendNumber(ledge.localNormal.x);
        appendNumber(ledge.localNormal.y);
        appendNumber(ledge.localNormal.z);
        for (const Vec3 normal : ledge.localNormals) {
            appendNumber(normal.x);
            appendNumber(normal.y);
            appendNumber(normal.z);
        }
        for (const Vec3 point : ledge.points) {
            appendNumber(point.x);
            appendNumber(point.y);
            appendNumber(point.z);
        }
        auto tags = ledge.tags;
        std::sort(tags.begin(), tags.end());
        for (const auto& tag : tags) append(tag);
    }
    std::vector<const ClimbingLadderBakeSource*> ladders;
    ladders.reserve(request.ladders.size());
    for (const auto& ladder : request.ladders) ladders.push_back(&ladder);
    std::sort(ladders.begin(), ladders.end(), [](const auto* lhs, const auto* rhs) {
        return lhs->id < rhs->id;
    });
    for (const auto* source : ladders) {
        const auto& ladder = *source;
        append(ladder.id);
        appendNumber(ladder.bottomCenter.x);
        appendNumber(ladder.bottomCenter.y);
        appendNumber(ladder.bottomCenter.z);
        appendNumber(ladder.localUp.x);
        appendNumber(ladder.localUp.y);
        appendNumber(ladder.localUp.z);
        appendNumber(ladder.localNormal.x);
        appendNumber(ladder.localNormal.y);
        appendNumber(ladder.localNormal.z);
        appendNumber(ladder.rungSpacing);
        appendNumber(ladder.width);
        appendNumber(ladder.rungCount);
        auto tags = ladder.tags;
        std::sort(tags.begin(), tags.end());
        for (const auto& tag : tags) append(tag);
    }
    std::ostringstream output;
    output << "fnv1a64:" << std::hex << std::setw(16) << std::setfill('0') << hash;
    return output.str();
}

bool uniqueNonEmpty(std::vector<std::string> values) {
    std::sort(values.begin(), values.end());
    return !std::any_of(values.begin(), values.end(), [](const auto& value) { return value.empty(); }) &&
           std::adjacent_find(values.begin(), values.end()) == values.end();
}

const eve::Value* field(const eve::Value::Object& object, std::string_view name) {
    const auto found = object.find(std::string(name));
    return found == object.end() ? nullptr : &found->second;
}

bool readString(const eve::Value::Object& object, std::string_view name, std::string& output) {
    const auto* value = field(object, name);
    const auto* text = value ? value->getIf<std::string>() : nullptr;
    if (!text) return false;
    output = *text;
    return true;
}

bool readFloat(const eve::Value::Object& object, std::string_view name, float& output) {
    const auto* value = field(object, name);
    if (!value) return false;
    double number = 0.0;
    if (const auto* integer = value->getIf<std::int64_t>())
        number = static_cast<double>(*integer);
    else if (const auto* real = value->getIf<double>())
        number = *real;
    else
        return false;
    if (!std::isfinite(number) || number < -std::numeric_limits<float>::max() ||
        number > std::numeric_limits<float>::max())
        return false;
    output = static_cast<float>(number);
    return true;
}

bool readUInt32(const eve::Value::Object& object, std::string_view name, std::uint32_t& output) {
    const auto* value = field(object, name);
    const auto* integer = value ? value->getIf<std::int64_t>() : nullptr;
    if (!integer || *integer < 0 || static_cast<std::uint64_t>(*integer) > std::numeric_limits<std::uint32_t>::max())
        return false;
    output = static_cast<std::uint32_t>(*integer);
    return true;
}

bool readBool(const eve::Value::Object& object, std::string_view name, bool& output) {
    const auto* value = field(object, name);
    const auto* boolean = value ? value->getIf<bool>() : nullptr;
    if (!boolean) return false;
    output = *boolean;
    return true;
}

bool readVec(const eve::Value& value, Vec3& output) {
    const auto* array = value.getIf<eve::Value::Array>();
    if (!array || array->size() != 3) return false;
    eve::Value::Object object{{"x", (*array)[0]}, {"y", (*array)[1]}, {"z", (*array)[2]}};
    return readFloat(object, "x", output.x) && readFloat(object, "y", output.y) &&
           readFloat(object, "z", output.z);
}

bool readVecField(const eve::Value::Object& object, std::string_view name, Vec3& output) {
    const auto* value = field(object, name);
    return value && readVec(*value, output);
}

bool readVecArray(const eve::Value::Object& object, std::string_view name, std::vector<Vec3>& output) {
    const auto* value = field(object, name);
    const auto* array = value ? value->getIf<eve::Value::Array>() : nullptr;
    if (!array) return false;
    output.reserve(array->size());
    for (const auto& item : *array) {
        Vec3 vector;
        if (!readVec(item, vector)) return false;
        output.push_back(vector);
    }
    return true;
}

bool readStrings(const eve::Value::Object& object, std::string_view name, std::vector<std::string>& output) {
    const auto* value = field(object, name);
    const auto* array = value ? value->getIf<eve::Value::Array>() : nullptr;
    if (!array) return false;
    output.reserve(array->size());
    for (const auto& item : *array) {
        const auto* text = item.getIf<std::string>();
        if (!text) return false;
        output.push_back(*text);
    }
    return true;
}

eve::Value vecValue(Vec3 value) {
    return eve::Value::array({eve::Value(value.x), eve::Value(value.y), eve::Value(value.z)});
}

eve::Value vecArrayValue(const std::vector<Vec3>& values) {
    eve::Value::Array result;
    result.reserve(values.size());
    for (const Vec3 value : values) result.push_back(vecValue(value));
    return eve::Value(std::move(result));
}

eve::Value stringArrayValue(const std::vector<std::string>& values) {
    eve::Value::Array result;
    result.reserve(values.size());
    for (const auto& value : values) result.emplace_back(value);
    return eve::Value(std::move(result));
}

ClimbingAnchorNodeDefinition ledgeNode(std::string id, ClimbingAnchorKind kind, Vec3 position, Vec3 normal,
                                       Vec3 tangent, const ClimbingAnchorBakeSettings& settings,
                                       std::vector<std::string> tags) {
    ClimbingAnchorNodeDefinition node;
    node.id = std::move(id);
    node.kind = kind;
    node.localPosition = position;
    node.localNormal = normal;
    node.localTangent = tangent;
    node.leftHandSocket = add(position, multiply(tangent, -settings.handSpacing * 0.5f));
    node.rightHandSocket = add(position, multiply(tangent, settings.handSpacing * 0.5f));
    node.feetSocket = add(position, {0.f, -settings.hangingFeetDrop, 0.f});
    node.occupancySlots = settings.occupancySlots;
    node.tags = std::move(tags);
    return node;
}

}  // namespace

eve::Result<eve::Value> encodeClimbingAnchorBakeRequest(const ClimbingAnchorBakeRequest& request) {
    eve::Value::Array ledges;
    ledges.reserve(request.ledges.size());
    for (const auto& source : request.ledges)
        ledges.push_back(eve::Value::object({
            {"id", source.id},
            {"points", vecArrayValue(source.points)},
            {"localNormal", vecValue(source.localNormal)},
            {"localNormals", vecArrayValue(source.localNormals)},
            {"closed", source.closed},
            {"tags", stringArrayValue(source.tags)},
        }));
    eve::Value::Array ladders;
    ladders.reserve(request.ladders.size());
    for (const auto& source : request.ladders)
        ladders.push_back(eve::Value::object({
            {"id", source.id},
            {"bottomCenter", vecValue(source.bottomCenter)},
            {"localUp", vecValue(source.localUp)},
            {"localNormal", vecValue(source.localNormal)},
            {"rungSpacing", source.rungSpacing},
            {"width", source.width},
            {"rungCount", static_cast<std::int64_t>(source.rungCount)},
            {"tags", stringArrayValue(source.tags)},
        }));
    return eve::Result<eve::Value>::success(eve::Value::object({
        {"schemaId", std::string(ClimbingAnchorBakeRequest::SchemaId)},
        {"schemaVersion", ClimbingAnchorBakeRequest::SchemaVersion},
        {"graphId", request.graphId},
        {"sourceGeometryContentId", request.sourceGeometryContentId},
        {"settings", eve::Value::object({
                         {"handSpacing", request.settings.handSpacing},
                         {"hangingFeetDrop", request.settings.hangingFeetDrop},
                         {"ladderMountOffset", request.settings.ladderMountOffset},
                         {"occupancySlots", static_cast<std::int64_t>(request.settings.occupancySlots)},
                     })},
        {"ledges", eve::Value(std::move(ledges))},
        {"ladders", eve::Value(std::move(ladders))},
    }));
}

eve::Result<ClimbingAnchorBakeRequest> decodeClimbingAnchorBakeRequest(const eve::Value& value) {
    const auto* root = value.getIf<eve::Value::Object>();
    if (!root)
        return failure<ClimbingAnchorBakeRequest>(eve::DiagnosticCode::ParseError,
                                                   "anchor bake request must be an object");
    ClimbingAnchorBakeRequest request;
    std::string schemaId;
    std::int64_t schemaVersion = -1;
    const auto* schema = field(*root, "schemaVersion");
    if (schema) {
        if (const auto* integer = schema->getIf<std::int64_t>()) schemaVersion = *integer;
    }
    const auto* settingsValue = field(*root, "settings");
    const auto* settings = settingsValue ? settingsValue->getIf<eve::Value::Object>() : nullptr;
    const auto* ledgesValue = field(*root, "ledges");
    const auto* ledges = ledgesValue ? ledgesValue->getIf<eve::Value::Array>() : nullptr;
    const auto* laddersValue = field(*root, "ladders");
    const auto* ladders = laddersValue ? laddersValue->getIf<eve::Value::Array>() : nullptr;
    if (!readString(*root, "schemaId", schemaId) || schemaId != ClimbingAnchorBakeRequest::SchemaId ||
        schemaVersion != ClimbingAnchorBakeRequest::SchemaVersion || !readString(*root, "graphId", request.graphId) ||
        !readString(*root, "sourceGeometryContentId", request.sourceGeometryContentId) || !settings || !ledges ||
        !ladders || !readFloat(*settings, "handSpacing", request.settings.handSpacing) ||
        !readFloat(*settings, "hangingFeetDrop", request.settings.hangingFeetDrop) ||
        !readFloat(*settings, "ladderMountOffset", request.settings.ladderMountOffset) ||
        !readUInt32(*settings, "occupancySlots", request.settings.occupancySlots)) {
        if (schemaVersion > ClimbingAnchorBakeRequest::SchemaVersion)
            return failure<ClimbingAnchorBakeRequest>(eve::DiagnosticCode::UnknownVersion,
                                                       "anchor bake request schema version is unsupported",
                                                       "schemaVersion");
        return failure<ClimbingAnchorBakeRequest>(eve::DiagnosticCode::ParseError,
                                                   "anchor bake request has missing or invalid fields");
    }
    request.ledges.reserve(ledges->size());
    for (std::size_t index = 0; index < ledges->size(); ++index) {
        const auto* object = (*ledges)[index].getIf<eve::Value::Object>();
        ClimbingLedgeBakeSource source;
        if (!object || !readString(*object, "id", source.id) || !readVecArray(*object, "points", source.points) ||
            !readVecField(*object, "localNormal", source.localNormal) ||
            !readVecArray(*object, "localNormals", source.localNormals) ||
            !readBool(*object, "closed", source.closed) || !readStrings(*object, "tags", source.tags))
            return failure<ClimbingAnchorBakeRequest>(eve::DiagnosticCode::ParseError,
                                                       "ledge bake source has missing or invalid fields",
                                                       "ledges." + std::to_string(index));
        request.ledges.push_back(std::move(source));
    }
    request.ladders.reserve(ladders->size());
    for (std::size_t index = 0; index < ladders->size(); ++index) {
        const auto* object = (*ladders)[index].getIf<eve::Value::Object>();
        ClimbingLadderBakeSource source;
        if (!object || !readString(*object, "id", source.id) ||
            !readVecField(*object, "bottomCenter", source.bottomCenter) ||
            !readVecField(*object, "localUp", source.localUp) ||
            !readVecField(*object, "localNormal", source.localNormal) ||
            !readFloat(*object, "rungSpacing", source.rungSpacing) || !readFloat(*object, "width", source.width) ||
            !readUInt32(*object, "rungCount", source.rungCount) || !readStrings(*object, "tags", source.tags))
            return failure<ClimbingAnchorBakeRequest>(eve::DiagnosticCode::ParseError,
                                                       "ladder bake source has missing or invalid fields",
                                                       "ladders." + std::to_string(index));
        request.ladders.push_back(std::move(source));
    }
    return eve::Result<ClimbingAnchorBakeRequest>::success(std::move(request));
}

eve::Result<ClimbingAnchorBakeResult> bakeClimbingAnchorGraph(const ClimbingAnchorBakeRequest& request) {
    if (request.graphId.empty() || request.sourceGeometryContentId.empty())
        return failure<ClimbingAnchorBakeResult>(eve::DiagnosticCode::InvalidArgument,
                                                  "graph and source geometry ids are required", "identity");
    if (!finite(request.settings.handSpacing) || request.settings.handSpacing <= 0.f ||
        !finite(request.settings.hangingFeetDrop) || request.settings.hangingFeetDrop <= 0.f ||
        !finite(request.settings.ladderMountOffset) || request.settings.ladderMountOffset < 0.f ||
        request.settings.occupancySlots == 0 || request.settings.occupancySlots > 8)
        return failure<ClimbingAnchorBakeResult>(eve::DiagnosticCode::InvalidArgument,
                                                  "bake settings are outside supported bounds", "settings");
    if (request.ledges.empty() && request.ladders.empty())
        return failure<ClimbingAnchorBakeResult>(eve::DiagnosticCode::InvalidArgument,
                                                  "at least one ledge or ladder source is required", "sources");

    ClimbingAnchorBakeResult result;
    result.graph.id = request.graphId;
    result.graph.sourceGeometryContentId = request.sourceGeometryContentId;
    result.graph.buildSettingsHash = hexHash(request);
    std::unordered_set<std::string> sourceIds;

    for (std::size_t sourceIndex = 0; sourceIndex < request.ledges.size(); ++sourceIndex) {
        const auto& source = request.ledges[sourceIndex];
        const std::string path = "ledges." + std::to_string(sourceIndex);
        if (source.id.empty() || !sourceIds.emplace(source.id).second || source.points.size() < 2 ||
            (!source.localNormals.empty() && source.localNormals.size() != source.points.size()) ||
            !uniqueNonEmpty(source.tags))
            return failure<ClimbingAnchorBakeResult>(eve::DiagnosticCode::InvalidArgument,
                                                      "ledge id/tags/normals are invalid or it needs two points", path);
        std::vector<std::string> ids;
        ids.reserve(source.points.size());
        for (std::size_t index = 0; index < source.points.size(); ++index) {
            if (!finite(source.points[index]))
                return failure<ClimbingAnchorBakeResult>(eve::DiagnosticCode::InvalidArgument,
                                                          "ledge points must be finite", path + ".points");
            const std::size_t previous = index == 0 ? (source.closed ? source.points.size() - 1 : 0) : index - 1;
            const std::size_t next = index + 1 == source.points.size() ? (source.closed ? 0 : index) : index + 1;
            const Vec3 normalValue = source.localNormals.empty() ? source.localNormal : source.localNormals[index];
            auto normalResult = normalized(normalValue, path + ".localNormals." + std::to_string(index));
            if (!normalResult) return eve::Result<ClimbingAnchorBakeResult>::failure(normalResult.status());
            const Vec3 normal = normalResult.value();
            const Vec3 segment = index + 1 < source.points.size() || source.closed
                                     ? subtract(source.points[next], source.points[index])
                                     : subtract(source.points[index], source.points[previous]);
            auto tangentResult = normalized(segment,
                                            path + ".points." + std::to_string(index));
            if (!tangentResult) return eve::Result<ClimbingAnchorBakeResult>::failure(tangentResult.status());
            Vec3 tangent = tangentResult.value();
            tangent = subtract(tangent, multiply(normal, dot(tangent, normal)));
            tangentResult = normalized(tangent, path + ".frame." + std::to_string(index));
            if (!tangentResult) return eve::Result<ClimbingAnchorBakeResult>::failure(tangentResult.status());
            tangent = tangentResult.value();
            ClimbingAnchorKind kind = ClimbingAnchorKind::Ledge;
            if (index != 0 && (index + 1 != source.points.size() || source.closed)) {
                auto incoming = normalized(subtract(source.points[index], source.points[previous]), path);
                auto outgoing = normalized(subtract(source.points[next], source.points[index]), path);
                if (!incoming) return eve::Result<ClimbingAnchorBakeResult>::failure(incoming.status());
                if (!outgoing) return eve::Result<ClimbingAnchorBakeResult>::failure(outgoing.status());
                const float turn = cross(incoming.value(), outgoing.value()).y;
                if (std::fabs(turn) > 0.05f)
                    kind = turn > 0.f ? ClimbingAnchorKind::CornerOuter : ClimbingAnchorKind::CornerInner;
            }
            const std::string id = source.id + "." + std::to_string(index);
            ids.push_back(id);
            result.graph.nodes.push_back(ledgeNode(id, kind, source.points[index], normal, tangent,
                                                   request.settings, source.tags));
            ++result.ledgeNodeCount;
        }
        const std::size_t edgeCount = source.closed ? ids.size() : ids.size() - 1;
        for (std::size_t index = 0; index < edgeCount; ++index) {
            const std::size_t next = (index + 1) % ids.size();
            const auto& target = result.graph.nodes[result.graph.nodes.size() - ids.size() + next];
            result.graph.edges.push_back({ids[index], ids[next],
                                          target.kind == ClimbingAnchorKind::Ledge
                                              ? ClimbingAnchorEdgeKind::Shimmy
                                              : ClimbingAnchorEdgeKind::Corner,
                                          true, {}, {}});
        }
    }

    for (std::size_t sourceIndex = 0; sourceIndex < request.ladders.size(); ++sourceIndex) {
        const auto& source = request.ladders[sourceIndex];
        const std::string path = "ladders." + std::to_string(sourceIndex);
        if (source.id.empty() || !sourceIds.emplace(source.id).second || !finite(source.bottomCenter) ||
            !finite(source.rungSpacing) || source.rungSpacing <= 0.f || !finite(source.width) ||
            source.width <= 0.f || source.rungCount < 2 || source.rungCount > 1024 ||
            !uniqueNonEmpty(source.tags))
            return failure<ClimbingAnchorBakeResult>(eve::DiagnosticCode::InvalidArgument,
                                                      "ladder source is outside supported bounds", path);
        auto upResult = normalized(source.localUp, path + ".localUp");
        auto normalResult = normalized(source.localNormal, path + ".localNormal");
        if (!upResult) return eve::Result<ClimbingAnchorBakeResult>::failure(upResult.status());
        if (!normalResult) return eve::Result<ClimbingAnchorBakeResult>::failure(normalResult.status());
        const Vec3 up = upResult.value();
        const Vec3 normal = normalResult.value();
        auto tangentResult = normalized(cross(up, normal), path + ".frame");
        if (!tangentResult) return eve::Result<ClimbingAnchorBakeResult>::failure(tangentResult.status());
        const Vec3 tangent = tangentResult.value();
        std::vector<std::string> ids;
        ids.reserve(source.rungCount);
        for (std::uint32_t index = 0; index < source.rungCount; ++index) {
            ClimbingAnchorNodeDefinition node;
            node.id = source.id + ".rung." + std::to_string(index);
            node.kind = ClimbingAnchorKind::LadderRung;
            node.localPosition = add(source.bottomCenter, multiply(up, source.rungSpacing * static_cast<float>(index)));
            node.localNormal = normal;
            node.localTangent = tangent;
            node.leftHandSocket = add(node.localPosition, multiply(tangent, -source.width * 0.5f));
            node.rightHandSocket = add(node.localPosition, multiply(tangent, source.width * 0.5f));
            node.feetSocket = add(node.localPosition, multiply(up, -source.rungSpacing * 2.f));
            node.occupancySlots = request.settings.occupancySlots;
            node.tags = source.tags;
            node.tags.push_back("ladder");
            std::sort(node.tags.begin(), node.tags.end());
            node.tags.erase(std::unique(node.tags.begin(), node.tags.end()), node.tags.end());
            ids.push_back(node.id);
            result.graph.nodes.push_back(std::move(node));
            ++result.ladderNodeCount;
        }
        auto endpointTags = source.tags;
        endpointTags.push_back("ladder_mount");
        std::sort(endpointTags.begin(), endpointTags.end());
        endpointTags.erase(std::unique(endpointTags.begin(), endpointTags.end()), endpointTags.end());
        const std::string mountId = source.id + ".mount";
        result.graph.nodes.push_back(ledgeNode(
            mountId, ClimbingAnchorKind::Ledge,
            add(source.bottomCenter, multiply(normal, request.settings.ladderMountOffset)), normal, tangent,
            request.settings, endpointTags));
        endpointTags = source.tags;
        endpointTags.push_back("ladder_dismount");
        std::sort(endpointTags.begin(), endpointTags.end());
        endpointTags.erase(std::unique(endpointTags.begin(), endpointTags.end()), endpointTags.end());
        const std::string dismountId = source.id + ".dismount";
        const Vec3 top = add(source.bottomCenter,
                             multiply(up, source.rungSpacing * static_cast<float>(source.rungCount - 1)));
        result.graph.nodes.push_back(ledgeNode(
            dismountId, ClimbingAnchorKind::Ledge,
            add(add(top, multiply(up, request.settings.ladderMountOffset)),
                multiply(normal, request.settings.ladderMountOffset)),
            normal, tangent, request.settings, endpointTags));
        result.graph.edges.push_back({mountId, ids.front(), ClimbingAnchorEdgeKind::Mount, false, {}, {}});
        for (std::size_t index = 0; index + 1 < ids.size(); ++index)
            result.graph.edges.push_back({ids[index], ids[index + 1], ClimbingAnchorEdgeKind::Climb, true, {}, {}});
        result.graph.edges.push_back(
            {ids.back(), dismountId, ClimbingAnchorEdgeKind::Dismount, false, {}, {}});
    }

    auto valid = validateClimbingAnchorGraphDefinition(result.graph);
    if (!valid) return eve::Result<ClimbingAnchorBakeResult>::failure(valid.status());
    auto encoded = encodeClimbingAnchorGraphDefinition(result.graph);
    if (!encoded) return eve::Result<ClimbingAnchorBakeResult>::failure(encoded.status());
    auto canonical = decodeClimbingAnchorGraphDefinition(encoded.value());
    if (!canonical) return eve::Result<ClimbingAnchorBakeResult>::failure(canonical.status());
    result.graph = std::move(canonical.value());
    return eve::Result<ClimbingAnchorBakeResult>::success(std::move(result));
}

eve::Result<ClimbingAnchorAuthoringOverlay> inspectClimbingAnchorGraphAuthoring(
    const ClimbingAnchorGraphDefinition& graph) {
    auto valid = validateClimbingAnchorGraphDefinition(graph);
    if (!valid) return eve::Result<ClimbingAnchorAuthoringOverlay>::failure(valid.status());
    ClimbingAnchorAuthoringOverlay overlay;
    overlay.graphId = graph.id;
    overlay.buildSettingsHash = graph.buildSettingsHash;
    overlay.nodes.reserve(graph.nodes.size());
    for (const auto& node : graph.nodes)
        overlay.nodes.push_back({node.id, node.kind, node.localPosition, node.localNormal, node.localTangent,
                                 node.leftHandSocket, node.rightHandSocket, node.feetSocket,
                                 node.occupancySlots});
    overlay.edges.reserve(graph.edges.size());
    for (const auto& edge : graph.edges)
        overlay.edges.push_back({edge.from, edge.to, edge.kind, edge.bidirectional});
    std::sort(overlay.nodes.begin(), overlay.nodes.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.id < rhs.id; });
    std::sort(overlay.edges.begin(), overlay.edges.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.from != rhs.from) return lhs.from < rhs.from;
        if (lhs.to != rhs.to) return lhs.to < rhs.to;
        return lhs.kind < rhs.kind;
    });
    return eve::Result<ClimbingAnchorAuthoringOverlay>::success(std::move(overlay));
}

}  // namespace eve::climbing
