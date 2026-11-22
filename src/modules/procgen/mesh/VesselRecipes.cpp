#include <array>
#include <cmath>
#include <limits>
#include <utility>
#include "procgen/algorithms/MarchingCubes.h"
#include "procgen/mesh/VesselGenerator.h"
namespace eve::procgen {
namespace {
struct Shape {
    const char*          name;
    float                height;
    std::array<float, 4> radii;
};
constexpr Shape shapes[] = {
    {"planter", 1.2f, {0.55f, 0.65f, 0.85f, 0.9f}}, {"jar", 1.6f, {0.6f, 1.3f, 1.3f, 0.65f}},
    {"bowl", 0.65f, {0.35f, 0.45f, 0.9f, 1.f}},     {"plate", 0.18f, {0.65f, 0.7f, 0.95f, 1.1f}},
    {"saucer", 0.1f, {0.5f, 0.55f, 0.8f, 0.85f}},   {"cylinder", 1.f, {0.8f, 0.8f, 0.8f, 0.8f}}};
eve::Result<MeshBuild> build(const Params& params) {
    auto invalid = [] {
        return eve::Result<MeshBuild>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "VesselParametersInvalid: Unknown shape/material or invalid curve, thickness, height or resolution"));
    };
    const auto   name  = params.getString("shape", "planter");
    const Shape* shape = nullptr;
    for (const auto& candidate : shapes)
        if (name == candidate.name) shape = &candidate;
    if (!shape) return invalid();
    const float missing = std::numeric_limits<float>::quiet_NaN();
    auto        read    = [&](const char* key, float fallback) {
        return params.getFloat(key, params.has(key) ? missing : fallback);
    };
    const float requestedHeight = read("vesselHeight", 0.f);
    const float height          = requestedHeight == 0.f ? shape->height : requestedHeight;
    const float thickness       = read("wallThickness", 0.045f);
    const float bottom          = read("bottomThickness", 0.05f);
    const int   samples         = params.getInt("curveSegments", params.has("curveSegments") ? -1 : 48);
    const int   radial          = params.getInt("radialSegments", params.has("radialSegments") ? -1 : 96);
    auto        material        = params.getString("material", "auto");
    if (material == "auto") material = name == "planter" ? "terracotta" : "porcelain";
    if (material != "terracotta" && material != "glazed_ceramic" && material != "porcelain" && material != "celadon" &&
        material != "stoneware")
        return invalid();
    if (!std::isfinite(height) || !std::isfinite(thickness) || !std::isfinite(bottom) || height <= 0.f ||
        height > 1000.f || thickness < 0.0001f || bottom < 0.0001f || bottom >= height || samples < 4 ||
        samples > 256 || radial < 3 || radial > 512)
        return invalid();
    std::array<float, 4> r{};
    const char*          keys[] = {"radius0", "radius1", "radius2", "radius3"};
    for (size_t i = 0; i < r.size(); ++i) {
        r[i] = read(keys[i], 0.f);
        if (r[i] == 0.f) r[i] = shape->radii[i];
        if (!std::isfinite(r[i]) || r[i] <= thickness + 0.0001f || r[i] > 1000.f) return invalid();
    }
    auto radius = [&](float t) {
        const float s = 1.f - t;
        return s * s * s * r[0] + 3.f * s * s * t * r[1] + 3.f * s * t * t * r[2] + t * t * t * r[3];
    };
    std::vector<VesselSectionPoint> section{{0.f, 0.f}};
    for (int i = 0; i <= samples; ++i) {
        const float t = float(i) / samples;
        section.push_back({radius(t), height * t});
    }
    // A semicircular lip joins the outer and inner surfaces without a zero-width top.
    for (int i = 1; i <= 8; ++i) {
        const float angle = 3.141592653589793f * float(i) / 8.f;
        section.push_back({r[3] - thickness * 0.5f + thickness * 0.5f * std::cos(angle),
                           height + thickness * 0.5f * std::sin(angle)});
    }
    for (int i = samples - 1; i >= 0; --i) {
        const float t = float(i) / samples;
        section.push_back({radius(t) - thickness, bottom + (height - bottom) * t});
    }
    section.push_back({0.f, bottom});
    auto result = revolveVesselSection(section, radial);
    if (!result.ok()) return result;
    result.value().setMeta("shape", name);
    result.value().setMeta("materialRecipe", "pbr." + material);
    return result;
}
}  // namespace
void registerVesselRecipes(MeshRecipeRegistry& registry) {
    RecipeDescriptor schema{"mesh.vessel", "Revolved pottery / vessels", "Pottery", {}};
    schema.params.push_back(ParamDescriptor::choice("shape", "Shape", "planter",
                                                    {"planter", "jar", "bowl", "plate", "saucer", "cylinder"}));
    schema.params.push_back(ParamDescriptor::choice(
        "material", "Material", "auto", {"auto", "terracotta", "glazed_ceramic", "porcelain", "celadon", "stoneware"}));
    schema.params.push_back(ParamDescriptor::integer("curveSegments", "Curve samples", 48, 4, 256));
    schema.params.push_back(ParamDescriptor::integer("radialSegments", "Radial segments", 96, 3, 512));
    schema.params.push_back(
        ParamDescriptor::floating("wallThickness", "Radial wall thickness", 0.045f, 0.0001f, 100.f, 0.001f));
    schema.params.push_back(
        ParamDescriptor::floating("bottomThickness", "Bottom thickness", 0.05f, 0.0001f, 100.f, 0.001f));
    schema.params.push_back(ParamDescriptor::floating("vesselHeight", "Height (0 = preset)", 0.f, 0.f, 1000.f, 0.01f));
    for (int i = 0; i < 4; ++i)
        schema.params.push_back(ParamDescriptor::floating("radius" + std::to_string(i),
                                                          "Bezier radius " + std::to_string(i) + " (0 = preset)", 0.f,
                                                          0.f, 1000.f, 0.01f));
    registry.registerRecipe(std::move(schema), [](const Params& params, MeshBuild& output, std::string& error) {
        auto result = build(params);
        if (!result.ok()) {
            error = "VesselParametersInvalid: invalid or intersecting vessel section";
            return false;
        }
        output = std::move(result.value());
        return true;
    });
}
}  // namespace eve::procgen
