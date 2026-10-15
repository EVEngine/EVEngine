#include <algorithm>
#include <bit>
#include <cmath>
#include <iomanip>
#include <limits>
#include <new>
#include <sstream>
#include "asset/CanonicalMesh.h"
#include "asset/CanonicalPcgCook.h"
#include "asset/CanonicalVolumeTextureCook.h"
#include "common/Value.h"

// CanonicalMeshEncode.cpp
namespace eve::asset {
Result<std::vector<std::uint8_t>> encodeCanonicalMesh(const CanonicalMeshData&   mesh,
                                                      const CanonicalMeshLimits& limits) {
    using Output = Result<std::vector<std::uint8_t>>;
    auto invalid = [] {
        return Output::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                 "invalid canonical mesh or byte budget", {}, {}, "asset.mesh"));
    };
    const auto count = mesh.positions.size() / 3;
    if (!count || mesh.positions.size() % 3 || count > limits.maximumVertices || mesh.indices.empty() ||
        mesh.indices.size() % 3 || mesh.indices.size() > limits.maximumIndices ||
        (!mesh.normals.empty() && mesh.normals.size() != mesh.positions.size()) ||
        (!mesh.colors.empty() && mesh.colors.size() != count * 4) ||
        (!mesh.colors.empty() && !mesh.attributes.empty()) || mesh.texcoords.size() > UINT32_MAX ||
        mesh.attributes.size() > UINT32_MAX)
        return invalid();
    const bool extended = !mesh.attributes.empty();
    uint64_t   floats =
        3 + (mesh.normals.empty() ? 0 : 3) + uint64_t(mesh.texcoords.size()) * 2 + (mesh.colors.empty() ? 0 : 4);
    auto validValues = [](const auto& values) {
        return std::all_of(values.begin(), values.end(), [](float v) { return std::isfinite(v); });
    };
    if (!validValues(mesh.positions) || !validValues(mesh.normals) || !validValues(mesh.colors)) return invalid();
    for (const auto& [set, values] : mesh.texcoords)
        if (values.size() != count * 2 || !validValues(values)) return invalid();
    for (const auto& [name, attribute] : mesh.attributes) {
        if (name.empty() || name.size() > 63 || name == "POSITION" || name == "NORMAL" ||
            name.starts_with("TEXCOORD_") || !attribute.components || attribute.components > 4 ||
            attribute.values.size() != count * attribute.components || !validValues(attribute.values) ||
            !std::all_of(name.begin(), name.end(),
                         [](char c) { return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }))
            return invalid();
        floats += attribute.components;
    }
    for (auto index : mesh.indices)
        if (index >= count) return invalid();
    const uint64_t header =
        (extended ? 28ull : 24ull) + uint64_t(mesh.texcoords.size()) * 4 + uint64_t(mesh.attributes.size()) * 72;
    const uint64_t indexBytes = uint64_t(mesh.indices.size()) * 4;
    if (header > limits.maximumDecodedBytes || indexBytes > limits.maximumDecodedBytes - header ||
        floats > (limits.maximumDecodedBytes - header - indexBytes) / 4 / count)
        return invalid();
    const uint64_t size = header + uint64_t(count) * floats * 4 + indexBytes;
    if (size > std::numeric_limits<size_t>::max()) return invalid();
    try {
        std::vector<uint8_t> bytes;
        bytes.reserve(size_t(size));
        bytes.insert(bytes.end(), {'E', 'V', 'M', 'E', 'S', 'H', 0, uint8_t(extended || !mesh.colors.empty() ? 3 : 2)});
        auto put = [&](uint32_t value) {
            for (unsigned i = 0; i < 4; ++i) bytes.push_back(uint8_t(value >> (i * 8)));
        };
        put(uint32_t(count));
        put(uint32_t(mesh.indices.size()));
        put((mesh.normals.empty() ? 0u : 1u) | (mesh.colors.empty() ? 0u : 2u));
        put(uint32_t(mesh.texcoords.size()));
        if (extended) put(uint32_t(mesh.attributes.size()));
        for (const auto& [set, values] : mesh.texcoords) put(set);
        for (const auto& [name, attribute] : mesh.attributes) {
            bytes.insert(bytes.end(), name.begin(), name.end());
            bytes.insert(bytes.end(), 64 - name.size(), 0);
            put(attribute.components);
            put(0);
        }
        auto stream = [&](const auto& values, size_t vertex, size_t components) {
            for (size_t j = 0; j < components; ++j) put(std::bit_cast<uint32_t>(values[vertex * components + j]));
        };
        for (size_t v = 0; v < count; ++v) {
            stream(mesh.positions, v, 3);
            if (!mesh.normals.empty()) stream(mesh.normals, v, 3);
            for (const auto& [set, values] : mesh.texcoords) stream(values, v, 2);
            for (const auto& [name, attribute] : mesh.attributes) stream(attribute.values, v, attribute.components);
            if (!mesh.colors.empty()) stream(mesh.colors, v, 4);
        }
        for (auto index : mesh.indices) put(index);
        return Output::success(std::move(bytes));
    } catch (const std::bad_alloc&) {
        return Output::failure(
            Diagnostic::error(DiagnosticCode::Failed, "canonical mesh allocation failed", {}, {}, "asset.mesh"));
    }
}
}  // namespace eve::asset

// CanonicalPcgCook.cpp
namespace eve::asset {
namespace {

const Value* field(const Value::Object& object, std::string_view name) {
    const auto found = object.find(std::string(name));
    return found == object.end() ? nullptr : &found->second;
}

bool number(const Value* value, double& output) {
    if (!value || !value->isNumeric()) return false;
    output = value->isInt64() ? double(value->asInt()) : value->asDouble();
    return std::isfinite(output);
}

void node(std::ostringstream& out, std::string_view id, std::string_view operation) {
    out << "NODE " << std::quoted(std::string(id)) << ' ' << std::quoted(std::string(operation)) << '\n';
}

void edge(std::ostringstream& out, std::string_view from, std::string_view to) {
    out << "EDGE " << std::quoted(std::string(from)) << ' ' << std::quoted(std::string(to)) << " 0\n";
}

}  // namespace

Result<CookedCanonicalPcgGraph> cookCanonicalPcgGraph(std::span<const std::uint8_t> definition,
                                                      std::uint32_t maximumRules, std::uint64_t maximumPlanBytes) {
    auto parsed =
        Value::fromJson(std::string_view(reinterpret_cast<const char*>(definition.data()), definition.size()));
    if (!parsed) return Result<CookedCanonicalPcgGraph>::failure(parsed.status());
    const auto*  root       = parsed.value().getIf<Value::Object>();
    const Value* schema     = root ? field(*root, "schema") : nullptr;
    const Value* version    = root ? field(*root, "schemaVersion") : nullptr;
    const Value* algorithm  = root ? field(*root, "algorithm") : nullptr;
    const Value* rulesValue = root ? field(*root, "rules") : nullptr;
    const auto*  rules      = rulesValue ? rulesValue->getIf<Value::Array>() : nullptr;
    if (!schema || !schema->isString() || schema->asString() != "eve.pcg-graph" || !version || !version->isInt64() ||
        version->asInt() != 1 || !algorithm || !algorithm->isString() ||
        algorithm->asString() != "terrain-layer-scatter-v1" || !rules)
        return Result<CookedCanonicalPcgGraph>::failure(Diagnostic::error(
            DiagnosticCode::Unsupported, "unsupported eve.pcg-graph/1 definition", {}, {}, "asset.cook.pcg"));
    if (rules->empty() || rules->size() > maximumRules)
        return Result<CookedCanonicalPcgGraph>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "PCG rule count exceeds Cook limits", "$.rules", {}, "asset.cook.pcg"));

    std::ostringstream plan;
    plan << "EVPCG_POINT_GRAPH 1\n" << std::setprecision(9);
    std::vector<std::string> outputs;
    Value::Array             slots;
    constexpr double         radiansToDegrees = 57.29577951308232;
    for (std::size_t i = 0; i < rules->size(); ++i) {
        const auto*  rule      = (*rules)[i].getIf<Value::Object>();
        const Value* id        = rule ? field(*rule, "id") : nullptr;
        const Value* prototype = rule ? field(*rule, "prototype") : nullptr;
        const Value* layer     = rule ? field(*rule, "layer") : nullptr;
        const Value* seed      = rule ? field(*rule, "seed") : nullptr;
        double       density = 0, minimumSlope = 0, maximumSlope = 0;
        if (!id || !id->isString() || id->asString().empty() || !prototype || !prototype->isString() ||
            prototype->asString().empty() || !layer || !layer->isString() || !seed || !seed->isInt64() ||
            seed->asInt() < 0 || !number(rule ? field(*rule, "densityPerSquareMeter") : nullptr, density) ||
            density < 0 || !number(rule ? field(*rule, "minimumSlopeRadians") : nullptr, minimumSlope) ||
            !number(rule ? field(*rule, "maximumSlopeRadians") : nullptr, maximumSlope) || minimumSlope < 0 ||
            maximumSlope < minimumSlope || maximumSlope > 3.141592653589793)
            return Result<CookedCanonicalPcgGraph>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "PCG scatter rule is invalid",
                                  "$.rules[" + std::to_string(i) + "]", {}, "asset.cook.pcg"));
        const std::string suffix          = std::to_string(i);
        const std::string sample          = "sample." + suffix;
        const std::string project         = "project." + suffix;
        const std::string slope           = "slope." + suffix;
        const std::string taggedPrototype = "prototype." + suffix;
        const std::string taggedLayer     = "layer." + suffix;
        node(plan, sample, "spatial.sample");
        plan << "FLOAT " << std::quoted(sample) << " \"spacing\" " << (density > 0 ? std::sqrt(1.0 / density) : 1.0)
             << '\n';
        plan << "INT " << std::quoted(sample) << " \"seed\" " << (seed->asInt() & 0x7fffffff) << '\n';
        node(plan, project, "spatial.project");
        edge(plan, sample, project);
        node(plan, slope, "filter.slope");
        edge(plan, project, slope);
        plan << "FLOAT " << std::quoted(slope) << " \"minDegrees\" " << minimumSlope * radiansToDegrees << '\n';
        plan << "FLOAT " << std::quoted(slope) << " \"maxDegrees\" " << maximumSlope * radiansToDegrees << '\n';
        std::string scatterOutput = slope;
        if (density == 0) {
            const std::string culled = "cull." + suffix;
            node(plan, culled, "density.cull");
            edge(plan, slope, culled);
            plan << "FLOAT " << std::quoted(culled) << " \"multiplier\" 0\n";
            scatterOutput = culled;
        }
        node(plan, taggedPrototype, "attribute.set.string");
        edge(plan, scatterOutput, taggedPrototype);
        plan << "STRING " << std::quoted(taggedPrototype) << " \"attribute\" \"prototype\"\n"
             << "STRING " << std::quoted(taggedPrototype) << " \"value\" " << std::quoted(prototype->asString())
             << '\n';
        node(plan, taggedLayer, "attribute.set.string");
        edge(plan, taggedPrototype, taggedLayer);
        plan << "STRING " << std::quoted(taggedLayer) << " \"attribute\" \"layer\"\n"
             << "STRING " << std::quoted(taggedLayer) << " \"value\" " << std::quoted(layer->asString()) << '\n';
        outputs.push_back(taggedLayer);
        slots.emplace_back(Value::Object{{"node", Value(sample)}, {"role", Value("terrain")}});
        slots.emplace_back(Value::Object{{"node", Value(project)}, {"role", Value("terrain")}});
    }
    std::string output = outputs.front();
    for (std::size_t i = 1; i < outputs.size(); ++i) {
        const std::string merged = "merge." + std::to_string(i);
        node(plan, merged, "merge");
        edge(plan, output, merged);
        plan << "EDGE " << std::quoted(outputs[i]) << ' ' << std::quoted(merged) << " 1\n";
        output = merged;
    }
    plan << "END\n";
    const std::string encodedPlan = plan.str();
    if (encodedPlan.size() > maximumPlanBytes)
        return Result<CookedCanonicalPcgGraph>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "compiled PCG plan exceeds Cook budget", {}, {}, "asset.cook.pcg"));
    Value::Object runtime{{"schema", Value("eve.pcg-graph")},  {"schemaVersion", Value(std::int64_t(1))},
                          {"executionPlan", Value("chunk:1")}, {"executionPlanFormat", Value("EVPCG_POINT_GRAPH/1")},
                          {"outputNode", Value(output)},       {"spatialSlots", Value(std::move(slots))}};
    auto          encodedDefinition = Value(std::move(runtime)).toJson();
    if (!encodedDefinition) return Result<CookedCanonicalPcgGraph>::failure(encodedDefinition.status());
    return Result<CookedCanonicalPcgGraph>::success(
        {{encodedDefinition.value().begin(), encodedDefinition.value().end()},
         {encodedPlan.begin(), encodedPlan.end()}});
}

}  // namespace eve::asset

// CanonicalVolumeTextureCook.cpp
namespace eve::asset {
namespace {
const Value* field(const Value::Object& object, const char* name) {
    const auto found = object.find(name);
    return found == object.end() ? nullptr : &found->second;
}
bool exactString(const Value::Object& object, const char* name, const char* expected) {
    const auto* value = field(object, name);
    return value && value->isString() && value->asString() == expected;
}
bool positive32(const Value::Object& object, const char* name, uint32_t& output) {
    const auto* value = field(object, name);
    if (!value || !value->isInt64() || value->asInt() <= 0 || uint64_t(value->asInt()) > UINT32_MAX) return false;
    output = uint32_t(value->asInt());
    return true;
}
void put32(std::vector<uint8_t>& bytes, uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) bytes.push_back(uint8_t(value >> shift));
}
}  // namespace

Result<CookedCanonicalVolumeTexture> cookCanonicalVolumeTextureRgba8(std::span<const uint8_t> definition,
                                                                     std::span<const uint8_t> sourceR8,
                                                                     uint64_t                 maximumDecodedBytes) {
    auto decoded =
        Value::fromJson(std::string_view(reinterpret_cast<const char*>(definition.data()), definition.size()));
    if (!decoded) return Result<CookedCanonicalVolumeTexture>::failure(decoded.status());
    const auto* object = decoded.value().getIf<Value::Object>();
    uint32_t    width = 0, height = 0, depth = 0;
    const auto* blob = object ? field(*object, "blob") : nullptr;
    if (!object || object->size() != 8 || !exactString(*object, "schema", "eve.volume-texture") ||
        !exactString(*object, "encoding", "r8") || !exactString(*object, "usage", "noise") || !blob ||
        !blob->isString() || !blob->asString().ends_with("/source.r8") || !positive32(*object, "width", width) ||
        !positive32(*object, "height", height) || !positive32(*object, "depth", depth))
        return Result<CookedCanonicalVolumeTexture>::failure(
            Diagnostic::error(DiagnosticCode::ParseError, "volume definition shape or members are invalid", {}, {},
                              "asset.cook.volume-texture"));
    const auto* version = field(*object, "schemaVersion");
    if (!version || !version->isInt64() || version->asInt() != 1 ||
        uint64_t(width) > std::numeric_limits<uint64_t>::max() / height ||
        uint64_t(width) * height > std::numeric_limits<uint64_t>::max() / depth)
        return Result<CookedCanonicalVolumeTexture>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "volume dimensions, version, source bytes or budget are invalid", {}, {},
            "asset.cook.volume-texture"));
    const uint64_t voxels = uint64_t(width) * height * depth;
    if (voxels > SIZE_MAX || voxels != sourceR8.size() || maximumDecodedBytes < 24 ||
        voxels > (maximumDecodedBytes - 24) / 4)
        return Result<CookedCanonicalVolumeTexture>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "volume dimensions, version, source bytes or budget are invalid", {}, {},
            "asset.cook.volume-texture"));

    auto runtime        = *object;
    runtime["encoding"] = Value("rgba8");
    runtime["blob"]     = Value("chunk:1");
    auto encoded        = Value(std::move(runtime)).toJson();
    if (!encoded) return Result<CookedCanonicalVolumeTexture>::failure(encoded.status());
    CookedCanonicalVolumeTexture result;
    result.definition.assign(encoded.value().begin(), encoded.value().end());
    result.bulk.insert(result.bulk.end(), {'E', 'V', 'V', 'O', 'L', 0, 1, 0});
    put32(result.bulk, width);
    put32(result.bulk, height);
    put32(result.bulk, depth);
    put32(result.bulk, 0);
    result.bulk.reserve(24 + size_t(voxels) * 4);
    for (uint8_t value : sourceR8) result.bulk.insert(result.bulk.end(), {value, value, value, 255});
    return Result<CookedCanonicalVolumeTexture>::success(std::move(result));
}

}  // namespace eve::asset
