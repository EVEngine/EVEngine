#include "physics/softbody/editing/SoftBody3DSchema.h"

#include "physics/softbody/SoftBody3DDefinition.h"

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

editing::PropertySchema softBody3DDefinitionSchema() {
    const physics::SoftBody3DDefinition defaults;
    editing::PropertySchema             schema;
    schema.typeId  = std::string(physics::SoftBody3DDefinition::SchemaId);
    schema.version = physics::SoftBody3DDefinition::SchemaVersion;
    numeric(schema, "cols", "Soft body columns", "Particle count along the local X axis", "Lattice",
            editing::PropertyType::Int, std::int64_t(defaults.cols), 2, 1000000, 1);
    numeric(schema, "rows", "Soft body rows", "Particle count along the local Y axis", "Lattice",
            editing::PropertyType::Int, std::int64_t(defaults.rows), 2, 1000000, 1);
    numeric(schema, "layers", "Soft body layers", "Particle count along the local Z axis", "Lattice",
            editing::PropertyType::Int, std::int64_t(defaults.layers), 2, 1000000, 1);
    numeric(schema, "spacing", "Particle spacing", "Rest distance between adjacent particles", "Lattice",
            editing::PropertyType::Float, defaults.spacing, 0.0001, 100000, 0.01, "m");
    numeric(schema, "originX", "Origin X", "Minimum rest-space X coordinate", "Transform", editing::PropertyType::Float,
            defaults.originX, -100000, 100000, 0.01, "m");
    numeric(schema, "originY", "Origin Y", "Minimum rest-space Y coordinate", "Transform", editing::PropertyType::Float,
            defaults.originY, -100000, 100000, 0.01, "m");
    numeric(schema, "originZ", "Origin Z", "Minimum rest-space Z coordinate", "Transform", editing::PropertyType::Float,
            defaults.originZ, -100000, 100000, 0.01, "m");
    numeric(schema, "gravityX", "Gravity X", "Acceleration along world X", "Forces", editing::PropertyType::Float,
            defaults.gravityX, -1000, 1000, 0.1, "m/s2");
    numeric(schema, "gravityY", "Gravity Y", "Acceleration along world Y", "Forces", editing::PropertyType::Float,
            defaults.gravityY, -1000, 1000, 0.1, "m/s2");
    numeric(schema, "gravityZ", "Gravity Z", "Acceleration along world Z", "Forces", editing::PropertyType::Float,
            defaults.gravityZ, -1000, 1000, 0.1, "m/s2");
    numeric(schema, "deformationResistance", "Deformation resistance", "Elastic shape-matching strength", "Solver",
            editing::PropertyType::Float, defaults.deformationResistance, 0, 1, 0.01);
    numeric(schema, "iterations", "Solver iterations", "Constraint iterations per substep", "Solver",
            editing::PropertyType::Int, std::int64_t(defaults.iterations), 1, 32, 1);
    numeric(schema, "damping", "Damping", "Velocity damping per integration step", "Solver",
            editing::PropertyType::Float, defaults.damping, 0, 1, 0.01);
    numeric(schema, "particleRadius", "Particle radius", "Collision radius of each particle", "Particles",
            editing::PropertyType::Float, defaults.particleRadius, 0.0001, 100000, 0.01, "m");
    numeric(schema, "particleMass", "Particle mass", "Mass used for force and rigid-body impulse exchange", "Particles",
            editing::PropertyType::Float, defaults.particleMass, 0.0001, 100000, 0.01, "kg");
    numeric(schema, "plasticYield", "Plastic yield", "Strain threshold before permanent deformation", "Plasticity",
            editing::PropertyType::Float, defaults.plasticYield, 0, 1000, 0.01);
    numeric(schema, "plasticCreep", "Plastic creep", "Rate at which yielded strain becomes permanent", "Plasticity",
            editing::PropertyType::Float, defaults.plasticCreep, 0, 1, 0.01);
    numeric(schema, "plasticRecovery", "Plastic recovery", "Rate at which plastic offsets return to rest", "Plasticity",
            editing::PropertyType::Float, defaults.plasticRecovery, 0, 1, 0.01);
    numeric(schema, "maxDeformation", "Maximum deformation",
            "Maximum stored plastic displacement; zero disables plasticity", "Plasticity", editing::PropertyType::Float,
            defaults.maxDeformation, 0, 100000, 0.01, "m");
    schema.properties.push_back(property("selfCollision", "Self collision", "Resolve non-neighbor particle overlap",
                                         "Collision", editing::PropertyType::Bool, defaults.selfCollision));
    return schema;
}

}  // namespace eve::physics_editing
