#pragma once
#include "fluids/VolumeFluidCodec.h"

#include <cmath>
#include <glm/gtc/quaternion.hpp>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace eve::fluids {
namespace {
template <class T, class F>
void fields(T& v, F&& f) {
    using S = std::remove_cv_t<T>;
    if constexpr (std::is_same_v<S, MeshSdf>) {
        f("origin", v.origin);
        f("cellSize", v.cellSize);
        f("dims", v.dims);
        f("distances", v.distances);
    } else if constexpr (std::is_same_v<S, VolumeFluidSdfCollider>) {
        f("label", v.label);
        f("sdf", v.sdf);
        f("position", v.position);
        f("rotation", v.rotation);
        f("scale", v.scale);
        f("inverted", v.inverted);
        f("velocity", v.velocity);
        f("angularVelocity", v.angularVelocity);
        f("friction", v.friction);
        f("staticFriction", v.staticFriction);
        f("rollingFriction", v.rollingFriction);
        f("stickiness", v.stickiness);
        f("stickDistance", v.stickDistance);
        f("frictionCombine", v.frictionCombine);
        f("stickinessCombine", v.stickinessCombine);
        f("rollingContacts", v.rollingContacts);
        f("collisionFilter", v.collisionFilter);
        f("isTrigger", v.isTrigger);
    } else if constexpr (std::is_same_v<S, FluidRendererSettings>) {
        f("blendSource", v.blendSource);
        f("blendDestination", v.blendDestination);
        f("particleBlendSource", v.particleBlendSource);
        f("particleBlendDestination", v.particleBlendDestination);
        f("particleZWrite", v.particleZWrite);
        f("thicknessCutoff", v.thicknessCutoff);
        f("thicknessDownsample", v.thicknessDownsample);
        f("generateSurface", v.generateSurface);
        f("blurRadius", v.blurRadius);
        f("surfaceDownsample", v.surfaceDownsample);
        f("lighting", v.lighting);
        f("smoothness", v.smoothness);
        f("metalness", v.metalness);
        f("ambientMultiplier", v.ambientMultiplier);
        f("generateReflection", v.generateReflection);
        f("reflection", v.reflection);
        f("generateRefraction", v.generateRefraction);
        f("transparency", v.transparency);
        f("absorption", v.absorption);
        f("refraction", v.refraction);
        f("refractionDownsample", v.refractionDownsample);
        f("generateFoam", v.generateFoam);
        f("foamDownsample", v.foamDownsample);
    } else if constexpr (std::is_same_v<S, VolumeFluidHeightFieldCollider>) {
        f("label", v.label);
        f("heights", v.heights);
        f("resolution", v.resolution);
        f("size", v.size);
        f("position", v.position);
        f("rotation", v.rotation);
        f("velocity", v.velocity);
        f("angularVelocity", v.angularVelocity);
        f("friction", v.friction);
        f("staticFriction", v.staticFriction);
        f("rollingFriction", v.rollingFriction);
        f("stickiness", v.stickiness);
        f("stickDistance", v.stickDistance);
        f("frictionCombine", v.frictionCombine);
        f("stickinessCombine", v.stickinessCombine);
        f("rollingContacts", v.rollingContacts);
        f("collisionFilter", v.collisionFilter);
        f("isTrigger", v.isTrigger);
    } else if constexpr (std::is_same_v<S, VolumeFluidSdfPose>) {
        f("label", v.label);
        f("position", v.position);
        f("rotation", v.rotation);
        f("scale", v.scale);
        f("velocity", v.velocity);
        f("angularVelocity", v.angularVelocity);
    } else if constexpr (std::is_same_v<S, VolumeFluidFoamSettings>) {
        f("rate", v.rate);
        f("randomness", v.randomness);
        f("vorticityThreshold", v.vorticityThreshold);
        f("densityThreshold", v.densityThreshold);
        f("lifetime", v.lifetime);
        f("maxPerStep", v.maxPerStep);
    } else if constexpr (std::is_same_v<S, VolumeFluidFoamSnapshot>) {
        f("schema", v.schema);
        f("version", v.version);
        f("settings", v.settings);
        f("credit", v.credit);
        f("randomState", v.randomState);
    } else if constexpr (std::is_same_v<S, VolumeFluidDiffuseParticle>) {
        f("position", v.position);
        f("velocity", v.velocity);
        f("life", v.life);
    } else if constexpr (std::is_same_v<S, VolumeFluidDiffuseSnapshot>) {
        f("schema", v.schema);
        f("version", v.version);
        f("capacity", v.capacity);
        f("particles", v.particles);
    } else if constexpr (std::is_same_v<S, VolumeFluidMaterial>) {
        f("density", v.density);
        f("smoothing", v.smoothing);
        f("viscosity", v.viscosity);
        f("cohesion", v.cohesion);
        f("buoyancy", v.buoyancy);
        f("drag", v.drag);
        f("atmosphericPressure", v.atmosphericPressure);
        f("diffusion", v.diffusion);
        f("yieldStress", v.yield);
        f("vorticity", v.vorticity);
        f("dynamicFriction", v.dynamicFriction);
        f("staticFriction", v.staticFriction);
        f("rollingContacts", v.rollingContacts);
        f("rollingFriction", v.rollingFriction);
        f("stickiness", v.stickiness);
        f("stickDistance", v.stickDistance);
        f("frictionCombine", v.frictionCombine);
        f("stickinessCombine", v.stickinessCombine);
        f("phase", v.phase);
    } else if constexpr (std::is_same_v<S, VolumeFluidEmitterBlueprint3D>) {
        f("capacity", v.capacity);
        f("resolution", v.resolution);
        f("restDensity", v.restDensity);
        f("smoothing", v.smoothing);
        f("viscosity", v.viscosity);
        f("surfaceTension", v.surfaceTension);
        f("buoyancy", v.buoyancy);
        f("atmosphericDrag", v.atmosphericDrag);
        f("atmosphericPressure", v.atmosphericPressure);
        f("vorticity", v.vorticity);
        f("diffusion", v.diffusion);
        f("diffusionData", v.diffusionData);
    } else if constexpr (std::is_same_v<S, VolumeGranularEmitterBlueprint3D>) {
        f("capacity", v.capacity);
        f("resolution", v.resolution);
        f("restDensity", v.restDensity);
        f("randomness", v.randomness);
    } else if constexpr (std::is_same_v<S, VolumeFluidViscosityColorKey>) {
        f("viscosity", v.viscosity);
        f("color", v.color);
    } else if constexpr (std::is_same_v<S, VolumeFluidColorKey>) {
        f("coordinate", v.coordinate);
        f("color", v.color);
    } else if constexpr (std::is_same_v<S, VolumeFluidSettings>) {
        f("capacity", v.capacity);
        f("spacing", v.spacing);
        f("gravity", v.gravity);
        f("minimum", v.minimum);
        f("maximum", v.maximum);
        f("iterations", v.iterations);
    } else if constexpr (std::is_same_v<S, VolumeFluidParticle>) {
        f("position", v.position);
        f("velocity", v.velocity);
        f("angularVelocity", v.angularVelocity);
        f("radii", v.radii);
        f("orientation", v.orientation);
        f("material", v.material);
        f("color", v.color);
        f("data", v.data);
        f("life", v.life);
        f("collisionFilter", v.collisionFilter);
        f("actorGroup", v.actorGroup);
        f("selfCollide", v.selfCollide);
    } else if constexpr (std::is_same_v<S, VolumeFluidParticleEvent>) {
        f("type", v.type);
        f("particleIndex", v.particleIndex);
        f("actorGroup", v.actorGroup);
        f("particle", v.particle);
    } else if constexpr (std::is_same_v<S, VolumeFluidParticleEventBatch>) {
        f("events", v.events);
        f("dropped", v.dropped);
    } else if constexpr (std::is_same_v<S, VolumeFluidWindZone>) {
        f("type", v.type);
        f("center", v.center);
        f("direction", v.direction);
        f("intensity", v.intensity);
        f("turbulence", v.turbulence);
        f("turbulenceFrequency", v.turbulenceFrequency);
        f("turbulenceSeed", v.turbulenceSeed);
        f("radius", v.radius);
        f("radial", v.radial);
    } else if constexpr (std::is_same_v<S, VolumeFluidStitch>) {
        f("particleIndex1", v.particleIndex1);
        f("particleIndex2", v.particleIndex2);
        f("compliance", v.compliance);
    } else if constexpr (std::is_same_v<S, VolumeFluidCollider>) {
        f("label", v.label);
        f("shape", v.shape);
        f("center", v.center);
        f("halfExtent", v.halfExtent);
        f("radius", v.radius);
        f("velocity", v.velocity);
        f("friction", v.friction);
        f("staticFriction", v.staticFriction);
        f("rollingFriction", v.rollingFriction);
        f("stickiness", v.stickiness);
        f("stickDistance", v.stickDistance);
        f("frictionCombine", v.frictionCombine);
        f("stickinessCombine", v.stickinessCombine);
        f("rollingContacts", v.rollingContacts);
        f("solidify", v.solidify);
        f("rotation", v.rotation);
        f("angularVelocity", v.angularVelocity);
        f("collisionFilter", v.collisionFilter);
        f("isTrigger", v.isTrigger);
    } else if constexpr (std::is_same_v<S, VolumeFluidDistanceHit>) {
        f("particleIndex", v.particleIndex);
        f("distance", v.distance);
        f("queryPoint", v.queryPoint);
        f("normal", v.normal);
        f("particle", v.particle);
    } else if constexpr (std::is_same_v<S, VolumeFluidRayHit>) {
        f("particleIndex", v.particleIndex);
        f("distance", v.distance);
        f("point", v.point);
        f("normal", v.normal);
        f("particle", v.particle);
    } else if constexpr (std::is_same_v<S, VolumeFluidQueryShape>) {
        f("type", v.type);
        f("center", v.center);
        f("size", v.size);
        f("rotation", v.rotation);
        f("contactOffset", v.contactOffset);
        f("maxDistance", v.maxDistance);
        f("phaseMask", v.phaseMask);
        f("collisionFilter", v.collisionFilter);
    } else if constexpr (std::is_same_v<S, VolumeFluidQueryHit>) {
        f("queryIndex", v.queryIndex);
        f("particleIndex", v.particleIndex);
        f("distance", v.distance);
        f("queryPoint", v.queryPoint);
        f("normal", v.normal);
        f("particle", v.particle);
    } else if constexpr (std::is_same_v<S, VolumeFluidSimplex>) {
        f("particleIndices", v.particleIndices);
        f("size", v.size);
    } else if constexpr (std::is_same_v<S, VolumeFluidSimplexHit>) {
        f("simplexBary", v.simplexBary);
        f("queryPoint", v.queryPoint);
        f("normal", v.normal);
        f("distance", v.distance);
        f("simplexIndex", v.simplexIndex);
        f("queryIndex", v.queryIndex);
    } else if constexpr (std::is_same_v<S, VolumeFluidFieldSample>) {
        f("velocity", v.velocity);
        f("vorticity", v.vorticity);
        f("density", v.density);
        f("neighborCount", v.neighborCount);
    } else if constexpr (std::is_same_v<S, VolumeFluidAttachment>) {
        f("particleIndex", v.particleIndex);
        f("colliderLabel", v.colliderLabel);
        f("localPosition", v.localPosition);
        f("localOrientation", v.localOrientation);
        f("constrainOrientation", v.constrainOrientation);
        f("dynamic", v.dynamic);
        f("compliance", v.compliance);
        f("breakThreshold", v.breakThreshold);
    } else if constexpr (std::is_same_v<S, VolumeFluidThermalRule>) {
        f("colliderLabel", v.colliderLabel);
        f("rate", v.rate);
        f("minimumViscosity", v.minimumViscosity);
        f("maximumViscosity", v.maximumViscosity);
        f("minimumCohesion", v.minimumCohesion);
        f("maximumCohesion", v.maximumCohesion);
    } else if constexpr (std::is_same_v<S, VolumeFluidContact>) {
        f("colliderLabel", v.colliderLabel);
        f("point", v.point);
        f("normal", v.normal);
        f("impulse", v.impulse);
        f("particleIndex", v.particleIndex);
        f("actorGroup", v.actorGroup);
        f("distance", v.distance);
    } else if constexpr (std::is_same_v<S, VolumeFluidContactEvent>) {
        f("type", v.type);
        f("contact", v.contact);
    } else if constexpr (std::is_same_v<S, VolumeFluidDistributionPoint>) {
        f("position", v.position);
        f("color", v.color);
        f("direction", v.direction);
    } else if constexpr (std::is_same_v<S, VolumeFluidEmission>) {
        f("shape", v.shape);
        f("origin", v.origin);
        f("direction", v.direction);
        f("extent", v.extent);
        f("speed", v.speed);
        f("jitter", v.jitter);
        f("randomVelocity", v.randomVelocity);
        f("useShapeColor", v.useShapeColor);
        f("actorCapacity", v.actorCapacity);
        f("granularRadiusRandomness", v.granularRadiusRandomness);
        f("prototype", v.prototype);
        f("seed", v.seed);
        f("distribution", v.distribution);
    } else if constexpr (std::is_same_v<S, VolumeFluidNozzlePose>) {
        f("position", v.position);
        f("rotation", v.rotation);
    } else if constexpr (std::is_same_v<S, VolumeFluidEmitterSnapshot>) {
        f("schema", v.schema);
        f("version", v.version);
        f("kind", v.kind);
        f("phase", v.phase);
        f("sequence", v.sequence);
        f("emitting", v.emitting);
    } else if constexpr (std::is_same_v<S, VolumeFluidSnapshot>) {
        f("schema", v.schema);
        f("version", v.version);
        f("settings", v.settings);
        f("particles", v.particles);
        f("colliders", v.colliders);
        f("sdfColliders", v.sdfColliders);
        f("heightFieldColliders", v.heightFieldColliders);
        f("attachments", v.attachments);
        f("stitches", v.stitches);
        f("simplexes", v.simplexes);
    }
}
template <class T>
Value pack(const T& v) {
    if constexpr (std::is_same_v<T, bool> || std::is_same_v<T, std::string> || std::is_same_v<T, float>)
        return Value(v);
    else if constexpr (std::is_integral_v<T> || std::is_enum_v<T>)
        return Value(int64_t(v));
    else if constexpr (std::is_same_v<T, glm::vec3> || std::is_same_v<T, glm::vec4> || std::is_same_v<T, glm::ivec2> ||
                       std::is_same_v<T, glm::ivec3>) {
        Value::Array a;
        for (int i = 0; i < v.length(); ++i) a.emplace_back(v[i]);
        return Value(std::move(a));
    } else if constexpr (requires {
                             v.begin();
                             v.end();
                         }) {
        Value::Array a;
        for (const auto& item : v) a.push_back(pack(item));
        return Value(std::move(a));
    } else {
        Value out(Value::Object{});
        fields(v, [&](const char* key, const auto& item) { out.set(key, pack(item)); });
        return out;
    }
}
template <class T>
void unpack(const Value& value, T& out, const std::string& path) {
    const auto fail = [&] { throw std::runtime_error(path); };
    if constexpr (std::is_same_v<T, bool>) {
        if (!value.isBool()) fail();
        out = value.asBool();
    } else if constexpr (std::is_same_v<T, std::string>) {
        if (!value.isString()) fail();
        out = value.asString();
    } else if constexpr (std::is_same_v<T, float>) {
        if (!value.isNumeric()) fail();
        const double v = value.isInt64() ? double(value.asInt()) : value.asDouble();
        if (!std::isfinite(v) || std::abs(v) > std::numeric_limits<float>::max()) fail();
        out = float(v);
    } else if constexpr (std::is_integral_v<T> || std::is_enum_v<T>) {
        if (!value.isInt64() || value.asInt() < 0 || value.asInt() > std::numeric_limits<unsigned>::max()) fail();
        out = static_cast<T>(value.asInt());
    } else if constexpr (std::is_same_v<T, glm::vec3> || std::is_same_v<T, glm::vec4>) {
        if (!value.isArray() || value.arraySize() != size_t(out.length())) fail();
        for (int i = 0; i < out.length(); ++i) unpack(value.at(size_t(i)), out[i], path + "/" + std::to_string(i));
    } else if constexpr (std::is_same_v<T, glm::ivec2> || std::is_same_v<T, glm::ivec3>) {
        if (!value.isArray() || value.arraySize() != size_t(out.length())) fail();
        for (int i = 0; i < out.length(); ++i) {
            const auto& item = value.at(size_t(i));
            if (!item.isInt64() || item.asInt() < 0 || item.asInt() > 2048) fail();
            out[i] = int(item.asInt());
        }
    } else if constexpr (std::is_same_v<T, std::array<unsigned, 3>>) {
        if (!value.isArray() || value.arraySize() != out.size()) fail();
        for (size_t i = 0; i < out.size(); ++i) unpack(value.at(i), out[i], path + "/" + std::to_string(i));
    } else if constexpr (requires { out.resize(size_t{}); }) {
        if (!value.isArray() || value.arraySize() > 1000000) fail();
        out.resize(value.arraySize());
        for (size_t i = 0; i < out.size(); ++i) unpack(value.at(i), out[i], path + "/" + std::to_string(i));
    } else {
        if (!value.isObject()) fail();
        size_t count = 0;
        fields(out, [&](const char* key, auto& item) {
            ++count;
            const auto* child = value.find(key);
            if (!child) throw std::runtime_error(path + "/" + key);
            unpack(*child, item, path + "/" + key);
        });
        if (value.keys().size() != count) fail();
    }
}
}  // namespace


}  // namespace eve::fluids
