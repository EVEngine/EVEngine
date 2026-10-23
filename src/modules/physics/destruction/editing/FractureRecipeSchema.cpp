#include "physics/destruction/editing/FractureRecipeSchema.h"

#include "physics/destruction/FractureRecipe.h"

#include <utility>

namespace eve::physics_editing {
namespace {

editing::PropertyDescriptor property(const char* path, const char* label, const char* description, const char* category,
                                     editing::PropertyType type, editing::Value defaultValue) {
    editing::PropertyDescriptor result;
    result.path           = editing::PropertyPath(path);
    result.displayNameKey = label;
    result.descriptionKey = description;
    result.category       = category;
    result.type           = type;
    result.defaultValue   = std::move(defaultValue);
    return result;
}

void numeric(editing::PropertySchema& schema, const char* path, const char* label, const char* description,
             const char* category, editing::PropertyType type, editing::Value value, double minimum, double maximum,
             double step, const char* units = "") {
    auto descriptor            = property(path, label, description, category, type, std::move(value));
    descriptor.numeric.minimum = minimum;
    descriptor.numeric.maximum = maximum;
    descriptor.numeric.step    = step;
    descriptor.numeric.units   = units;
    schema.properties.push_back(std::move(descriptor));
}

}  // namespace

editing::PropertySchema fractureRecipeSchema() {
    const physics::FractureRecipe defaults;
    editing::PropertySchema       schema;
    schema.typeId  = std::string(physics::FractureRecipe::SchemaId);
    schema.version = physics::FractureRecipe::SchemaVersion;

    auto mode = property("mode", "Fracture mode", "UniformVoronoi, ClusteredVoronoi, Radial, or Planar", "Fracture",
                         editing::PropertyType::String, std::string("UniformVoronoi"));
    mode.enumItems = {"UniformVoronoi", "ClusteredVoronoi", "Radial", "Planar"};
    schema.properties.push_back(std::move(mode));

    numeric(schema, "siteCountMin", "Site count min", "Minimum Voronoi site count", "Voronoi",
            editing::PropertyType::Int, std::int64_t(defaults.siteCountMin), 1, 4096, 1);
    numeric(schema, "siteCountMax", "Site count max", "Maximum Voronoi site count", "Voronoi",
            editing::PropertyType::Int, std::int64_t(defaults.siteCountMax), 1, 4096, 1);
    numeric(schema, "clusterCount", "Cluster count", "Island count for ClusteredVoronoi", "Voronoi",
            editing::PropertyType::Int, std::int64_t(defaults.clusterCount), 1, 256, 1);
    numeric(schema, "clusterRadius", "Cluster radius", "Site scatter radius around each island centre", "Voronoi",
            editing::PropertyType::Float, defaults.clusterRadius, 0.0001, 1000, 0.01, "m");
    numeric(schema, "radialSpokes", "Radial spokes", "Number of vertical cutting planes for Radial mode", "Radial",
            editing::PropertyType::Int, std::int64_t(defaults.radialSpokes), 1, 64, 1);
    numeric(schema, "radialPlanes", "Radial shells", "Number of horizontal shell cuts for Radial mode", "Radial",
            editing::PropertyType::Int, std::int64_t(defaults.radialPlanes), 0, 32, 1);
    numeric(schema, "seed", "Seed", "Deterministic cook seed mixed with the named RNG stream", "Random",
            editing::PropertyType::Int, std::int64_t(defaults.seed), 0, 9223372036854775807.0, 1);
    schema.properties.push_back(property("randomStreamName", "RNG stream", "Named deterministic stream (no wall clock)",
                                         "Random", editing::PropertyType::String, defaults.randomStreamName));
    schema.properties.push_back(property("interiorMaterialLogicalId", "Interior material",
                                         "Logical id for broken-face interior material", "Material",
                                         editing::PropertyType::String, defaults.interiorMaterialLogicalId));
    numeric(schema, "defaultStrainThreshold", "Default strain threshold", "Break threshold for generated edges",
            "Structure", editing::PropertyType::Float, defaults.defaultStrainThreshold, 0.0001, 100000, 0.1);
    numeric(schema, "defaultDensity", "Default density", "Proxy bone density", "Material",
            editing::PropertyType::Float, defaults.defaultDensity, 0.0001, 100000, 0.1, "kg/m3");
    numeric(schema, "defaultFriction", "Default friction", "Proxy bone friction", "Material",
            editing::PropertyType::Float, defaults.defaultFriction, 0, 1, 0.01);
    numeric(schema, "defaultRestitution", "Default restitution", "Proxy bone restitution", "Material",
            editing::PropertyType::Float, defaults.defaultRestitution, 0, 1, 0.01);
    numeric(schema, "minimumThickness", "Minimum thickness", "Reject meshes thinner than this on any AABB axis",
            "Validation", editing::PropertyType::Float, defaults.minimumThickness, 0.0001, 1000, 0.01, "m");
    numeric(schema, "maximumBones", "Maximum bones", "Soft cap on cooked leaf bones", "Validation",
            editing::PropertyType::Int, std::int64_t(defaults.maximumBones), 1, 4096, 1);
    return schema;
}

}  // namespace eve::physics_editing
