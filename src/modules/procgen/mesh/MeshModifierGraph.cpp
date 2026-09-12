#include "procgen/mesh/MeshModifierGraph.h"
#include "procgen/mesh/MeshBoolean.h"
#include "procgen/mesh/MeshUvProjection.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <unordered_set>

namespace eve::procgen {
namespace {

struct ParamSpec {
    const char* key;
    const char* kind;
    const char* defaultValue;
};

struct OperationSpec {
    const char*            id;
    int                    inputs;
    bool                   perVertex;
    std::vector<ParamSpec> params;
};

const std::vector<OperationSpec>& operationSpecs() {
    static const std::vector<OperationSpec> specs = [] {
        std::vector<OperationSpec> values = {
            {"mesh.input", 0, false, {}},
            {"mesh.output", 1, false, {}},
            {"mesh.splineTube",
             0,
             false,
             {{"radius", "float", "0.5"},
              {"pathSegments", "int", "32"},
              {"radialSegments", "int", "12"},
              {"roll", "float", "0"},
              {"cap", "int", "1"}}},
            {"mesh.splineRibbon",
             0,
             false,
             {{"width", "float", "2"},
              {"thickness", "float", "0"},
              {"pathSegments", "int", "32"},
              {"roll", "float", "0"},
              {"cap", "int", "1"}}},
            {"mesh.splineExtrude",
             0,
             false,
             {{"pathSegments", "int", "32"}, {"roll", "float", "0"}, {"cap", "int", "1"}}},
            {"deform.transform",
             1,
             true,
             {{"x", "float", "0"},
              {"y", "float", "0"},
              {"z", "float", "0"},
              {"pitch", "float", "0"},
              {"yaw", "float", "0"},
              {"roll", "float", "0"},
              {"scaleX", "float", "1"},
              {"scaleY", "float", "1"},
              {"scaleZ", "float", "1"}}},
            {"deform.bend", 1, true, {{"axis", "string", "y"}, {"angle", "float", "0"}, {"extent", "float", "1"}}},
            {"deform.twist", 1, true, {{"axis", "string", "y"}, {"angle", "float", "0"}, {"extent", "float", "1"}}},
            {"deform.noise",
             1,
             true,
             {{"amplitude", "float", "0.1"}, {"frequency", "float", "1"}, {"seed", "int", "1"}}},
            {"deform.soundReact",
             1,
             true,
             {{"level", "float", "0"},
              {"threshold", "float", "0"},
              {"strength", "float", "1"},
              {"frequency", "float", "1"},
              {"phase", "float", "0"},
              {"axis", "string", "y"}}},
            {"deform.radial",
             1,
             true,
             {{"x", "float", "0"},
              {"y", "float", "0"},
              {"z", "float", "0"},
              {"radius", "float", "1"},
              {"strength", "float", "0.1"},
              {"falloff", "float", "1"}}},
            {"deform.effector",
             1,
             true,
             {{"pointCount", "int", "1"}, {"density", "float", "1"},  {"multiplier", "float", "1"},
              {"p0x", "float", "0"},      {"p0y", "float", "0"},      {"p0z", "float", "0"},
              {"p0dx", "float", "0"},     {"p0dy", "float", "1"},     {"p0dz", "float", "0"},
              {"p0radius", "float", "1"}, {"p0weight", "float", "1"}, {"p1x", "float", "0"},
              {"p1y", "float", "0"},      {"p1z", "float", "0"},      {"p1dx", "float", "0"},
              {"p1dy", "float", "1"},     {"p1dz", "float", "0"},     {"p1radius", "float", "1"},
              {"p1weight", "float", "1"}, {"p2x", "float", "0"},      {"p2y", "float", "0"},
              {"p2z", "float", "0"},      {"p2dx", "float", "0"},     {"p2dy", "float", "1"},
              {"p2dz", "float", "0"},     {"p2radius", "float", "1"}, {"p2weight", "float", "1"},
              {"p3x", "float", "0"},      {"p3y", "float", "0"},      {"p3z", "float", "0"},
              {"p3dx", "float", "0"},     {"p3dy", "float", "1"},     {"p3dz", "float", "0"},
              {"p3radius", "float", "1"}, {"p3weight", "float", "1"}}},
            {"deform.angularBend",
             1,
             false,
             {{"angle", "float", "0"}, {"direction", "float", "0"}, {"axis", "string", "y"}}},
            {"deform.spherify",
             1,
             true,
             {{"x", "float", "0"},
              {"y", "float", "0"},
              {"z", "float", "0"},
              {"radius", "float", "1"},
              {"weight", "float", "1"}}},
            {"deform.ffd", 1, false, {{"p000x", "float", "0"}, {"p000y", "float", "0"}, {"p000z", "float", "0"},
                                      {"p001x", "float", "0"}, {"p001y", "float", "0"}, {"p001z", "float", "0"},
                                      {"p010x", "float", "0"}, {"p010y", "float", "0"}, {"p010z", "float", "0"},
                                      {"p011x", "float", "0"}, {"p011y", "float", "0"}, {"p011z", "float", "0"},
                                      {"p100x", "float", "0"}, {"p100y", "float", "0"}, {"p100z", "float", "0"},
                                      {"p101x", "float", "0"}, {"p101y", "float", "0"}, {"p101z", "float", "0"},
                                      {"p110x", "float", "0"}, {"p110y", "float", "0"}, {"p110z", "float", "0"},
                                      {"p111x", "float", "0"}, {"p111y", "float", "0"}, {"p111z", "float", "0"},
                                      {"weight", "float", "1"}}},
            {"deform.morph", 2, false, {{"weight", "float", "0.5"}}},
            {"deform.meshFit",
             2,
             false,
             {{"directionX", "float", "0"},
              {"directionY", "float", "-1"},
              {"directionZ", "float", "0"},
              {"maxDistance", "float", "10"},
              {"surfaceOffset", "float", "0"},
              {"bidirectional", "int", "0"}}},
            {"deform.spline",
             1,
             false,
             {{"axis", "string", "y"},
              {"c0x", "float", "0"},
              {"c0y", "float", "0"},
              {"c0z", "float", "0"},
              {"c1x", "float", "0"},
              {"c1y", "float", "0"},
              {"c1z", "float", "0"},
              {"c2x", "float", "0"},
              {"c2y", "float", "0"},
              {"c2z", "float", "0"},
              {"c3x", "float", "0"},
              {"c3y", "float", "0"},
              {"c3z", "float", "0"},
              {"weight", "float", "1"}}},
            {"deform.splinePath",
             1,
             false,
             {{"axis", "string", "y"}, {"weight", "float", "1"}, {"roll", "float", "0"}, {"scale", "float", "1"}}},
            {"deform.smooth", 1, false, {{"strength", "float", "0.5"}, {"iterations", "int", "1"}}},
            {"mesh.subdivide", 1, false, {{"levels", "int", "1"}}},
            {"mesh.cutPlane",
             1,
             false,
             {{"normalX", "float", "0"},
              {"normalY", "float", "1"},
              {"normalZ", "float", "0"},
              {"distance", "float", "0"},
              {"keepPositive", "int", "1"},
              {"cap", "int", "0"}}},
            {"mesh.append", 2, false, {}},
            {"mesh.boolean", 2, false, {{"operation", "string", "difference"}}},
            {"mesh.projectUv", 1, false, {{"mode", "string", "box"}, {"scale", "float", "1"},
                                           {"offsetU", "float", "0"}, {"offsetV", "float", "0"}}},
            {"mesh.weld", 1, false, {{"tolerance", "float", "0.0001"}}},
        };
        for (auto& spec : values) {
            if (!spec.perVertex) continue;
            spec.params.insert(spec.params.end(), {{"maskX", "float", "0"},
                                                   {"maskY", "float", "0"},
                                                   {"maskZ", "float", "0"},
                                                   {"maskRadius", "float", "0"},
                                                   {"maskFalloff", "float", "1"},
                                                   {"maskInvert", "int", "0"}});
        }
        return values;
    }();
    return specs;
}

const OperationSpec* findSpec(std::string_view operation) {
    const auto& specs = operationSpecs();
    const auto  found =
        std::find_if(specs.begin(), specs.end(), [&](const OperationSpec& spec) { return operation == spec.id; });
    return found == specs.end() ? nullptr : &*found;
}

const ParamSpec* findParam(const OperationSpec& spec, std::string_view key) {
    const auto found =
        std::find_if(spec.params.begin(), spec.params.end(), [&](const ParamSpec& param) { return key == param.key; });
    return found == spec.params.end() ? nullptr : &*found;
}

Result<void> graphFailure(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<void>::failure(
        Diagnostic::error(code, std::move(message), std::move(path), {}, "procgen.meshModifierGraph"));
}

template <class T>
Result<T> graphFailureValue(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(
        Diagnostic::error(code, std::move(message), std::move(path), {}, "procgen.meshModifierGraph"));
}

float parameter(const auto& node, std::string_view key, float fallback) {
    const auto found = node.floats.find(std::string(key));
    return found == node.floats.end() ? fallback : found->second;
}

int intParameter(const auto& node, std::string_view key, int fallback) {
    const auto found = node.ints.find(std::string(key));
    return found == node.ints.end() ? fallback : found->second;
}

std::string stringParameter(const auto& node, std::string_view key, std::string fallback) {
    const auto found = node.strings.find(std::string(key));
    return found == node.strings.end() ? std::move(fallback) : found->second;
}

struct Vec3 {
    float x = 0.f, y = 0.f, z = 0.f;
};

Vec3 rotateX(Vec3 value, float radians) {
    const float c = std::cos(radians), s = std::sin(radians);
    return {value.x, c * value.y - s * value.z, s * value.y + c * value.z};
}

Vec3 rotateY(Vec3 value, float radians) {
    const float c = std::cos(radians), s = std::sin(radians);
    return {c * value.x + s * value.z, value.y, -s * value.x + c * value.z};
}

Vec3 rotateZ(Vec3 value, float radians) {
    const float c = std::cos(radians), s = std::sin(radians);
    return {c * value.x - s * value.y, s * value.x + c * value.y, value.z};
}

float coherentNoise(Vec3 value, int seed) {
    const float phase = static_cast<float>(seed);
    return std::sin(value.x * 1.13f + value.y * 0.71f + value.z * 0.47f + phase * 0.123f) * 0.5f +
           std::sin(value.x * 0.37f - value.y * 1.41f + value.z * 0.89f + phase * 0.071f) * 0.3f +
           std::sin(-value.x * 0.83f + value.y * 0.29f + value.z * 1.67f + phase * 0.037f) * 0.2f;
}

void applyVertexOperation(const auto& node, Vec3& position, Vec3& normal) {
    constexpr float radiansPerDegree = 0.01745329251994329577f;
    if (node.operation == "deform.transform") {
        const float sx = parameter(node, "scaleX", 1.f);
        const float sy = parameter(node, "scaleY", 1.f);
        const float sz = parameter(node, "scaleZ", 1.f);
        position       = {position.x * sx, position.y * sy, position.z * sz};
        normal         = {normal.x / sx, normal.y / sy, normal.z / sz};
        position       = rotateZ(rotateY(rotateX(position, parameter(node, "pitch", 0.f) * radiansPerDegree),
                                         parameter(node, "yaw", 0.f) * radiansPerDegree),
                                 parameter(node, "roll", 0.f) * radiansPerDegree);
        normal         = rotateZ(rotateY(rotateX(normal, parameter(node, "pitch", 0.f) * radiansPerDegree),
                                         parameter(node, "yaw", 0.f) * radiansPerDegree),
                                 parameter(node, "roll", 0.f) * radiansPerDegree);
        position.x += parameter(node, "x", 0.f);
        position.y += parameter(node, "y", 0.f);
        position.z += parameter(node, "z", 0.f);
    } else if (node.operation == "deform.twist" || node.operation == "deform.bend") {
        const std::string axis   = stringParameter(node, "axis", "y");
        const float       extent = std::max(std::abs(parameter(node, "extent", 1.f)), 1e-6f);
        const float       along  = axis == "x" ? position.x : axis == "z" ? position.z : position.y;
        const float       angle  = parameter(node, "angle", 0.f) * radiansPerDegree * along / extent;
        if (node.operation == "deform.twist") {
            if (axis == "x") {
                position = rotateX(position, angle);
                normal   = rotateX(normal, angle);
            } else if (axis == "z") {
                position = rotateZ(position, angle);
                normal   = rotateZ(normal, angle);
            } else {
                position = rotateY(position, angle);
                normal   = rotateY(normal, angle);
            }
        } else {
            if (axis == "x") {
                position = rotateZ(position, angle);
                normal   = rotateZ(normal, angle);
            } else if (axis == "z") {
                position = rotateX(position, -angle);
                normal   = rotateX(normal, -angle);
            } else {
                position = rotateZ(position, -angle);
                normal   = rotateZ(normal, -angle);
            }
        }
    } else if (node.operation == "deform.noise") {
        const float frequency = parameter(node, "frequency", 1.f);
        const Vec3  sample{position.x * frequency, position.y * frequency, position.z * frequency};
        const int   seed      = intParameter(node, "seed", 1);
        const float amplitude = parameter(node, "amplitude", 0.1f);
        position.x += coherentNoise(sample, seed) * amplitude;
        position.y += coherentNoise({sample.y, sample.z, sample.x}, seed + 1013) * amplitude;
        position.z += coherentNoise({sample.z, sample.x, sample.y}, seed + 2029) * amplitude;
    } else if (node.operation == "deform.soundReact") {
        const std::string axis       = stringParameter(node, "axis", "y");
        const float       coordinate = axis == "x" ? position.x : (axis == "z" ? position.z : position.y);
        const float       level      = parameter(node, "level", 0.f);
        const float       threshold  = parameter(node, "threshold", 0.f);
        const float       response   = std::max(level - threshold, 0.f) * parameter(node, "strength", 1.f);
        const float       wave = 0.5f + 0.5f * std::sin(coordinate * parameter(node, "frequency", 1.f) * 6.283185307f +
                                                        parameter(node, "phase", 0.f));
        Vec3              radial = position;
        if (axis == "x") radial.x = 0.f;
        if (axis == "y") radial.y = 0.f;
        if (axis == "z") radial.z = 0.f;
        const float radialLength = std::sqrt(radial.x * radial.x + radial.y * radial.y + radial.z * radial.z);
        if (radialLength > 1e-7f) {
            const float amount = response * wave / radialLength;
            position.x += radial.x * amount;
            position.y += radial.y * amount;
            position.z += radial.z * amount;
        }
    } else if (node.operation == "deform.radial") {
        const Vec3  center{parameter(node, "x", 0.f), parameter(node, "y", 0.f), parameter(node, "z", 0.f)};
        Vec3        delta{position.x - center.x, position.y - center.y, position.z - center.z};
        const float distance = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
        const float radius   = std::max(parameter(node, "radius", 1.f), 1e-6f);
        if (distance < radius && distance > 1e-7f) {
            const float falloff = std::max(parameter(node, "falloff", 1.f), 0.01f);
            const float amount  = parameter(node, "strength", 0.1f) * std::pow(1.f - distance / radius, falloff);
            position.x += delta.x / distance * amount;
            position.y += delta.y / distance * amount;
            position.z += delta.z / distance * amount;
        }
    } else if (node.operation == "deform.effector") {
        const int   pointCount = std::clamp(intParameter(node, "pointCount", 1), 1, 4);
        const float density    = std::max(parameter(node, "density", 1.f), 0.01f);
        const float multiplier = parameter(node, "multiplier", 1.f);
        const Vec3  source     = position;
        for (int point = 0; point < pointCount; ++point) {
            const std::string prefix = "p" + std::to_string(point);
            const Vec3        center{parameter(node, prefix + "x", 0.f), parameter(node, prefix + "y", 0.f),
                                     parameter(node, prefix + "z", 0.f)};
            const Vec3        offset{parameter(node, prefix + "dx", 0.f), parameter(node, prefix + "dy", 1.f),
                                     parameter(node, prefix + "dz", 0.f)};
            const float       dx       = source.x - center.x;
            const float       dy       = source.y - center.y;
            const float       dz       = source.z - center.z;
            const float       distance = std::sqrt(dx * dx + dy * dy + dz * dz);
            const float       radius   = parameter(node, prefix + "radius", 1.f);
            if (distance >= radius) continue;
            const float influence =
                std::pow(1.f - distance / radius, density) * parameter(node, prefix + "weight", 1.f) * multiplier;
            position.x += offset.x * influence;
            position.y += offset.y * influence;
            position.z += offset.z * influence;
        }
    } else if (node.operation == "deform.spherify") {
        const Vec3  center{parameter(node, "x", 0.f), parameter(node, "y", 0.f), parameter(node, "z", 0.f)};
        Vec3        delta{position.x - center.x, position.y - center.y, position.z - center.z};
        const float length = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
        if (length > 1e-7f) {
            const float radius = std::max(parameter(node, "radius", 1.f), 0.f);
            const float weight = std::clamp(parameter(node, "weight", 1.f), 0.f, 1.f);
            const Vec3  target{center.x + delta.x * radius / length, center.y + delta.y * radius / length,
                               center.z + delta.z * radius / length};
            position = {std::lerp(position.x, target.x, weight), std::lerp(position.y, target.y, weight),
                        std::lerp(position.z, target.z, weight)};
            normal   = {delta.x / length, delta.y / length, delta.z / length};
        }
    }
    const float length = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
    if (length > 1e-7f) normal = {normal.x / length, normal.y / length, normal.z / length};
}

void applyMaskedVertexOperation(const auto& node, Vec3& position, Vec3& normal) {
    const Vec3 originalPosition = position;
    const Vec3 originalNormal   = normal;
    applyVertexOperation(node, position, normal);
    const float radius = parameter(node, "maskRadius", 0.f);
    if (radius <= 0.f) return;
    const float dx       = originalPosition.x - parameter(node, "maskX", 0.f);
    const float dy       = originalPosition.y - parameter(node, "maskY", 0.f);
    const float dz       = originalPosition.z - parameter(node, "maskZ", 0.f);
    const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    float       weight =
        std::pow(std::clamp(1.f - distance / radius, 0.f, 1.f), std::max(parameter(node, "maskFalloff", 1.f), 0.01f));
    if (intParameter(node, "maskInvert", 0) != 0) weight = 1.f - weight;
    position = {std::lerp(originalPosition.x, position.x, weight), std::lerp(originalPosition.y, position.y, weight),
                std::lerp(originalPosition.z, position.z, weight)};
    normal   = {std::lerp(originalNormal.x, normal.x, weight), std::lerp(originalNormal.y, normal.y, weight),
                std::lerp(originalNormal.z, normal.z, weight)};
    const float normalLength = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
    if (normalLength > 1e-7f) normal = {normal.x / normalLength, normal.y / normalLength, normal.z / normalLength};
}

Result<void> validateMesh(const MeshBuild& mesh, std::string_view path) {
    if (mesh.empty()) return graphFailure(DiagnosticCode::InvalidArgument, "mesh input is empty", std::string(path));
    if (mesh.positions().size() % 3u != 0u || mesh.normals().size() != mesh.positions().size() ||
        mesh.uvs().size() != (mesh.positions().size() / 3u) * 2u || mesh.indices().size() % 3u != 0u)
        return graphFailure(DiagnosticCode::InvariantViolation, "mesh streams have incompatible lengths",
                            std::string(path));
    for (const auto index : mesh.indices())
        if (index >= static_cast<std::uint32_t>(mesh.getVertexCount()))
            return graphFailure(DiagnosticCode::InvariantViolation, "mesh index is outside the vertex stream",
                                std::string(path));
    return Result<void>::success();
}

void recalculateNormals(MeshBuild& mesh) {
    auto& normals = mesh.normals();
    std::fill(normals.begin(), normals.end(), 0.f);
    const auto& positions = mesh.positions();
    const auto& indices   = mesh.indices();
    for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
        const std::size_t a = std::size_t(indices[i]) * 3u;
        const std::size_t b = std::size_t(indices[i + 1]) * 3u;
        const std::size_t c = std::size_t(indices[i + 2]) * 3u;
        const Vec3        ab{positions[b] - positions[a], positions[b + 1] - positions[a + 1],
                             positions[b + 2] - positions[a + 2]};
        const Vec3        ac{positions[c] - positions[a], positions[c + 1] - positions[a + 1],
                             positions[c + 2] - positions[a + 2]};
        const Vec3        cross{ab.y * ac.z - ab.z * ac.y, ab.z * ac.x - ab.x * ac.z, ab.x * ac.y - ab.y * ac.x};
        for (const auto vertex : {a, b, c}) {
            normals[vertex] += cross.x;
            normals[vertex + 1] += cross.y;
            normals[vertex + 2] += cross.z;
        }
    }
    for (std::size_t i = 0; i + 2 < normals.size(); i += 3) {
        const float length =
            std::sqrt(normals[i] * normals[i] + normals[i + 1] * normals[i + 1] + normals[i + 2] * normals[i + 2]);
        if (length > 1e-7f) {
            normals[i] /= length;
            normals[i + 1] /= length;
            normals[i + 2] /= length;
        }
    }
}

Result<MeshBuild> smoothMesh(const MeshBuild& input, float strength, int iterations) {
    MeshBuild                                      output = input;
    const int                                      count  = output.getVertexCount();
    std::vector<std::unordered_set<std::uint32_t>> neighbors(static_cast<std::size_t>(count));
    const auto&                                    indices = output.indices();
    for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
        const std::array<std::uint32_t, 3> tri{indices[i], indices[i + 1], indices[i + 2]};
        for (int corner = 0; corner < 3; ++corner) {
            neighbors[tri[corner]].insert(tri[(corner + 1) % 3]);
            neighbors[tri[corner]].insert(tri[(corner + 2) % 3]);
        }
    }
    strength                = std::clamp(strength, 0.f, 1.f);
    iterations              = std::clamp(iterations, 1, 64);
    std::vector<float> next = output.positions();
    for (int iteration = 0; iteration < iterations; ++iteration) {
        const auto current = output.positions();
        for (int vertex = 0; vertex < count; ++vertex) {
            const auto& adjacent = neighbors[static_cast<std::size_t>(vertex)];
            if (adjacent.empty()) continue;
            Vec3 average;
            for (const auto neighbor : adjacent) {
                average.x += current[std::size_t(neighbor) * 3u];
                average.y += current[std::size_t(neighbor) * 3u + 1u];
                average.z += current[std::size_t(neighbor) * 3u + 2u];
            }
            const float       divisor = float(adjacent.size());
            const std::size_t base    = std::size_t(vertex) * 3u;
            next[base]                = std::lerp(current[base], average.x / divisor, strength);
            next[base + 1]            = std::lerp(current[base + 1], average.y / divisor, strength);
            next[base + 2]            = std::lerp(current[base + 2], average.z / divisor, strength);
        }
        output.positions() = next;
    }
    recalculateNormals(output);
    return Result<MeshBuild>::success(std::move(output));
}

void copyMetadata(const MeshBuild& input, MeshBuild& output) {
    for (const auto& [key, value] : input.metadata()) output.setMeta(key, value);
}

Result<MeshBuild> angularBendMesh(const MeshBuild& input, const auto& node) {
    MeshBuild output    = input;
    auto&     positions = output.positions();
    if (positions.empty()) return Result<MeshBuild>::success(std::move(output));
    const std::string axis      = stringParameter(node, "axis", "y");
    const int         component = axis == "x" ? 0 : axis == "z" ? 2 : 1;
    float             minimum   = positions[static_cast<std::size_t>(component)];
    float             maximum   = minimum;
    for (std::size_t i = static_cast<std::size_t>(component); i < positions.size(); i += 3u) {
        minimum = std::min(minimum, positions[i]);
        maximum = std::max(maximum, positions[i]);
    }
    const float extent    = std::max(maximum - minimum, 1e-6f);
    const float direction = parameter(node, "direction", 0.f) * 0.01745329251994329577f;
    const Vec3  bendAxis{std::sin(direction), 0.f, std::cos(direction)};
    const float angle   = parameter(node, "angle", 0.f) * 0.01745329251994329577f;
    auto&       normals = output.normals();
    for (int vertex = 0; vertex < output.getVertexCount(); ++vertex) {
        const std::size_t base  = static_cast<std::size_t>(vertex) * 3u;
        const float       t     = (positions[base + static_cast<std::size_t>(component)] - minimum) / extent;
        const float       theta = angle * t;
        const float       c = std::cos(theta), s = std::sin(theta);
        auto              rotate = [&](Vec3 value) {
            const float dot = value.x * bendAxis.x + value.y * bendAxis.y + value.z * bendAxis.z;
            const Vec3  cross{bendAxis.y * value.z - bendAxis.z * value.y, bendAxis.z * value.x - bendAxis.x * value.z,
                              bendAxis.x * value.y - bendAxis.y * value.x};
            return Vec3{value.x * c + cross.x * s + bendAxis.x * dot * (1.f - c),
                        value.y * c + cross.y * s + bendAxis.y * dot * (1.f - c),
                        value.z * c + cross.z * s + bendAxis.z * dot * (1.f - c)};
        };
        const Vec3 position = rotate({positions[base], positions[base + 1], positions[base + 2]});
        const Vec3 normal   = rotate({normals[base], normals[base + 1], normals[base + 2]});
        positions[base]     = position.x;
        positions[base + 1] = position.y;
        positions[base + 2] = position.z;
        normals[base]       = normal.x;
        normals[base + 1]   = normal.y;
        normals[base + 2]   = normal.z;
    }
    return Result<MeshBuild>::success(std::move(output));
}

Result<MeshBuild> splineMesh(const MeshBuild& input, const auto& node) {
    MeshBuild output    = input;
    auto&     positions = output.positions();
    if (positions.empty()) return Result<MeshBuild>::success(std::move(output));
    const std::string axis      = stringParameter(node, "axis", "y");
    const int         component = axis == "x" ? 0 : axis == "z" ? 2 : 1;
    float             minimum   = positions[static_cast<std::size_t>(component)];
    float             maximum   = minimum;
    for (std::size_t i = static_cast<std::size_t>(component); i < positions.size(); i += 3u) {
        minimum = std::min(minimum, positions[i]);
        maximum = std::max(maximum, positions[i]);
    }
    const float         extent = std::max(maximum - minimum, 1e-6f);
    std::array<Vec3, 4> controls;
    for (int i = 0; i < 4; ++i) {
        const std::string prefix              = "c" + std::to_string(i);
        controls[static_cast<std::size_t>(i)] = {parameter(node, prefix + "x", 0.f), parameter(node, prefix + "y", 0.f),
                                                 parameter(node, prefix + "z", 0.f)};
    }
    const float weight = std::clamp(parameter(node, "weight", 1.f), 0.f, 1.f);
    auto        bezier = [&](float t) {
        const float                u = 1.f - t;
        const std::array<float, 4> b{u * u * u, 3.f * u * u * t, 3.f * u * t * t, t * t * t};
        Vec3                       result;
        for (int i = 0; i < 4; ++i) {
            result.x += controls[static_cast<std::size_t>(i)].x * b[static_cast<std::size_t>(i)];
            result.y += controls[static_cast<std::size_t>(i)].y * b[static_cast<std::size_t>(i)];
            result.z += controls[static_cast<std::size_t>(i)].z * b[static_cast<std::size_t>(i)];
        }
        return result;
    };
    for (std::size_t base = 0; base < positions.size(); base += 3u) {
        Vec3        source{positions[base], positions[base + 1], positions[base + 2]};
        const float t       = std::clamp((component == 0   ? source.x
                                          : component == 2 ? source.z
                                                           : source.y) -
                                             minimum,
                                         0.f, extent) /
                              extent;
        const Vec3  offset  = bezier(t);
        positions[base]     = source.x + offset.x * weight;
        positions[base + 1] = source.y + offset.y * weight;
        positions[base + 2] = source.z + offset.z * weight;
    }
    recalculateNormals(output);
    return Result<MeshBuild>::success(std::move(output));
}

Result<MeshBuild> splinePathMesh(const MeshBuild& input, const auto& node) {
    MeshBuild output    = input;
    auto&     positions = output.positions();
    if (positions.empty()) return Result<MeshBuild>::success(std::move(output));
    const std::string axis      = stringParameter(node, "axis", "y");
    const int         component = axis == "x" ? 0 : axis == "z" ? 2 : 1;
    float             minimum   = positions[static_cast<std::size_t>(component)];
    float             maximum   = minimum;
    for (std::size_t i = static_cast<std::size_t>(component); i < positions.size(); i += 3u) {
        minimum = std::min(minimum, positions[i]);
        maximum = std::max(maximum, positions[i]);
    }
    const float extent = std::max(maximum - minimum, 1e-6f);
    const float weight = std::clamp(parameter(node, "weight", 1.f), 0.f, 1.f);
    const float scale  = parameter(node, "scale", 1.f);
    const float roll   = parameter(node, "roll", 0.f) * 0.01745329251994329577f;
    for (std::size_t base = 0; base < positions.size(); base += 3u) {
        const Vec3  source{positions[base], positions[base + 1], positions[base + 2]};
        const float along   = component == 0 ? source.x : component == 2 ? source.z : source.y;
        auto        sampled = node.splinePath.evaluateResult((along - minimum) / extent);
        if (!sampled.ok()) return Result<MeshBuild>::failure(sampled.status());
        const auto& sample = sampled.value();
        Vec3        tangent{sample.tangentX, sample.tangentY, sample.tangentZ};
        Vec3        side, up;
        if (tangent.z < -0.999999f) {
            side = {0.f, -1.f, 0.f};
            up   = {-1.f, 0.f, 0.f};
        } else {
            const float a = 1.f / (1.f + tangent.z);
            const float b = -tangent.x * tangent.y * a;
            side          = {1.f - tangent.x * tangent.x * a, b, -tangent.x};
            up            = {b, 1.f - tangent.y * tangent.y * a, -tangent.y};
        }
        const float c = std::cos(roll), s = std::sin(roll);
        const Vec3  rolledSide{side.x * c + up.x * s, side.y * c + up.y * s, side.z * c + up.z * s};
        const Vec3  rolledUp{up.x * c - side.x * s, up.y * c - side.y * s, up.z * c - side.z * s};
        const float localA = component == 0 ? source.y : source.x;
        const float localB = component == 2 ? source.y : source.z;
        const Vec3  target{sample.x + (rolledSide.x * localA + rolledUp.x * localB) * scale,
                           sample.y + (rolledSide.y * localA + rolledUp.y * localB) * scale,
                           sample.z + (rolledSide.z * localA + rolledUp.z * localB) * scale};
        positions[base]     = std::lerp(source.x, target.x, weight);
        positions[base + 1] = std::lerp(source.y, target.y, weight);
        positions[base + 2] = std::lerp(source.z, target.z, weight);
    }
    recalculateNormals(output);
    return Result<MeshBuild>::success(std::move(output));
}

Result<MeshBuild> splineTubeMesh(const auto& node) {
    if (node.splinePath.chunkCount() > 1) {
        MeshBuild output;
        for (int chunk = 0; chunk < node.splinePath.chunkCount(); ++chunk) {
            auto path = node.splinePath.chunkPathResult(chunk);
            if (!path.ok()) return Result<MeshBuild>::failure(path.status());
            auto chunkNode       = node;
            chunkNode.splinePath = std::move(path).takeValue();
            auto mesh            = splineTubeMesh(chunkNode);
            if (!mesh.ok()) return mesh;
            if (!output.appendTransformed(&mesh.value(), 0.f, 0.f, 0.f, 0.f, 1.f, 1.f, 1.f))
                return graphFailureValue<MeshBuild>(DiagnosticCode::InvariantViolation,
                                                    "failed to compose spline tube chunks");
        }
        output.setMeta("generator", "mesh.splineTube");
        output.setMeta("chunks", std::to_string(node.splinePath.chunkCount()));
        return Result<MeshBuild>::success(std::move(output));
    }
    const float radius         = parameter(node, "radius", 0.5f);
    const int   pathSegments   = intParameter(node, "pathSegments", 32);
    const int   radialSegments = intParameter(node, "radialSegments", 12);
    const float roll           = parameter(node, "roll", 0.f);
    const bool  capped         = intParameter(node, "cap", 1) != 0 && !node.splinePath.isClosed();
    const int   ringStride     = radialSegments + 1;
    const int   ringCount      = pathSegments + 1;
    MeshBuild   output;
    output.reserve(ringCount * ringStride + (capped ? 2 * (ringStride + 1) : 0),
                   pathSegments * radialSegments * 6 + (capped ? radialSegments * 6 : 0));

    constexpr float tau           = 6.28318530717958647692f;
    auto            sampledFrames = node.splinePath.sampleFramesResult(pathSegments, true, roll, 24);
    if (!sampledFrames.ok()) return Result<MeshBuild>::failure(sampledFrames.status());
    const auto& frames = sampledFrames.value();
    for (int ring = 0; ring < ringCount; ++ring) {
        const float u      = static_cast<float>(ring) / static_cast<float>(pathSegments);
        const auto& frame  = frames[static_cast<std::size_t>(ring)];
        const auto& sample = frame.sample;
        const Vec3  rolledSide{frame.sideX, frame.sideY, frame.sideZ};
        const Vec3  rolledUp{frame.upX, frame.upY, frame.upZ};
        for (int radial = 0; radial <= radialSegments; ++radial) {
            const float v     = static_cast<float>(radial) / static_cast<float>(radialSegments);
            const float angle = v * tau;
            const float c = std::cos(angle), s = std::sin(angle);
            const Vec3  offset{rolledSide.x * c * sample.scaleX + rolledUp.x * s * sample.scaleY,
                               rolledSide.y * c * sample.scaleX + rolledUp.y * s * sample.scaleY,
                               rolledSide.z * c * sample.scaleX + rolledUp.z * s * sample.scaleY};
            const float inverseX = 1.f / sample.scaleX, inverseY = 1.f / sample.scaleY;
            Vec3        normal{rolledSide.x * c * inverseX + rolledUp.x * s * inverseY,
                               rolledSide.y * c * inverseX + rolledUp.y * s * inverseY,
                               rolledSide.z * c * inverseX + rolledUp.z * s * inverseY};
            const float normalLength = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
            normal                   = {normal.x / normalLength, normal.y / normalLength, normal.z / normalLength};
            output.addVertex(sample.x + offset.x * radius, sample.y + offset.y * radius, sample.z + offset.z * radius,
                             normal.x, normal.y, normal.z, u, v);
        }
    }

    output.setActiveGroup("tube");
    for (int ring = 0; ring < pathSegments; ++ring) {
        for (int radial = 0; radial < radialSegments; ++radial) {
            const auto a = static_cast<std::uint32_t>(ring * ringStride + radial);
            const auto b = a + static_cast<std::uint32_t>(ringStride);
            output.addTriangle(a, a + 1u, b);
            output.addTriangle(a + 1u, b + 1u, b);
        }
    }
    if (capped) {
        output.setActiveGroup("caps");
        auto startSample = node.splinePath.evaluateResult(0.f);
        auto endSample   = node.splinePath.evaluateResult(1.f);
        if (!startSample.ok()) return Result<MeshBuild>::failure(startSample.status());
        if (!endSample.ok()) return Result<MeshBuild>::failure(endSample.status());
        const Vec3 startNormal{-startSample.value().tangentX, -startSample.value().tangentY,
                               -startSample.value().tangentZ};
        const Vec3 endNormal{endSample.value().tangentX, endSample.value().tangentY, endSample.value().tangentZ};
        const auto startCenter = static_cast<std::uint32_t>(output.getVertexCount());
        output.addVertex(startSample.value().x, startSample.value().y, startSample.value().z, startNormal.x,
                         startNormal.y, startNormal.z, 0.5f, 0.5f);
        const auto startRim = static_cast<std::uint32_t>(output.getVertexCount());
        for (int radial = 0; radial <= radialSegments; ++radial) {
            const float angle = static_cast<float>(radial) / static_cast<float>(radialSegments) * tau;
            output.addVertex(output.getPositionX(radial), output.getPositionY(radial), output.getPositionZ(radial),
                             startNormal.x, startNormal.y, startNormal.z, 0.5f + std::cos(angle) * 0.5f,
                             0.5f + std::sin(angle) * 0.5f);
        }
        const auto endCenter = static_cast<std::uint32_t>(output.getVertexCount());
        output.addVertex(endSample.value().x, endSample.value().y, endSample.value().z, endSample.value().tangentX,
                         endSample.value().tangentY, endSample.value().tangentZ, 0.5f, 0.5f);
        const auto lastRing = static_cast<std::uint32_t>(pathSegments * ringStride);
        const auto endRim   = static_cast<std::uint32_t>(output.getVertexCount());
        for (int radial = 0; radial <= radialSegments; ++radial) {
            const float angle = static_cast<float>(radial) / static_cast<float>(radialSegments) * tau;
            output.addVertex(output.getPositionX(static_cast<int>(lastRing) + radial),
                             output.getPositionY(static_cast<int>(lastRing) + radial),
                             output.getPositionZ(static_cast<int>(lastRing) + radial), endNormal.x, endNormal.y,
                             endNormal.z, 0.5f + std::cos(angle) * 0.5f, 0.5f + std::sin(angle) * 0.5f);
        }
        for (int radial = 0; radial < radialSegments; ++radial) {
            const auto r = static_cast<std::uint32_t>(radial);
            output.addTriangle(startCenter, startRim + r + 1u, startRim + r);
            output.addTriangle(endCenter, endRim + r, endRim + r + 1u);
        }
    }
    output.setMeta("generator", "mesh.splineTube");
    output.setMeta("pathSegments", std::to_string(pathSegments));
    output.setMeta("radialSegments", std::to_string(radialSegments));
    output.setMeta("closed", node.splinePath.isClosed() ? "1" : "0");
    return Result<MeshBuild>::success(std::move(output));
}

Result<MeshBuild> splineRibbonMesh(const auto& node) {
    if (node.splinePath.chunkCount() > 1) {
        MeshBuild output;
        for (int chunk = 0; chunk < node.splinePath.chunkCount(); ++chunk) {
            auto path = node.splinePath.chunkPathResult(chunk);
            if (!path.ok()) return Result<MeshBuild>::failure(path.status());
            auto chunkNode       = node;
            chunkNode.splinePath = std::move(path).takeValue();
            auto mesh            = splineRibbonMesh(chunkNode);
            if (!mesh.ok()) return mesh;
            if (!output.appendTransformed(&mesh.value(), 0.f, 0.f, 0.f, 0.f, 1.f, 1.f, 1.f))
                return graphFailureValue<MeshBuild>(DiagnosticCode::InvariantViolation,
                                                    "failed to compose spline ribbon chunks");
        }
        output.setMeta("generator", "mesh.splineRibbon");
        output.setMeta("chunks", std::to_string(node.splinePath.chunkCount()));
        return Result<MeshBuild>::success(std::move(output));
    }
    const float width        = parameter(node, "width", 2.f);
    const float thickness    = parameter(node, "thickness", 0.f);
    const int   pathSegments = intParameter(node, "pathSegments", 32);
    const bool  solid        = thickness > 1e-7f;
    const bool  capped       = solid && intParameter(node, "cap", 1) != 0 && !node.splinePath.isClosed();
    auto        frames       = node.splinePath.sampleFramesResult(pathSegments, true, parameter(node, "roll", 0.f), 24);
    if (!frames.ok()) return Result<MeshBuild>::failure(frames.status());

    const int ringStride = solid ? 8 : 2;
    MeshBuild output;
    output.reserve((pathSegments + 1) * ringStride + (capped ? 8 : 0),
                   pathSegments * (solid ? 24 : 6) + (capped ? 12 : 0));
    const float halfWidth = width * 0.5f;
    for (int ring = 0; ring <= pathSegments; ++ring) {
        const auto& frame = frames.value()[static_cast<std::size_t>(ring)];
        const auto& p     = frame.sample;
        const Vec3  side{frame.sideX, frame.sideY, frame.sideZ};
        const Vec3  up{frame.upX, frame.upY, frame.upZ};
        const float u             = static_cast<float>(ring) / static_cast<float>(pathSegments);
        const float ringHalfWidth = halfWidth * p.scaleX;
        const float ringThickness = thickness * p.scaleY;
        const Vec3  left{p.x - side.x * ringHalfWidth, p.y - side.y * ringHalfWidth, p.z - side.z * ringHalfWidth};
        const Vec3  right{p.x + side.x * ringHalfWidth, p.y + side.y * ringHalfWidth, p.z + side.z * ringHalfWidth};
        output.addVertex(left.x, left.y, left.z, up.x, up.y, up.z, u, 0.f);
        output.addVertex(right.x, right.y, right.z, up.x, up.y, up.z, u, 1.f);
        if (!solid) continue;
        const Vec3 down{-up.x, -up.y, -up.z};
        const Vec3 bottomLeft{left.x - up.x * ringThickness, left.y - up.y * ringThickness,
                              left.z - up.z * ringThickness};
        const Vec3 bottomRight{right.x - up.x * ringThickness, right.y - up.y * ringThickness,
                               right.z - up.z * ringThickness};
        output.addVertex(bottomRight.x, bottomRight.y, bottomRight.z, down.x, down.y, down.z, u, 0.f);
        output.addVertex(bottomLeft.x, bottomLeft.y, bottomLeft.z, down.x, down.y, down.z, u, 1.f);
        output.addVertex(left.x, left.y, left.z, -side.x, -side.y, -side.z, u, 0.f);
        output.addVertex(bottomLeft.x, bottomLeft.y, bottomLeft.z, -side.x, -side.y, -side.z, u, 1.f);
        output.addVertex(right.x, right.y, right.z, side.x, side.y, side.z, u, 0.f);
        output.addVertex(bottomRight.x, bottomRight.y, bottomRight.z, side.x, side.y, side.z, u, 1.f);
    }

    output.setActiveGroup("surface");
    for (int ring = 0; ring < pathSegments; ++ring) {
        const auto a = static_cast<std::uint32_t>(ring * ringStride);
        const auto b = a + static_cast<std::uint32_t>(ringStride);
        output.addTriangle(a, b, a + 1u);
        output.addTriangle(a + 1u, b, b + 1u);
    }
    if (solid) {
        output.setActiveGroup("bottom");
        for (int ring = 0; ring < pathSegments; ++ring) {
            const auto a = static_cast<std::uint32_t>(ring * ringStride + 2);
            const auto b = a + static_cast<std::uint32_t>(ringStride);
            output.addTriangle(a, b, a + 1u);
            output.addTriangle(a + 1u, b, b + 1u);
        }
        output.setActiveGroup("sides");
        for (int ring = 0; ring < pathSegments; ++ring) {
            const auto base = static_cast<std::uint32_t>(ring * ringStride);
            const auto next = base + static_cast<std::uint32_t>(ringStride);
            output.addTriangle(base + 4u, base + 5u, next + 4u);
            output.addTriangle(base + 5u, next + 5u, next + 4u);
            output.addTriangle(base + 6u, next + 6u, base + 7u);
            output.addTriangle(base + 7u, next + 6u, next + 7u);
        }
        if (capped) {
            output.setActiveGroup("caps");
            for (int end = 0; end < 2; ++end) {
                const int   sourceRing = end == 0 ? 0 : pathSegments;
                const auto& frame      = frames.value()[static_cast<std::size_t>(sourceRing)];
                const Vec3  normal{(end == 0 ? -1.f : 1.f) * frame.sample.tangentX,
                                   (end == 0 ? -1.f : 1.f) * frame.sample.tangentY,
                                   (end == 0 ? -1.f : 1.f) * frame.sample.tangentZ};
                const auto  source = static_cast<std::uint32_t>(sourceRing * ringStride);
                const auto  base   = static_cast<std::uint32_t>(output.getVertexCount());
                for (const auto offset : {0u, 1u, 2u, 3u}) {
                    const auto vertex = source + offset;
                    output.addVertex(output.getPositionX(static_cast<int>(vertex)),
                                     output.getPositionY(static_cast<int>(vertex)),
                                     output.getPositionZ(static_cast<int>(vertex)), normal.x, normal.y, normal.z,
                                     offset == 0u || offset == 3u ? 0.f : 1.f, offset < 2u ? 0.f : 1.f);
                }
                if (end == 0) {
                    output.addTriangle(base, base + 1u, base + 3u);
                    output.addTriangle(base + 1u, base + 2u, base + 3u);
                } else {
                    output.addTriangle(base, base + 3u, base + 1u);
                    output.addTriangle(base + 1u, base + 3u, base + 2u);
                }
            }
        }
    }
    output.setMeta("generator", "mesh.splineRibbon");
    output.setMeta("pathSegments", std::to_string(pathSegments));
    output.setMeta("closed", node.splinePath.isClosed() ? "1" : "0");
    return Result<MeshBuild>::success(std::move(output));
}

float profileArea(const SplineProfile& profile) {
    float area = 0.f;
    for (std::size_t i = 0; i < profile.points.size(); ++i) {
        const auto& a = profile.points[i];
        const auto& b = profile.points[(i + 1u) % profile.points.size()];
        area += a.x * b.y - b.x * a.y;
    }
    return area * 0.5f;
}

float cross2(const SplineProfilePoint& a, const SplineProfilePoint& b, const SplineProfilePoint& c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

bool insideTriangle(const SplineProfilePoint& p, const SplineProfilePoint& a, const SplineProfilePoint& b,
                    const SplineProfilePoint& c) {
    constexpr float epsilon = 1e-7f;
    return cross2(a, b, p) >= -epsilon && cross2(b, c, p) >= -epsilon && cross2(c, a, p) >= -epsilon;
}

Result<std::vector<std::uint32_t>> triangulateProfile(const SplineProfile& profile) {
    std::vector<std::uint32_t> polygon(profile.points.size());
    if (profileArea(profile) > 0.f)
        for (std::size_t i = 0; i < polygon.size(); ++i) polygon[i] = static_cast<std::uint32_t>(i);
    else
        for (std::size_t i = 0; i < polygon.size(); ++i)
            polygon[i] = static_cast<std::uint32_t>(polygon.size() - 1u - i);
    std::vector<std::uint32_t> triangles;
    while (polygon.size() > 3u) {
        bool clipped = false;
        for (std::size_t i = 0; i < polygon.size(); ++i) {
            const auto previous = polygon[(i + polygon.size() - 1u) % polygon.size()];
            const auto current  = polygon[i];
            const auto next     = polygon[(i + 1u) % polygon.size()];
            if (cross2(profile.points[previous], profile.points[current], profile.points[next]) <= 1e-7f) continue;
            bool contains = false;
            for (const auto candidate : polygon) {
                if (candidate == previous || candidate == current || candidate == next) continue;
                if (insideTriangle(profile.points[candidate], profile.points[previous], profile.points[current],
                                   profile.points[next])) {
                    contains = true;
                    break;
                }
            }
            if (contains) continue;
            triangles.insert(triangles.end(), {previous, current, next});
            polygon.erase(polygon.begin() + static_cast<std::ptrdiff_t>(i));
            clipped = true;
            break;
        }
        if (!clipped)
            return graphFailureValue<std::vector<std::uint32_t>>(
                DiagnosticCode::InvalidArgument, "closed spline profile must be a simple non-degenerate polygon",
                "profile");
    }
    triangles.insert(triangles.end(), {polygon[0], polygon[1], polygon[2]});
    return Result<std::vector<std::uint32_t>>::success(std::move(triangles));
}

Result<MeshBuild> splineExtrudeMesh(const auto& node) {
    if (node.splinePath.chunkCount() > 1) {
        MeshBuild output;
        for (int chunk = 0; chunk < node.splinePath.chunkCount(); ++chunk) {
            auto path = node.splinePath.chunkPathResult(chunk);
            if (!path.ok()) return Result<MeshBuild>::failure(path.status());
            auto chunkNode       = node;
            chunkNode.splinePath = std::move(path).takeValue();
            auto mesh            = splineExtrudeMesh(chunkNode);
            if (!mesh.ok()) return mesh;
            if (!output.appendTransformed(&mesh.value(), 0.f, 0.f, 0.f, 0.f, 1.f, 1.f, 1.f))
                return graphFailureValue<MeshBuild>(DiagnosticCode::InvariantViolation,
                                                    "failed to compose spline extrusion chunks");
        }
        output.setMeta("generator", "mesh.splineExtrude");
        output.setMeta("chunks", std::to_string(node.splinePath.chunkCount()));
        return Result<MeshBuild>::success(std::move(output));
    }
    const int  pathSegments = intParameter(node, "pathSegments", 32);
    const bool capped = node.splineProfile.closed && intParameter(node, "cap", 1) != 0 && !node.splinePath.isClosed();
    auto       frames = node.splinePath.sampleFramesResult(pathSegments, true, parameter(node, "roll", 0.f), 24);
    if (!frames.ok()) return Result<MeshBuild>::failure(frames.status());
    const auto& profile         = node.splineProfile;
    const int   pointCount      = static_cast<int>(profile.points.size());
    const int   profileSegments = profile.closed ? pointCount : pointCount - 1;
    const int   ringStride      = pointCount + (profile.closed ? 1 : 0);
    MeshBuild   output;
    output.reserve((pathSegments + 1) * ringStride + (capped ? pointCount * 2 : 0),
                   pathSegments * profileSegments * 6 + (capped ? (pointCount - 2) * 6 : 0));
    const float orientation = profile.closed && profileArea(profile) < 0.f ? -1.f : 1.f;
    for (int ring = 0; ring <= pathSegments; ++ring) {
        const auto& frame  = frames.value()[static_cast<std::size_t>(ring)];
        const auto& sample = frame.sample;
        for (int profileIndex = 0; profileIndex < ringStride; ++profileIndex) {
            const int   i        = profileIndex % pointCount;
            const int   previous = profile.closed ? (i + pointCount - 1) % pointCount : std::max(0, i - 1);
            const int   next     = profile.closed ? (i + 1) % pointCount : std::min(pointCount - 1, i + 1);
            const auto& p        = profile.points[static_cast<std::size_t>(i)];
            const auto& a        = profile.points[static_cast<std::size_t>(previous)];
            const auto& b        = profile.points[static_cast<std::size_t>(next)];
            float       nx = orientation * (b.y - a.y), ny = orientation * (a.x - b.x);
            const float inverseX = 1.f / sample.scaleX, inverseY = 1.f / sample.scaleY;
            nx *= inverseX;
            ny *= inverseY;
            const float nl = std::sqrt(nx * nx + ny * ny);
            nx /= nl;
            ny /= nl;
            output.addVertex(sample.x + frame.sideX * p.x * sample.scaleX + frame.upX * p.y * sample.scaleY,
                             sample.y + frame.sideY * p.x * sample.scaleX + frame.upY * p.y * sample.scaleY,
                             sample.z + frame.sideZ * p.x * sample.scaleX + frame.upZ * p.y * sample.scaleY,
                             frame.sideX * nx + frame.upX * ny, frame.sideY * nx + frame.upY * ny,
                             frame.sideZ * nx + frame.upZ * ny,
                             static_cast<float>(ring) / static_cast<float>(pathSegments),
                             static_cast<float>(profileIndex) / static_cast<float>(profileSegments));
        }
    }
    output.setActiveGroup("profile");
    for (int ring = 0; ring < pathSegments; ++ring)
        for (int segment = 0; segment < profileSegments; ++segment) {
            const auto a = static_cast<std::uint32_t>(ring * ringStride + segment);
            const auto b = a + static_cast<std::uint32_t>(ringStride);
            output.addTriangle(a, a + 1u, b);
            output.addTriangle(a + 1u, b + 1u, b);
        }
    if (capped) {
        auto triangles = triangulateProfile(profile);
        if (!triangles.ok()) return Result<MeshBuild>::failure(triangles.status());
        output.setActiveGroup("caps");
        for (int end = 0; end < 2; ++end) {
            const int   ring  = end == 0 ? 0 : pathSegments;
            const auto& frame = frames.value()[static_cast<std::size_t>(ring)];
            const auto  base  = static_cast<std::uint32_t>(output.getVertexCount());
            for (int i = 0; i < pointCount; ++i) {
                const int   source    = ring * ringStride + i;
                const float direction = end == 0 ? -1.f : 1.f;
                output.addVertex(output.getPositionX(source), output.getPositionY(source), output.getPositionZ(source),
                                 frame.sample.tangentX * direction, frame.sample.tangentY * direction,
                                 frame.sample.tangentZ * direction, profile.points[static_cast<std::size_t>(i)].x,
                                 profile.points[static_cast<std::size_t>(i)].y);
            }
            for (std::size_t i = 0; i < triangles.value().size(); i += 3u) {
                const auto a = base + triangles.value()[i], b = base + triangles.value()[i + 1u];
                const auto c = base + triangles.value()[i + 2u];
                if (end == 0)
                    output.addTriangle(a, c, b);
                else
                    output.addTriangle(a, b, c);
            }
        }
    }
    output.setMeta("generator", "mesh.splineExtrude");
    output.setMeta("profileClosed", profile.closed ? "1" : "0");
    output.setMeta("closed", node.splinePath.isClosed() ? "1" : "0");
    return Result<MeshBuild>::success(std::move(output));
}

bool rayTriangle(Vec3 origin, Vec3 direction, Vec3 a, Vec3 b, Vec3 c, float& distance, Vec3& normal) {
    const Vec3  edge1{b.x - a.x, b.y - a.y, b.z - a.z};
    const Vec3  edge2{c.x - a.x, c.y - a.y, c.z - a.z};
    const Vec3  p{direction.y * edge2.z - direction.z * edge2.y, direction.z * edge2.x - direction.x * edge2.z,
                  direction.x * edge2.y - direction.y * edge2.x};
    const float determinant = edge1.x * p.x + edge1.y * p.y + edge1.z * p.z;
    if (std::abs(determinant) < 1e-7f) return false;
    const float inverse = 1.f / determinant;
    const Vec3  offset{origin.x - a.x, origin.y - a.y, origin.z - a.z};
    const float u = (offset.x * p.x + offset.y * p.y + offset.z * p.z) * inverse;
    if (u < 0.f || u > 1.f) return false;
    const Vec3  q{offset.y * edge1.z - offset.z * edge1.y, offset.z * edge1.x - offset.x * edge1.z,
                  offset.x * edge1.y - offset.y * edge1.x};
    const float v = (direction.x * q.x + direction.y * q.y + direction.z * q.z) * inverse;
    if (v < 0.f || u + v > 1.f) return false;
    distance = (edge2.x * q.x + edge2.y * q.y + edge2.z * q.z) * inverse;
    if (distance < 1e-6f) return false;
    normal                   = {edge1.y * edge2.z - edge1.z * edge2.y, edge1.z * edge2.x - edge1.x * edge2.z,
                                edge1.x * edge2.y - edge1.y * edge2.x};
    const float normalLength = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
    if (normalLength < 1e-7f) return false;
    normal = {normal.x / normalLength, normal.y / normalLength, normal.z / normalLength};
    return true;
}

Result<MeshBuild> meshFitMesh(const MeshBuild& input, const MeshBuild& surface, const auto& node) {
    MeshBuild   output    = input;
    auto&       positions = output.positions();
    Vec3        direction{parameter(node, "directionX", 0.f), parameter(node, "directionY", -1.f),
                          parameter(node, "directionZ", 0.f)};
    const float directionLength =
        std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
    direction           = {direction.x / directionLength, direction.y / directionLength, direction.z / directionLength};
    const float maximum = parameter(node, "maxDistance", 10.f);
    const float surfaceOffset   = parameter(node, "surfaceOffset", 0.f);
    const bool  bidirectional   = intParameter(node, "bidirectional", 0) != 0;
    const auto& targetPositions = surface.positions();
    const auto& targetIndices   = surface.indices();
    for (int vertex = 0; vertex < output.getVertexCount(); ++vertex) {
        const std::size_t base = static_cast<std::size_t>(vertex) * 3u;
        const Vec3        origin{positions[base], positions[base + 1], positions[base + 2]};
        float             best = maximum + 1.f;
        Vec3              bestDirection{}, bestNormal{};
        for (const float sign : {1.f, -1.f}) {
            if (sign < 0.f && !bidirectional) continue;
            const Vec3 ray{direction.x * sign, direction.y * sign, direction.z * sign};
            for (std::size_t triangle = 0; triangle + 2u < targetIndices.size(); triangle += 3u) {
                const auto point = [&](std::uint32_t index) {
                    const std::size_t target = static_cast<std::size_t>(index) * 3u;
                    return Vec3{targetPositions[target], targetPositions[target + 1], targetPositions[target + 2]};
                };
                float distance = 0.f;
                Vec3  normal{};
                if (!rayTriangle(origin, ray, point(targetIndices[triangle]), point(targetIndices[triangle + 1u]),
                                 point(targetIndices[triangle + 2u]), distance, normal) ||
                    distance > maximum || distance >= best)
                    continue;
                best          = distance;
                bestDirection = ray;
                bestNormal    = normal;
            }
        }
        if (best > maximum) continue;
        positions[base]     = origin.x + bestDirection.x * best + bestNormal.x * surfaceOffset;
        positions[base + 1] = origin.y + bestDirection.y * best + bestNormal.y * surfaceOffset;
        positions[base + 2] = origin.z + bestDirection.z * best + bestNormal.z * surfaceOffset;
    }
    recalculateNormals(output);
    output.setMeta("deformer", "deform.meshFit");
    return Result<MeshBuild>::success(std::move(output));
}

Result<MeshBuild> ffdMesh(const MeshBuild& input, const auto& node) {
    MeshBuild output    = input;
    auto&     positions = output.positions();
    if (positions.empty()) return Result<MeshBuild>::success(std::move(output));
    Vec3 minimum{positions[0], positions[1], positions[2]};
    Vec3 maximum = minimum;
    for (std::size_t i = 0; i < positions.size(); i += 3u) {
        minimum = {std::min(minimum.x, positions[i]), std::min(minimum.y, positions[i + 1]),
                   std::min(minimum.z, positions[i + 2])};
        maximum = {std::max(maximum.x, positions[i]), std::max(maximum.y, positions[i + 1]),
                   std::max(maximum.z, positions[i + 2])};
    }
    const Vec3          extent{std::max(maximum.x - minimum.x, 1e-6f), std::max(maximum.y - minimum.y, 1e-6f),
                               std::max(maximum.z - minimum.z, 1e-6f)};
    std::array<Vec3, 8> offsets;
    const std::array<const char*, 8> names{"p000", "p001", "p010", "p011", "p100", "p101", "p110", "p111"};
    for (std::size_t i = 0; i < names.size(); ++i) {
        offsets[i] = {parameter(node, std::string(names[i]) + "x", 0.f),
                      parameter(node, std::string(names[i]) + "y", 0.f),
                      parameter(node, std::string(names[i]) + "z", 0.f)};
    }
    const float weight = std::clamp(parameter(node, "weight", 1.f), 0.f, 1.f);
    for (std::size_t base = 0; base < positions.size(); base += 3u) {
        const float x = std::clamp((positions[base] - minimum.x) / extent.x, 0.f, 1.f);
        const float y = std::clamp((positions[base + 1] - minimum.y) / extent.y, 0.f, 1.f);
        const float z = std::clamp((positions[base + 2] - minimum.z) / extent.z, 0.f, 1.f);
        Vec3        displacement;
        for (int ix = 0; ix < 2; ++ix)
            for (int iy = 0; iy < 2; ++iy)
                for (int iz = 0; iz < 2; ++iz) {
                    const float blend  = (ix ? x : 1.f - x) * (iy ? y : 1.f - y) * (iz ? z : 1.f - z);
                    const auto& offset = offsets[static_cast<std::size_t>(ix * 4 + iy * 2 + iz)];
                    displacement.x += offset.x * blend;
                    displacement.y += offset.y * blend;
                    displacement.z += offset.z * blend;
                }
        positions[base] += displacement.x * weight;
        positions[base + 1] += displacement.y * weight;
        positions[base + 2] += displacement.z * weight;
    }
    recalculateNormals(output);
    return Result<MeshBuild>::success(std::move(output));
}

Result<MeshBuild> morphMesh(const MeshBuild& first, const MeshBuild& second, float weight) {
    if (first.getVertexCount() != second.getVertexCount() || first.indices() != second.indices())
        return graphFailureValue<MeshBuild>(DiagnosticCode::TypeMismatch,
                                            "deform.morph inputs must have identical topology", "deform.morph");
    MeshBuild output = first;
    weight           = std::clamp(weight, 0.f, 1.f);
    for (std::size_t i = 0; i < output.positions().size(); ++i)
        output.positions()[i] = std::lerp(first.positions()[i], second.positions()[i], weight);
    recalculateNormals(output);
    return Result<MeshBuild>::success(std::move(output));
}

Result<MeshBuild> subdivideMesh(const MeshBuild& input, int levels) {
    MeshBuild current = input;
    levels            = std::clamp(levels, 1, 4);
    for (int level = 0; level < levels; ++level) {
        MeshBuild next;
        next.reserve(current.getIndexCount() * 2, current.getIndexCount() * 4);
        std::vector<int> assignments;
        auto             addSource = [&](std::uint32_t index) {
            next.addVertex(current.getPositionX(static_cast<int>(index)), current.getPositionY(static_cast<int>(index)),
                           current.getPositionZ(static_cast<int>(index)), current.getNormalX(static_cast<int>(index)),
                           current.getNormalY(static_cast<int>(index)), current.getNormalZ(static_cast<int>(index)),
                           current.getUvU(static_cast<int>(index)), current.getUvV(static_cast<int>(index)));
            return static_cast<std::uint32_t>(next.getVertexCount() - 1);
        };
        auto midpoint = [&](std::uint32_t a, std::uint32_t b) {
            next.addVertex((current.getPositionX(a) + current.getPositionX(b)) * 0.5f,
                           (current.getPositionY(a) + current.getPositionY(b)) * 0.5f,
                           (current.getPositionZ(a) + current.getPositionZ(b)) * 0.5f,
                           (current.getNormalX(a) + current.getNormalX(b)) * 0.5f,
                           (current.getNormalY(a) + current.getNormalY(b)) * 0.5f,
                           (current.getNormalZ(a) + current.getNormalZ(b)) * 0.5f,
                           (current.getUvU(a) + current.getUvU(b)) * 0.5f,
                           (current.getUvV(a) + current.getUvV(b)) * 0.5f);
            return static_cast<std::uint32_t>(next.getVertexCount() - 1);
        };
        for (int triangle = 0; triangle * 3 + 2 < current.getIndexCount(); ++triangle) {
            const auto a0 = static_cast<std::uint32_t>(current.getIndex(triangle * 3));
            const auto b0 = static_cast<std::uint32_t>(current.getIndex(triangle * 3 + 1));
            const auto c0 = static_cast<std::uint32_t>(current.getIndex(triangle * 3 + 2));
            const auto a = addSource(a0), b = addSource(b0), c = addSource(c0);
            const auto ab = midpoint(a0, b0), bc = midpoint(b0, c0), ca = midpoint(c0, a0);
            for (const auto& tri :
                 {std::array{a, ab, ca}, std::array{ab, b, bc}, std::array{ca, bc, c}, std::array{ab, bc, ca}}) {
                next.addTriangle(tri[0], tri[1], tri[2]);
                assignments.push_back(current.getTriangleGroup(triangle));
            }
        }
        std::vector<std::string> names;
        for (int i = 0; i < current.getGroupCount(); ++i) names.push_back(current.getGroupName(i));
        auto restored = next.restoreGroupData(std::move(names), std::move(assignments), -1);
        if (!restored.ok()) return Result<MeshBuild>::failure(restored.status());
        copyMetadata(current, next);
        recalculateNormals(next);
        current = std::move(next);
    }
    return Result<MeshBuild>::success(std::move(current));
}

struct ClipVertex {
    Vec3  position;
    Vec3  normal;
    float u = 0.f;
    float v = 0.f;
};

ClipVertex interpolate(const ClipVertex& a, const ClipVertex& b, float t) {
    return {{std::lerp(a.position.x, b.position.x, t), std::lerp(a.position.y, b.position.y, t),
             std::lerp(a.position.z, b.position.z, t)},
            {std::lerp(a.normal.x, b.normal.x, t), std::lerp(a.normal.y, b.normal.y, t),
             std::lerp(a.normal.z, b.normal.z, t)},
            std::lerp(a.u, b.u, t),
            std::lerp(a.v, b.v, t)};
}

Result<MeshBuild> cutPlaneMesh(const MeshBuild& input, const auto& node) {
    Vec3 normal{parameter(node, "normalX", 0.f), parameter(node, "normalY", 1.f), parameter(node, "normalZ", 0.f)};
    const float length = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
    if (length < 1e-7f)
        return graphFailureValue<MeshBuild>(DiagnosticCode::InvalidArgument, "mesh.cutPlane requires a non-zero normal",
                                            "normal");
    normal                     = {normal.x / length, normal.y / length, normal.z / length};
    const float distance       = parameter(node, "distance", 0.f);
    const float sign           = intParameter(node, "keepPositive", 1) != 0 ? 1.f : -1.f;
    auto        signedDistance = [&](const ClipVertex& vertex) {
        return sign *
               (vertex.position.x * normal.x + vertex.position.y * normal.y + vertex.position.z * normal.z - distance);
    };
    MeshBuild output;
    output.reserve(input.getIndexCount() * 2, input.getIndexCount() * 2);
    std::vector<int>                 assignments;
    std::vector<std::array<Vec3, 2>> cutSegments;
    for (int triangle = 0; triangle * 3 + 2 < input.getIndexCount(); ++triangle) {
        std::vector<ClipVertex> polygon;
        for (int corner = 0; corner < 3; ++corner) {
            const int index = input.getIndex(triangle * 3 + corner);
            polygon.push_back({{input.getPositionX(index), input.getPositionY(index), input.getPositionZ(index)},
                               {input.getNormalX(index), input.getNormalY(index), input.getNormalZ(index)},
                               input.getUvU(index),
                               input.getUvV(index)});
        }
        std::vector<ClipVertex> clipped;
        std::vector<Vec3>       intersections;
        for (std::size_t i = 0; i < polygon.size(); ++i) {
            const auto& a  = polygon[i];
            const auto& b  = polygon[(i + 1u) % polygon.size()];
            const float da = signedDistance(a), db = signedDistance(b);
            const bool  insideA = da >= 0.f, insideB = db >= 0.f;
            if (insideA) clipped.push_back(a);
            if (insideA != insideB) {
                auto intersection = interpolate(a, b, da / (da - db));
                clipped.push_back(intersection);
                intersections.push_back(intersection.position);
            }
        }
        if (intersections.size() == 2u) cutSegments.push_back({intersections[0], intersections[1]});
        if (clipped.size() < 3u) continue;
        const auto base = static_cast<std::uint32_t>(output.getVertexCount());
        for (const auto& vertex : clipped)
            output.addVertex(vertex.position.x, vertex.position.y, vertex.position.z, vertex.normal.x, vertex.normal.y,
                             vertex.normal.z, vertex.u, vertex.v);
        for (std::size_t i = 1; i + 1 < clipped.size(); ++i) {
            output.addTriangle(base, base + static_cast<std::uint32_t>(i), base + static_cast<std::uint32_t>(i + 1));
            assignments.push_back(input.getTriangleGroup(triangle));
        }
    }
    std::vector<std::string> names;
    for (int i = 0; i < input.getGroupCount(); ++i) names.push_back(input.getGroupName(i));
    if (intParameter(node, "cap", 0) != 0 && !cutSegments.empty()) {
        const int capGroup = static_cast<int>(names.size());
        names.push_back("__cut_cap");
        std::vector<Vec3>               points;
        std::vector<std::array<int, 2>> edges;
        auto                            pointIndex = [&](const Vec3& point) {
            constexpr float toleranceSquared = 1e-10f;
            for (std::size_t i = 0; i < points.size(); ++i) {
                const float dx = points[i].x - point.x, dy = points[i].y - point.y, dz = points[i].z - point.z;
                if (dx * dx + dy * dy + dz * dz <= toleranceSquared) return static_cast<int>(i);
            }
            points.push_back(point);
            return static_cast<int>(points.size() - 1u);
        };
        for (const auto& segment : cutSegments) {
            const int a = pointIndex(segment[0]), b = pointIndex(segment[1]);
            if (a == b) continue;
            const auto duplicate = std::find_if(edges.begin(), edges.end(), [&](const auto& edge) {
                return (edge[0] == a && edge[1] == b) || (edge[0] == b && edge[1] == a);
            });
            if (duplicate == edges.end()) edges.push_back({a, b});
        }
        std::vector<bool> used(edges.size(), false);
        for (std::size_t seed = 0; seed < edges.size(); ++seed) {
            if (used[seed]) continue;
            std::vector<int> loop{edges[seed][0], edges[seed][1]};
            used[seed] = true;
            while (loop.back() != loop.front()) {
                bool extended = false;
                for (std::size_t edge = 0; edge < edges.size(); ++edge) {
                    if (used[edge]) continue;
                    if (edges[edge][0] == loop.back() || edges[edge][1] == loop.back()) {
                        loop.push_back(edges[edge][0] == loop.back() ? edges[edge][1] : edges[edge][0]);
                        used[edge] = true;
                        extended   = true;
                        break;
                    }
                }
                if (!extended || loop.size() > edges.size() + 1u) break;
            }
            if (loop.size() < 4u || loop.back() != loop.front()) continue;
            loop.pop_back();
            Vec3 centroid;
            for (const int index : loop) {
                centroid.x += points[static_cast<std::size_t>(index)].x;
                centroid.y += points[static_cast<std::size_t>(index)].y;
                centroid.z += points[static_cast<std::size_t>(index)].z;
            }
            const float inverseCount = 1.f / static_cast<float>(loop.size());
            centroid = {centroid.x * inverseCount, centroid.y * inverseCount, centroid.z * inverseCount};
            const Vec3 capNormal{-normal.x * sign, -normal.y * sign, -normal.z * sign};
            Vec3       winding;
            for (std::size_t i = 0; i < loop.size(); ++i) {
                const auto& a = points[static_cast<std::size_t>(loop[i])];
                const auto& b = points[static_cast<std::size_t>(loop[(i + 1u) % loop.size()])];
                winding.x += a.y * b.z - a.z * b.y;
                winding.y += a.z * b.x - a.x * b.z;
                winding.z += a.x * b.y - a.y * b.x;
            }
            if (winding.x * capNormal.x + winding.y * capNormal.y + winding.z * capNormal.z < 0.f)
                std::reverse(loop.begin(), loop.end());
            const auto centerIndex = static_cast<std::uint32_t>(output.getVertexCount());
            output.addVertex(centroid.x, centroid.y, centroid.z, capNormal.x, capNormal.y, capNormal.z, 0.5f, 0.5f);
            for (const int index : loop) {
                const auto& point = points[static_cast<std::size_t>(index)];
                output.addVertex(point.x, point.y, point.z, capNormal.x, capNormal.y, capNormal.z,
                                 0.5f + (point.x - centroid.x), 0.5f + (point.z - centroid.z));
            }
            for (std::size_t i = 0; i < loop.size(); ++i) {
                const auto a = centerIndex + 1u + static_cast<std::uint32_t>(i);
                const auto b = centerIndex + 1u + static_cast<std::uint32_t>((i + 1u) % loop.size());
                output.addTriangle(centerIndex, a, b);
                assignments.push_back(capGroup);
            }
        }
    }
    auto restored = output.restoreGroupData(std::move(names), std::move(assignments), -1);
    if (!restored.ok()) return Result<MeshBuild>::failure(restored.status());
    copyMetadata(input, output);
    return Result<MeshBuild>::success(std::move(output));
}

struct WeldKey {
    std::int64_t x, y, z;
    bool         operator==(const WeldKey&) const = default;
};

struct WeldKeyHash {
    std::size_t operator()(const WeldKey& key) const noexcept {
        std::size_t value = std::hash<std::int64_t>{}(key.x);
        value ^= std::hash<std::int64_t>{}(key.y) + 0x9e3779b9u + (value << 6u) + (value >> 2u);
        value ^= std::hash<std::int64_t>{}(key.z) + 0x9e3779b9u + (value << 6u) + (value >> 2u);
        return value;
    }
};

Result<MeshBuild> weldMesh(const MeshBuild& input, float tolerance) {
    if (!std::isfinite(tolerance) || tolerance <= 0.f)
        return graphFailureValue<MeshBuild>(DiagnosticCode::InvalidArgument,
                                            "mesh.weld tolerance must be finite and positive", "tolerance");
    MeshBuild output;
    output.reserve(input.getVertexCount(), input.getIndexCount());
    std::unordered_map<WeldKey, std::uint32_t, WeldKeyHash> vertices;
    std::vector<std::uint32_t>                              remap(static_cast<std::size_t>(input.getVertexCount()));
    for (int i = 0; i < input.getVertexCount(); ++i) {
        const WeldKey key{std::llround(input.getPositionX(i) / tolerance),
                          std::llround(input.getPositionY(i) / tolerance),
                          std::llround(input.getPositionZ(i) / tolerance)};
        const auto    found = vertices.find(key);
        if (found != vertices.end()) {
            remap[static_cast<std::size_t>(i)] = found->second;
            continue;
        }
        const auto next = static_cast<std::uint32_t>(output.getVertexCount());
        vertices.emplace(key, next);
        remap[static_cast<std::size_t>(i)] = next;
        output.addVertex(input.getPositionX(i), input.getPositionY(i), input.getPositionZ(i), input.getNormalX(i),
                         input.getNormalY(i), input.getNormalZ(i), input.getUvU(i), input.getUvV(i));
    }
    std::vector<int> assignments;
    for (int i = 0; i + 2 < input.getIndexCount(); i += 3) {
        const auto a = remap[static_cast<std::size_t>(input.getIndex(i))];
        const auto b = remap[static_cast<std::size_t>(input.getIndex(i + 1))];
        const auto c = remap[static_cast<std::size_t>(input.getIndex(i + 2))];
        if (a == b || b == c || a == c) continue;
        output.addTriangle(a, b, c);
        assignments.push_back(input.getTriangleGroup(i / 3));
    }
    std::vector<std::string> names;
    for (int i = 0; i < input.getGroupCount(); ++i) names.push_back(input.getGroupName(i));
    auto restored = output.restoreGroupData(std::move(names), std::move(assignments), -1);
    if (!restored.ok()) return Result<MeshBuild>::failure(restored.status());
    for (const auto& [key, value] : input.metadata()) output.setMeta(key, value);
    recalculateNormals(output);
    return Result<MeshBuild>::success(std::move(output));
}

}  // namespace

Result<void> MeshModifierGraph::addNode(std::string id, std::string operation) {
    if (id.empty()) return graphFailure(DiagnosticCode::InvalidArgument, "mesh modifier node id is empty", "node.id");
    const auto* spec = findSpec(operation);
    if (!spec)
        return graphFailure(DiagnosticCode::NotFound, "unknown mesh modifier operation: " + operation,
                            "node.operation");
    if (nodes_.contains(id))
        return graphFailure(DiagnosticCode::AlreadyExists, "duplicate mesh modifier node: " + id, "node.id");
    Node node;
    node.id        = id;
    node.operation = std::move(operation);
    node.inputs.resize(static_cast<std::size_t>(spec->inputs));
    for (const auto& param : spec->params) {
        if (std::string_view(param.kind) == "float")
            node.floats[param.key] = std::stof(param.defaultValue);
        else if (std::string_view(param.kind) == "int")
            node.ints[param.key] = std::stoi(param.defaultValue);
        else
            node.strings[param.key] = param.defaultValue;
    }
    nodes_.emplace(id, std::move(node));
    nodeOrder_.push_back(std::move(id));
    invalidate();
    return Result<void>::success();
}

Result<void> MeshModifierGraph::removeNode(std::string_view id) {
    const std::string key(id);
    if (!nodes_.erase(key))
        return graphFailure(DiagnosticCode::NotFound, "mesh modifier node not found: " + key, "node.id");
    std::erase(nodeOrder_, key);
    for (auto& [_, node] : nodes_)
        for (auto& input : node.inputs)
            if (input == key) input.clear();
    invalidate();
    return Result<void>::success();
}

Result<void> MeshModifierGraph::connect(std::string_view fromId, std::string_view toId, int inputIndex) {
    const auto source = nodes_.find(std::string(fromId));
    const auto target = nodes_.find(std::string(toId));
    if (source == nodes_.end() || target == nodes_.end())
        return graphFailure(DiagnosticCode::NotFound, "mesh modifier connection references an unknown node", "edge");
    if (source == target)
        return graphFailure(DiagnosticCode::Conflict, "mesh modifier node cannot connect to itself", "edge");
    if (inputIndex < 0 || inputIndex >= static_cast<int>(target->second.inputs.size()))
        return graphFailure(DiagnosticCode::InvalidArgument, "mesh modifier input index is out of range", "edge.input");
    target->second.inputs[static_cast<std::size_t>(inputIndex)] = source->first;
    invalidate();
    return Result<void>::success();
}

Result<void> MeshModifierGraph::disconnect(std::string_view toId, int inputIndex) {
    const auto target = nodes_.find(std::string(toId));
    if (target == nodes_.end())
        return graphFailure(DiagnosticCode::NotFound, "mesh modifier node not found", "edge.to");
    if (inputIndex < 0 || inputIndex >= static_cast<int>(target->second.inputs.size()))
        return graphFailure(DiagnosticCode::InvalidArgument, "mesh modifier input index is out of range", "edge.input");
    target->second.inputs[static_cast<std::size_t>(inputIndex)].clear();
    invalidate();
    return Result<void>::success();
}

Result<void> MeshModifierGraph::setNodeMesh(std::string_view id, const MeshBuild& mesh) {
    const auto found = nodes_.find(std::string(id));
    if (found == nodes_.end()) return graphFailure(DiagnosticCode::NotFound, "mesh modifier node not found", "node.id");
    if (found->second.operation != "mesh.input")
        return graphFailure(DiagnosticCode::PreconditionViolation, "only mesh.input accepts a mesh snapshot",
                            "node.operation");
    auto valid = validateMesh(mesh, "mesh");
    if (!valid.ok()) return valid;
    found->second.inputMesh    = mesh;
    found->second.hasInputMesh = true;
    invalidate();
    return Result<void>::success();
}

Result<void> MeshModifierGraph::setNodeSplinePath(std::string_view id, const SplinePath& path) {
    const auto found = nodes_.find(std::string(id));
    if (found == nodes_.end()) return graphFailure(DiagnosticCode::NotFound, "mesh modifier node not found", "node.id");
    if (found->second.operation != "deform.splinePath" && found->second.operation != "mesh.splineTube" &&
        found->second.operation != "mesh.splineRibbon" && found->second.operation != "mesh.splineExtrude")
        return graphFailure(DiagnosticCode::TypeMismatch,
                            "spline paths can only be bound to spline deformation or generation nodes",
                            std::string(id));
    auto ready = path.evaluateResult(0.f);
    if (!ready.ok()) return Result<void>::failure(ready.status());
    found->second.splinePath    = path;
    found->second.hasSplinePath = true;
    invalidate();
    return Result<void>::success();
}

Result<void> MeshModifierGraph::setNodeSplineProfile(std::string_view id, const SplineProfile& profile) {
    const auto found = nodes_.find(std::string(id));
    if (found == nodes_.end()) return graphFailure(DiagnosticCode::NotFound, "mesh modifier node not found", "node.id");
    if (found->second.operation != "mesh.splineExtrude")
        return graphFailure(DiagnosticCode::TypeMismatch, "profiles can only be bound to mesh.splineExtrude",
                            std::string(id));
    const std::size_t minimum = profile.closed ? 3u : 2u;
    if (profile.points.size() < minimum || profile.points.size() > 256u)
        return graphFailure(DiagnosticCode::InvalidArgument, "spline profile point count is out of range",
                            "profile.points");
    for (std::size_t i = 0; i < profile.points.size(); ++i) {
        const auto& point = profile.points[i];
        const auto& next  = profile.points[(i + 1u) % profile.points.size()];
        if (!std::isfinite(point.x) || !std::isfinite(point.y))
            return graphFailure(DiagnosticCode::InvalidArgument, "spline profile points must be finite",
                                "profile.points");
        if ((profile.closed || i + 1u < profile.points.size()) &&
            std::hypot(next.x - point.x, next.y - point.y) < 1e-6f)
            return graphFailure(DiagnosticCode::InvalidArgument, "spline profile edges must have non-zero length",
                                "profile.points");
    }
    if (profile.closed) {
        auto triangles = triangulateProfile(profile);
        if (!triangles.ok()) return Result<void>::failure(triangles.status());
    }
    found->second.splineProfile    = profile;
    found->second.hasSplineProfile = true;
    invalidate();
    return Result<void>::success();
}

Result<void> MeshModifierGraph::setNodeFloat(std::string_view id, std::string key, float value) {
    if (!std::isfinite(value))
        return graphFailure(DiagnosticCode::InvalidArgument, "mesh modifier float must be finite", key);
    const auto found = nodes_.find(std::string(id));
    if (found == nodes_.end()) return graphFailure(DiagnosticCode::NotFound, "mesh modifier node not found", "node.id");
    const auto* spec  = findSpec(found->second.operation);
    const auto* param = spec ? findParam(*spec, key) : nullptr;
    if (!param || std::string_view(param->kind) != "float")
        return graphFailure(DiagnosticCode::TypeMismatch, "unknown or non-float mesh modifier parameter", key);
    found->second.floats[std::move(key)] = value;
    invalidate();
    return Result<void>::success();
}

Result<void> MeshModifierGraph::setNodeInt(std::string_view id, std::string key, int value) {
    const auto found = nodes_.find(std::string(id));
    if (found == nodes_.end()) return graphFailure(DiagnosticCode::NotFound, "mesh modifier node not found", "node.id");
    const auto* spec  = findSpec(found->second.operation);
    const auto* param = spec ? findParam(*spec, key) : nullptr;
    if (!param || std::string_view(param->kind) != "int")
        return graphFailure(DiagnosticCode::TypeMismatch, "unknown or non-integer mesh modifier parameter", key);
    found->second.ints[std::move(key)] = value;
    invalidate();
    return Result<void>::success();
}

Result<void> MeshModifierGraph::setNodeString(std::string_view id, std::string key, std::string value) {
    const auto found = nodes_.find(std::string(id));
    if (found == nodes_.end()) return graphFailure(DiagnosticCode::NotFound, "mesh modifier node not found", "node.id");
    const auto* spec  = findSpec(found->second.operation);
    const auto* param = spec ? findParam(*spec, key) : nullptr;
    if (!param || std::string_view(param->kind) != "string")
        return graphFailure(DiagnosticCode::TypeMismatch, "unknown or non-string mesh modifier parameter", key);
    found->second.strings[std::move(key)] = std::move(value);
    invalidate();
    return Result<void>::success();
}

Result<void> MeshModifierGraph::validateNode(std::string_view id, std::unordered_map<std::string, int>& states) const {
    const std::string key(id);
    if (states[key] == 2) return Result<void>::success();
    if (states[key] == 1) return graphFailure(DiagnosticCode::Conflict, "cycle at mesh modifier node: " + key, key);
    const auto found = nodes_.find(key);
    if (found == nodes_.end()) return graphFailure(DiagnosticCode::NotFound, "unknown mesh modifier node: " + key, key);
    states[key]      = 1;
    const Node& node = found->second;
    if (node.operation == "mesh.input") {
        if (!node.hasInputMesh)
            return graphFailure(DiagnosticCode::PreconditionViolation, "mesh.input has no bound mesh", key);
        auto valid = validateMesh(node.inputMesh, key);
        if (!valid.ok()) return valid;
    }
    for (std::size_t input = 0; input < node.inputs.size(); ++input) {
        if (node.inputs[input].empty())
            return graphFailure(DiagnosticCode::PreconditionViolation,
                                node.operation + " requires mesh input " + std::to_string(input), key);
        auto valid = validateNode(node.inputs[input], states);
        if (!valid.ok()) return valid;
    }
    const std::string axis = stringParameter(node, "axis", "y");
    if ((node.operation == "deform.bend" || node.operation == "deform.twist" ||
         node.operation == "deform.angularBend" || node.operation == "deform.spline" ||
         node.operation == "deform.splinePath") &&
        axis != "x" && axis != "y" && axis != "z")
        return graphFailure(DiagnosticCode::InvalidArgument, "deformation axis must be x, y, or z", key + ".axis");
    if ((node.operation == "deform.splinePath" || node.operation == "mesh.splineTube" ||
         node.operation == "mesh.splineRibbon" || node.operation == "mesh.splineExtrude") &&
        !node.hasSplinePath)
        return graphFailure(DiagnosticCode::PreconditionViolation, node.operation + " has no bound spline path", key);
    if (node.hasSplinePath && node.splinePath.chunkCount() > 1) {
        if (node.operation == "deform.splinePath")
            return graphFailure(DiagnosticCode::PreconditionViolation, "deform.splinePath requires one connected chunk",
                                key);
        for (int chunk = 0; chunk < node.splinePath.chunkCount(); ++chunk) {
            auto path = node.splinePath.chunkPathResult(chunk);
            if (!path.ok()) return Result<void>::failure(path.status());
        }
    }
    if (node.operation == "deform.splinePath" && std::abs(parameter(node, "scale", 1.f)) < 1e-7f)
        return graphFailure(DiagnosticCode::InvalidArgument, "spline path scale must be non-zero", key + ".scale");
    if (node.operation == "mesh.splineTube") {
        const int pathSegments   = intParameter(node, "pathSegments", 32);
        const int radialSegments = intParameter(node, "radialSegments", 12);
        if (parameter(node, "radius", 0.5f) <= 0.f)
            return graphFailure(DiagnosticCode::InvalidArgument, "spline tube radius must be positive",
                                key + ".radius");
        if (pathSegments < 1 || pathSegments > 4096)
            return graphFailure(DiagnosticCode::InvalidArgument, "spline tube pathSegments must be in [1, 4096]",
                                key + ".pathSegments");
        if (radialSegments < 3 || radialSegments > 256)
            return graphFailure(DiagnosticCode::InvalidArgument, "spline tube radialSegments must be in [3, 256]",
                                key + ".radialSegments");
        const std::uint64_t capVertices = node.splinePath.isClosed() || intParameter(node, "cap", 1) == 0
                                              ? 0u
                                              : 2u * static_cast<std::uint64_t>(radialSegments + 2);
        const std::uint64_t vertexCount =
            static_cast<std::uint64_t>(pathSegments + 1) * static_cast<std::uint64_t>(radialSegments + 1) + capVertices;
        if (vertexCount > 1'000'000u)
            return graphFailure(DiagnosticCode::InvalidArgument, "spline tube exceeds the one-million vertex budget",
                                key);
    }
    if (node.operation == "mesh.splineRibbon") {
        const int pathSegments = intParameter(node, "pathSegments", 32);
        if (parameter(node, "width", 2.f) <= 0.f)
            return graphFailure(DiagnosticCode::InvalidArgument, "spline ribbon width must be positive",
                                key + ".width");
        if (parameter(node, "thickness", 0.f) < 0.f)
            return graphFailure(DiagnosticCode::InvalidArgument, "spline ribbon thickness must be non-negative",
                                key + ".thickness");
        if (pathSegments < 1 || pathSegments > 4096)
            return graphFailure(DiagnosticCode::InvalidArgument, "spline ribbon pathSegments must be in [1, 4096]",
                                key + ".pathSegments");
    }
    if (node.operation == "mesh.splineExtrude") {
        if (!node.hasSplineProfile)
            return graphFailure(DiagnosticCode::PreconditionViolation, "mesh.splineExtrude has no bound profile", key);
        const int pathSegments = intParameter(node, "pathSegments", 32);
        if (pathSegments < 1 || pathSegments > 4096)
            return graphFailure(DiagnosticCode::InvalidArgument, "spline extrusion pathSegments must be in [1, 4096]",
                                key + ".pathSegments");
        const std::uint64_t stride = node.splineProfile.points.size() + (node.splineProfile.closed ? 1u : 0u);
        if (static_cast<std::uint64_t>(pathSegments + 1) * stride > 1'000'000u)
            return graphFailure(DiagnosticCode::InvalidArgument,
                                "spline extrusion exceeds the one-million vertex budget", key);
    }
    if (node.operation == "deform.transform" &&
        (std::abs(parameter(node, "scaleX", 1.f)) < 1e-7f || std::abs(parameter(node, "scaleY", 1.f)) < 1e-7f ||
         std::abs(parameter(node, "scaleZ", 1.f)) < 1e-7f))
        return graphFailure(DiagnosticCode::InvalidArgument, "deformation scale components must be non-zero", key);
    if (node.operation == "deform.soundReact") {
        const float       level = parameter(node, "level", 0.f), threshold = parameter(node, "threshold", 0.f);
        const std::string axis = stringParameter(node, "axis", "y");
        if (level < 0.f || level > 1.f)
            return graphFailure(DiagnosticCode::InvalidArgument, "sound-react level must be in [0, 1]", key + ".level");
        if (threshold < 0.f || threshold > 1.f)
            return graphFailure(DiagnosticCode::InvalidArgument, "sound-react threshold must be in [0, 1]",
                                key + ".threshold");
        if (axis != "x" && axis != "y" && axis != "z")
            return graphFailure(DiagnosticCode::InvalidArgument, "sound-react axis must be x, y, or z", key + ".axis");
        if (parameter(node, "frequency", 1.f) < 0.f)
            return graphFailure(DiagnosticCode::InvalidArgument, "sound-react frequency must be non-negative",
                                key + ".frequency");
    }
    if (node.operation == "deform.effector") {
        const int pointCount = intParameter(node, "pointCount", 1);
        if (pointCount < 1 || pointCount > 4)
            return graphFailure(DiagnosticCode::InvalidArgument, "mesh effector pointCount must be in [1, 4]",
                                key + ".pointCount");
        if (parameter(node, "density", 1.f) <= 0.f)
            return graphFailure(DiagnosticCode::InvalidArgument, "mesh effector density must be positive",
                                key + ".density");
        for (int point = 0; point < pointCount; ++point) {
            const std::string radius = "p" + std::to_string(point) + "radius";
            if (parameter(node, radius, 1.f) <= 0.f)
                return graphFailure(DiagnosticCode::InvalidArgument, "mesh effector radius must be positive",
                                    key + "." + radius);
        }
    }
    if (node.operation == "deform.meshFit") {
        const float x = parameter(node, "directionX", 0.f), y = parameter(node, "directionY", -1.f);
        const float z = parameter(node, "directionZ", 0.f);
        if (x * x + y * y + z * z < 1e-12f)
            return graphFailure(DiagnosticCode::InvalidArgument, "mesh fit direction must be non-zero",
                                key + ".direction");
        if (parameter(node, "maxDistance", 10.f) <= 0.f)
            return graphFailure(DiagnosticCode::InvalidArgument, "mesh fit maxDistance must be positive",
                                key + ".maxDistance");
    }
    states[key] = 2;
    return Result<void>::success();
}

Result<void> MeshModifierGraph::validateResult() const {
    std::unordered_map<std::string, int> states;
    for (const auto& id : nodeOrder_) {
        auto valid = validateNode(id, states);
        if (!valid.ok()) return valid;
    }
    return Result<void>::success();
}

Result<std::vector<MeshModifierGraph::Segment>> MeshModifierGraph::compilePlan(std::string_view outputId) const {
    std::vector<std::string>                        order;
    std::unordered_map<std::string, int>            states;
    std::function<Result<void>(const std::string&)> visit = [&](const std::string& id) -> Result<void> {
        if (states[id] == 2) return Result<void>::success();
        if (states[id] == 1) return graphFailure(DiagnosticCode::Conflict, "cycle at mesh modifier node: " + id, id);
        const auto found = nodes_.find(id);
        if (found == nodes_.end())
            return graphFailure(DiagnosticCode::NotFound, "unknown mesh modifier output: " + id, id);
        states[id] = 1;
        for (const auto& input : found->second.inputs) {
            if (input.empty())
                return graphFailure(DiagnosticCode::PreconditionViolation,
                                    found->second.operation + " has an unconnected input", id);
            auto result = visit(input);
            if (!result.ok()) return result;
        }
        states[id] = 2;
        order.push_back(id);
        return Result<void>::success();
    };
    auto visited = visit(std::string(outputId));
    if (!visited.ok()) return Result<std::vector<Segment>>::failure(visited.status());
    std::unordered_map<std::string, std::vector<std::string>> consumers;
    for (const auto& id : order)
        for (const auto& input : nodes_.at(id).inputs)
            if (!input.empty()) consumers[input].push_back(id);
    std::unordered_set<std::string> assigned;
    std::vector<Segment>            segments;
    for (const auto& id : order) {
        if (assigned.contains(id)) continue;
        Segment segment;
        segment.nodes.push_back(id);
        assigned.insert(id);
        const auto* firstSpec = findSpec(nodes_.at(id).operation);
        std::string cursor    = id;
        while (firstSpec && firstSpec->perVertex && consumers[cursor].size() == 1u) {
            const auto& next     = consumers[cursor].front();
            const auto* nextSpec = findSpec(nodes_.at(next).operation);
            if (!nextSpec || !nextSpec->perVertex || assigned.contains(next)) break;
            segment.nodes.push_back(next);
            assigned.insert(next);
            cursor = next;
        }
        segment.fusedVertexTraversal = segment.nodes.size() > 1u;
        segments.push_back(std::move(segment));
    }
    return Result<std::vector<Segment>>::success(std::move(segments));
}

Result<MeshBuild> MeshModifierGraph::executeFused(const Segment&                                    segment,
                                                  const std::unordered_map<std::string, MeshBuild>& outputs) const {
    const Node& first  = nodes_.at(segment.nodes.front());
    const auto  source = outputs.find(first.inputs.front());
    if (source == outputs.end())
        return graphFailureValue<MeshBuild>(DiagnosticCode::InvariantViolation, "fused mesh input was not evaluated");
    MeshBuild output    = source->second;
    auto&     positions = output.positions();
    auto&     normals   = output.normals();
    for (int vertex = 0; vertex < output.getVertexCount(); ++vertex) {
        const std::size_t p = std::size_t(vertex) * 3u;
        Vec3              position{positions[p], positions[p + 1], positions[p + 2]};
        Vec3              normal{normals[p], normals[p + 1], normals[p + 2]};
        for (const auto& id : segment.nodes) applyMaskedVertexOperation(nodes_.at(id), position, normal);
        positions[p]     = position.x;
        positions[p + 1] = position.y;
        positions[p + 2] = position.z;
        normals[p]       = normal.x;
        normals[p + 1]   = normal.y;
        normals[p + 2]   = normal.z;
    }
    return Result<MeshBuild>::success(std::move(output));
}

Result<MeshBuild> MeshModifierGraph::executeNode(const Node&                                       node,
                                                 const std::unordered_map<std::string, MeshBuild>& outputs) const {
    if (node.operation == "mesh.input") return Result<MeshBuild>::success(node.inputMesh);
    if (node.operation == "mesh.splineTube") return splineTubeMesh(node);
    if (node.operation == "mesh.splineRibbon") return splineRibbonMesh(node);
    if (node.operation == "mesh.splineExtrude") return splineExtrudeMesh(node);
    const auto first = outputs.find(node.inputs.front());
    if (first == outputs.end())
        return graphFailureValue<MeshBuild>(DiagnosticCode::InvariantViolation, "mesh node input was not evaluated",
                                            node.id);
    if (const auto* spec = findSpec(node.operation); spec && spec->perVertex) {
        MeshBuild output    = first->second;
        auto&     positions = output.positions();
        auto&     normals   = output.normals();
        for (int vertex = 0; vertex < output.getVertexCount(); ++vertex) {
            const std::size_t p = std::size_t(vertex) * 3u;
            Vec3              position{positions[p], positions[p + 1], positions[p + 2]};
            Vec3              normal{normals[p], normals[p + 1], normals[p + 2]};
            applyMaskedVertexOperation(node, position, normal);
            positions[p]     = position.x;
            positions[p + 1] = position.y;
            positions[p + 2] = position.z;
            normals[p]       = normal.x;
            normals[p + 1]   = normal.y;
            normals[p + 2]   = normal.z;
        }
        return Result<MeshBuild>::success(std::move(output));
    }
    if (node.operation == "mesh.output") return Result<MeshBuild>::success(first->second);
    if (node.operation == "deform.angularBend") return angularBendMesh(first->second, node);
    if (node.operation == "deform.ffd") return ffdMesh(first->second, node);
    if (node.operation == "deform.spline") return splineMesh(first->second, node);
    if (node.operation == "deform.splinePath") return splinePathMesh(first->second, node);
    if (node.operation == "deform.morph") {
        const auto second = outputs.find(node.inputs[1]);
        if (second == outputs.end())
            return graphFailureValue<MeshBuild>(DiagnosticCode::InvariantViolation,
                                                "deform.morph second input was not evaluated", node.id);
        return morphMesh(first->second, second->second, parameter(node, "weight", 0.5f));
    }
    if (node.operation == "deform.meshFit") {
        const auto second = outputs.find(node.inputs[1]);
        if (second == outputs.end())
            return graphFailureValue<MeshBuild>(DiagnosticCode::InvariantViolation,
                                                "deform.meshFit surface input was not evaluated", node.id);
        return meshFitMesh(first->second, second->second, node);
    }
    if (node.operation == "deform.smooth")
        return smoothMesh(first->second, parameter(node, "strength", 0.5f), intParameter(node, "iterations", 1));
    if (node.operation == "mesh.subdivide") return subdivideMesh(first->second, intParameter(node, "levels", 1));
    if (node.operation == "mesh.cutPlane") return cutPlaneMesh(first->second, node);
    if (node.operation == "mesh.append") {
        const auto second = outputs.find(node.inputs[1]);
        if (second == outputs.end())
            return graphFailureValue<MeshBuild>(DiagnosticCode::InvariantViolation,
                                                "mesh.append second input was not evaluated", node.id);
        MeshBuild output = first->second;
        if (!output.appendTransformed(&second->second, 0.f, 0.f, 0.f, 0.f, 1.f, 1.f, 1.f))
            return graphFailureValue<MeshBuild>(DiagnosticCode::InvalidArgument,
                                                "mesh.append rejected an empty or invalid input", node.id);
        return Result<MeshBuild>::success(std::move(output));
    }
    if (node.operation == "mesh.boolean") {
        const auto second = outputs.find(node.inputs[1]);
        if (second == outputs.end())
            return graphFailureValue<MeshBuild>(DiagnosticCode::PreconditionViolation,
                                                "mesh.boolean second input was not evaluated", node.id);
        return meshBooleanResult(first->second, second->second,
                                 stringParameter(node, "operation", "difference"));
    }
    if (node.operation == "mesh.projectUv")
        return projectMeshUvResult(first->second, stringParameter(node, "mode", "box"),
                                   parameter(node, "scale", 1.f), parameter(node, "offsetU", 0.f),
                                   parameter(node, "offsetV", 0.f));
    if (node.operation == "mesh.weld") return weldMesh(first->second, parameter(node, "tolerance", 0.0001f));
    return graphFailureValue<MeshBuild>(DiagnosticCode::Unsupported,
                                        "unsupported mesh modifier operation: " + node.operation, node.id);
}

Result<MeshBuild> MeshModifierGraph::executeResult(std::string_view outputId) {
    const auto requested = nodes_.find(std::string(outputId));
    if (requested == nodes_.end())
        return graphFailureValue<MeshBuild>(
            DiagnosticCode::NotFound, "unknown mesh modifier output: " + std::string(outputId), std::string(outputId));
    if (requested->second.cacheValid) {
        metrics_              = {{requested->first, requested->second.cache.getVertexCount(),
                                  requested->second.cache.getIndexCount() / 3, 0.f, true, false}};
        compiledSegmentCount_ = 0;
        fusedOperationCount_  = 0;
        return Result<MeshBuild>::success(requested->second.cache);
    }
    std::unordered_map<std::string, int> validationStates;
    auto                                 valid = validateNode(outputId, validationStates);
    if (!valid.ok()) return Result<MeshBuild>::failure(valid.status());
    auto planned = compilePlan(outputId);
    if (!planned.ok()) return Result<MeshBuild>::failure(planned.status());
    const auto                                 plan = std::move(planned).takeValue();
    std::unordered_map<std::string, MeshBuild> outputs;
    std::vector<MeshModifierNodeMetric>        candidateMetrics;
    int                                        candidateFused = 0;
    for (const auto& segment : plan) {
        const auto        started   = std::chrono::steady_clock::now();
        Result<MeshBuild> evaluated = segment.fusedVertexTraversal
                                          ? executeFused(segment, outputs)
                                          : executeNode(nodes_.at(segment.nodes.front()), outputs);
        if (!evaluated.ok()) return Result<MeshBuild>::failure(evaluated.status());
        MeshBuild  output = std::move(evaluated).takeValue();
        const auto elapsed =
            std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
        const std::string& outputNode = segment.nodes.back();
        candidateMetrics.push_back({outputNode, output.getVertexCount(), output.getIndexCount() / 3, elapsed, false,
                                    segment.fusedVertexTraversal});
        if (segment.fusedVertexTraversal) candidateFused += static_cast<int>(segment.nodes.size());
        outputs.emplace(outputNode, std::move(output));
    }
    const auto found = outputs.find(std::string(outputId));
    if (found == outputs.end())
        return graphFailureValue<MeshBuild>(DiagnosticCode::InvariantViolation, "mesh modifier output was not produced",
                                            std::string(outputId));
    for (auto& [id, node] : nodes_) {
        const auto output = outputs.find(id);
        if (output != outputs.end()) {
            node.cache      = output->second;
            node.cacheValid = true;
        }
    }
    metrics_              = std::move(candidateMetrics);
    compiledSegmentCount_ = static_cast<int>(plan.size());
    fusedOperationCount_  = candidateFused;
    ++executionPlanBuildCount_;
    return Result<MeshBuild>::success(found->second);
}

void MeshModifierGraph::clearCache() {
    for (auto& [_, node] : nodes_) {
        node.cache.clear();
        node.cacheValid = false;
    }
    metrics_.clear();
    compiledSegmentCount_ = 0;
    fusedOperationCount_  = 0;
}

void MeshModifierGraph::invalidate() {
    clearCache();
    ++revision_;
}

bool MeshModifierGraph::hasNode(std::string_view id) const { return nodes_.contains(std::string(id)); }

std::string MeshModifierGraph::nodeId(int index) const {
    return index >= 0 && index < static_cast<int>(nodeOrder_.size()) ? nodeOrder_[static_cast<std::size_t>(index)]
                                                                     : std::string();
}

std::string MeshModifierGraph::nodeOperation(std::string_view id) const {
    const auto found = nodes_.find(std::string(id));
    return found == nodes_.end() ? std::string() : found->second.operation;
}

int         MeshModifierGraph::operationCount() { return static_cast<int>(operationSpecs().size()); }
std::string MeshModifierGraph::operationId(int index) {
    return index >= 0 && index < operationCount() ? operationSpecs()[static_cast<std::size_t>(index)].id
                                                  : std::string();
}
int MeshModifierGraph::operationInputCount(std::string_view operation) {
    const auto* spec = findSpec(operation);
    return spec ? spec->inputs : -1;
}
int MeshModifierGraph::operationParamCount(std::string_view operation) {
    const auto* spec = findSpec(operation);
    return spec ? static_cast<int>(spec->params.size()) : 0;
}
std::string MeshModifierGraph::operationParamKey(std::string_view operation, int index) {
    const auto* spec = findSpec(operation);
    return spec && index >= 0 && index < static_cast<int>(spec->params.size())
               ? spec->params[static_cast<std::size_t>(index)].key
               : std::string();
}
std::string MeshModifierGraph::operationParamKind(std::string_view operation, int index) {
    const auto* spec = findSpec(operation);
    return spec && index >= 0 && index < static_cast<int>(spec->params.size())
               ? spec->params[static_cast<std::size_t>(index)].kind
               : std::string();
}
std::string MeshModifierGraph::operationParamDefault(std::string_view operation, int index) {
    const auto* spec = findSpec(operation);
    return spec && index >= 0 && index < static_cast<int>(spec->params.size())
               ? spec->params[static_cast<std::size_t>(index)].defaultValue
               : std::string();
}

}  // namespace eve::procgen
