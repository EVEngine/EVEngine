#include "fluids/VolumeFluidCodecInternal.inc"

namespace eve::fluids {
Value encodeFluidRendererSettings(const FluidRendererSettings& settings) { return pack(settings); }
Result<FluidRendererSettings> decodeFluidRendererSettings(const Value& value) {
    FluidRendererSettings decoded;
    try {
        unpack(value, decoded, "fluids.surface.rendererSettings");
    } catch (const std::runtime_error& error) {
        return Result<FluidRendererSettings>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Invalid, missing or unknown fluid renderer setting", error.what()));
    }
    return Result<FluidRendererSettings>::success(std::move(decoded));
}

Value encodeVolumeFluid(const VolumeFluidSnapshot& snapshot) { return pack(snapshot); }
Value encodeVolumeFluidFoam(const VolumeFluidFoamSnapshot& snapshot) { return pack(snapshot); }
Result<VolumeFluidFoamSnapshot> decodeVolumeFluidFoam(const Value& value) {
    VolumeFluidFoamSnapshot decoded;
    try {
        unpack(value, decoded, "fluids.volume.foam");
    } catch (const std::runtime_error& error) {
        return Result<VolumeFluidFoamSnapshot>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid foam state fields", error.what()));
    }
    VolumeFluidFoam controller;
    auto            validated = controller.restore(decoded);
    if (!validated) return Result<VolumeFluidFoamSnapshot>::failure(validated.status());
    return Result<VolumeFluidFoamSnapshot>::success(std::move(decoded));
}
Value encodeVolumeFluidDiffuse(const VolumeFluidDiffuseSnapshot& snapshot) { return pack(snapshot); }
Result<VolumeFluidDiffuseSnapshot> decodeVolumeFluidDiffuse(const Value& value) {
    VolumeFluidDiffuseSnapshot decoded;
    try {
        const auto* particles = value.find("particles");
        if (!particles || !particles->isArray() || particles->arraySize() > 65536)
            throw std::runtime_error("fluids.volume.diffuse/particles");
        unpack(value, decoded, "fluids.volume.diffuse");
    } catch (const std::runtime_error& error) {
        return Result<VolumeFluidDiffuseSnapshot>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid diffuse snapshot fields", error.what()));
    }
    VolumeFluidDiffuse pool;
    auto               validated = pool.restore(decoded);
    if (!validated) return Result<VolumeFluidDiffuseSnapshot>::failure(validated.status());
    return Result<VolumeFluidDiffuseSnapshot>::success(std::move(decoded));
}
Result<std::vector<VolumeFluidDiffuseParticle>> decodeVolumeFluidDiffuseParticles(const Value& value) {
    std::vector<VolumeFluidDiffuseParticle> decoded;
    try {
        if (!value.isArray() || value.arraySize() > 65536) throw std::runtime_error("fluids.volume.diffuse/particles");
        unpack(value, decoded, "fluids.volume.diffuse/particles");
    } catch (const std::runtime_error& error) {
        return Result<std::vector<VolumeFluidDiffuseParticle>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid diffuse particle fields", error.what()));
    }
    return Result<std::vector<VolumeFluidDiffuseParticle>>::success(std::move(decoded));
}
Value encodeVolumeFluidFieldSamples(const std::vector<VolumeFluidFieldSample>& samples) { return pack(samples); }
Value encodeVolumeFluidRayHits(const std::vector<VolumeFluidRayHit>& hits) { return pack(hits); }
Value encodeVolumeFluidDistanceHits(const std::vector<VolumeFluidDistanceHit>& hits) { return pack(hits); }
Value encodeVolumeFluidQueryHits(const std::vector<VolumeFluidQueryHit>& hits) { return pack(hits); }
Value encodeVolumeFluidSimplexHits(const std::vector<VolumeFluidSimplexHit>& hits) { return pack(hits); }
Result<std::vector<VolumeFluidSimplex>> decodeVolumeFluidSimplexes(const Value& value) {
    std::vector<VolumeFluidSimplex> decoded;
    try {
        if (!value.isArray() || value.arraySize() > 65536) throw std::runtime_error("fluids.volume/simplexes");
        unpack(value, decoded, "fluids.volume/simplexes");
    } catch (const std::runtime_error& error) {
        return Result<std::vector<VolumeFluidSimplex>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid simplex fields or count", error.what()));
    }
    return Result<std::vector<VolumeFluidSimplex>>::success(std::move(decoded));
}
Result<std::vector<VolumeFluidStitch>> decodeVolumeFluidStitches(const Value& value) {
    std::vector<VolumeFluidStitch> decoded;
    try {
        if (!value.isArray() || value.arraySize() > 65536) throw std::runtime_error("fluids.volume/stitches");
        unpack(value, decoded, "fluids.volume/stitches");
    } catch (const std::runtime_error& error) {
        return Result<std::vector<VolumeFluidStitch>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid stitch fields or count", error.what()));
    }
    return Result<std::vector<VolumeFluidStitch>>::success(std::move(decoded));
}
Result<std::vector<VolumeFluidQueryShape>> decodeVolumeFluidQueries(const Value& value) {
    std::vector<VolumeFluidQueryShape> decoded;
    try {
        if (!value.isArray() || value.arraySize() > 256) throw std::runtime_error("fluids.volume.queries");
        unpack(value, decoded, "fluids.volume.queries");
    } catch (const std::runtime_error& error) {
        return Result<std::vector<VolumeFluidQueryShape>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid query fields or count", error.what()));
    }
    return Result<std::vector<VolumeFluidQueryShape>>::success(std::move(decoded));
}
Result<std::vector<glm::vec4>> decodeVolumeFluidColors(const Value& value) {
    std::vector<glm::vec4> decoded;
    try {
        if (!value.isArray() || value.arraySize() > 256) throw std::runtime_error("fluids.volume.queryColors");
        unpack(value, decoded, "fluids.volume.queryColors");
    } catch (const std::runtime_error& error) {
        return Result<std::vector<glm::vec4>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid query color fields or count", error.what()));
    }
    return Result<std::vector<glm::vec4>>::success(std::move(decoded));
}
Result<std::vector<glm::vec3>> decodeVolumeFluidFieldPositions(const Value& value) {
    std::vector<glm::vec3> decoded;
    try {
        if (!value.isArray() || value.arraySize() > 65536) throw std::runtime_error("fluids.volume.field/positions");
        unpack(value, decoded, "fluids.volume.field/positions");
    } catch (const std::runtime_error& error) {
        return Result<std::vector<glm::vec3>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid field query positions", error.what()));
    }
    return Result<std::vector<glm::vec3>>::success(std::move(decoded));
}
Result<std::vector<VolumeFluidWindZone>> decodeVolumeFluidWindZones(const Value& value) {
    std::vector<VolumeFluidWindZone> decoded;
    try {
        if (!value.isArray() || value.arraySize() > 64) throw std::runtime_error("fluids.volume.wind/zones");
        unpack(value, decoded, "fluids.volume.wind/zones");
    } catch (const std::runtime_error& error) {
        return Result<std::vector<VolumeFluidWindZone>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid wind-zone fields or count", error.what()));
    }
    return Result<std::vector<VolumeFluidWindZone>>::success(std::move(decoded));
}
Value encodeVolumeFluidCollider(const VolumeFluidCollider& collider) { return pack(collider); }
Value encodeVolumeFluidSdfCollider(const VolumeFluidSdfCollider& collider) { return pack(collider); }
Value encodeVolumeFluidHeightFieldCollider(const VolumeFluidHeightFieldCollider& collider) { return pack(collider); }
Value encodeVolumeFluidSdfPose(const VolumeFluidSdfPose& pose) { return pack(pose); }
Result<std::vector<VolumeFluidSdfPose>> decodeVolumeFluidSdfPoses(const Value& value) {
    std::vector<VolumeFluidSdfPose> decoded;
    try {
        if (!value.isArray() || value.arraySize() > 16) throw std::runtime_error("fluids.volume.sdfPoses");
        unpack(value, decoded, "fluids.volume.sdfPoses");
    } catch (const std::runtime_error& error) {
        return Result<std::vector<VolumeFluidSdfPose>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid SDF pose fields", error.what()));
    }
    return Result<std::vector<VolumeFluidSdfPose>>::success(std::move(decoded));
}
Result<std::vector<VolumeFluidSdfCollider>> decodeVolumeFluidSdfColliders(const Value& value) {
    std::vector<VolumeFluidSdfCollider> decoded;
    try {
        if (!value.isArray() || value.arraySize() > 16) throw std::runtime_error("fluids.volume.sdfColliders");
        unpack(value, decoded, "fluids.volume.sdfColliders");
    } catch (const std::runtime_error& error) {
        return Result<std::vector<VolumeFluidSdfCollider>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid SDF collider fields or count", error.what()));
    }
    return Result<std::vector<VolumeFluidSdfCollider>>::success(std::move(decoded));
}
Result<std::vector<VolumeFluidHeightFieldCollider>> decodeVolumeFluidHeightFieldColliders(const Value& value) {
    std::vector<VolumeFluidHeightFieldCollider> decoded;
    try {
        if (!value.isArray() || value.arraySize() > 16) throw std::runtime_error("fluids.volume.heightFieldColliders");
        unpack(value, decoded, "fluids.volume.heightFieldColliders");
    } catch (const std::runtime_error& error) {
        return Result<std::vector<VolumeFluidHeightFieldCollider>>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Invalid height-field collider fields or count", error.what()));
    }
    return Result<std::vector<VolumeFluidHeightFieldCollider>>::success(std::move(decoded));
}
Value encodeVolumeFluidWindZone(const VolumeFluidWindZone& zone) { return pack(zone); }
Value encodeVolumeFluidContacts(const std::vector<VolumeFluidContact>& contacts) { return pack(contacts); }
Value encodeVolumeFluidContactEvents(const std::vector<VolumeFluidContactEvent>& events) { return pack(events); }
Value encodeVolumeFluidThermalRule(const VolumeFluidThermalRule& rule) { return pack(rule); }
Result<std::vector<VolumeFluidViscosityColorKey>> decodeVolumeFluidViscosityColors(const Value& value) {
    std::vector<VolumeFluidViscosityColorKey> decoded;
    try {
        if (!value.isArray() || value.arraySize() > 32) throw std::runtime_error("fluids.volume.viscosityColors");
        unpack(value, decoded, "fluids.volume.viscosityColors");
    } catch (const std::runtime_error& error) {
        return Result<std::vector<VolumeFluidViscosityColorKey>>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Invalid viscosity color fields or count", error.what()));
    }
    return Result<std::vector<VolumeFluidViscosityColorKey>>::success(std::move(decoded));
}
Result<std::vector<VolumeFluidColorKey>> decodeVolumeFluidColorGradient(const Value& value) {
    std::vector<VolumeFluidColorKey> decoded;
    try {
        if (!value.isArray() || value.arraySize() < 2 || value.arraySize() > 32)
            throw std::runtime_error("fluids.volume.colorGradient");
        unpack(value, decoded, "fluids.volume.colorGradient");
    } catch (const std::runtime_error& error) {
        return Result<std::vector<VolumeFluidColorKey>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid color gradient fields or count", error.what()));
    }
    return Result<std::vector<VolumeFluidColorKey>>::success(std::move(decoded));
}
Result<std::vector<VolumeFluidThermalRule>> decodeVolumeFluidThermalRules(const Value& value) {
    std::vector<VolumeFluidThermalRule> decoded;
    try {
        if (!value.isArray() || value.arraySize() > 1024) throw std::runtime_error("fluids.volume.thermalRules");
        unpack(value, decoded, "fluids.volume.thermalRules");
    } catch (const std::runtime_error& error) {
        return Result<std::vector<VolumeFluidThermalRule>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid thermal rule fields or count", error.what()));
    }
    return Result<std::vector<VolumeFluidThermalRule>>::success(std::move(decoded));
}
Value encodeVolumeFluidParticles(const std::vector<VolumeFluidParticle>& particles) { return pack(particles); }
Value encodeVolumeFluidParticleEvents(const VolumeFluidParticleEventBatch& batch) { return pack(batch); }
Result<VolumeFluidMaterial> decodeVolumeFluidMaterial(const Value& value) {
    VolumeFluidMaterial decoded;
    try {
        unpack(value, decoded, "fluids.volume.material");
    } catch (const std::runtime_error& error) {
        return Result<VolumeFluidMaterial>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Invalid, missing or unknown material field", error.what()));
    }
    return Result<VolumeFluidMaterial>::success(decoded);
}

Value encodeVolumeFluidEmitterBlueprint3D(const VolumeFluidEmitterBlueprint3D& blueprint) { return pack(blueprint); }
Result<VolumeFluidEmitterBlueprint3D> decodeVolumeFluidEmitterBlueprint3D(const Value& value) {
    VolumeFluidEmitterBlueprint3D decoded;
    try {
        unpack(value, decoded, "fluids.volume.emitterBlueprint");
    } catch (const std::runtime_error& error) {
        return Result<VolumeFluidEmitterBlueprint3D>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument,
                              "Invalid, missing or unknown fluid emitter blueprint field", error.what()));
    }
    return Result<VolumeFluidEmitterBlueprint3D>::success(std::move(decoded));
}
Value encodeVolumeGranularEmitterBlueprint3D(const VolumeGranularEmitterBlueprint3D& blueprint) {
    return pack(blueprint);
}
Result<VolumeGranularEmitterBlueprint3D> decodeVolumeGranularEmitterBlueprint3D(const Value& value) {
    VolumeGranularEmitterBlueprint3D decoded;
    try {
        unpack(value, decoded, "fluids.volume.granularEmitterBlueprint");
    } catch (const std::runtime_error& error) {
        return Result<VolumeGranularEmitterBlueprint3D>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument,
                              "Invalid, missing or unknown granular emitter blueprint field", error.what()));
    }
    return Result<VolumeGranularEmitterBlueprint3D>::success(std::move(decoded));
}
Value encodeVolumeFluidEmitterBlueprintApplication3D(const VolumeFluidEmitterBlueprintApplication3D& application) {
    Value               result(Value::Object{});
    VolumeFluidSnapshot solver;
    solver.settings = application.settings;
    result.set("solver", encodeVolumeFluid(solver));
    result.set("emission", encodeVolumeFluidEmission(application.emission));
    Value metrics(Value::Object{});
    metrics.set("particleSize", double(application.metrics.particleSize));
    metrics.set("particleMass", double(application.metrics.particleMass));
    metrics.set("smoothingRadius", double(application.metrics.smoothingRadius));
    result.set("metrics", std::move(metrics));
    return result;
}
Result<std::vector<VolumeFluidCollider>> decodeVolumeFluidColliders(const Value& value) {
    std::vector<VolumeFluidCollider> decoded;
    try {
        if (!value.isArray() || value.arraySize() > 1024) throw std::runtime_error("fluids.volume.colliders");
        unpack(value, decoded, "fluids.volume.colliders");
    } catch (const std::runtime_error& error) {
        return Result<std::vector<VolumeFluidCollider>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid collider fields or count", error.what()));
    }
    return Result<std::vector<VolumeFluidCollider>>::success(std::move(decoded));
}

Result<VolumeFluidNozzlePose> decodeVolumeFluidNozzlePose(const Value& value) {
    VolumeFluidNozzlePose decoded;
    try {
        unpack(value, decoded, "fluids.volume.nozzlePose");
    } catch (const std::runtime_error& error) {
        return Result<VolumeFluidNozzlePose>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid nozzle pose fields", error.what()));
    }
    return Result<VolumeFluidNozzlePose>::success(decoded);
}

Value encodeVolumeFluidEmitterState(const VolumeFluidEmitterSnapshot& state) { return pack(state); }
Result<VolumeFluidEmitterSnapshot> decodeVolumeFluidEmitterState(const Value& value) {
    VolumeFluidEmitterSnapshot decoded;
    try {
        unpack(value, decoded, "fluids.volume.emitterState");
    } catch (const std::runtime_error& error) {
        return Result<VolumeFluidEmitterSnapshot>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid emitter-state fields", error.what()));
    }
    VolumeFluidEmitter    rate;
    VolumeFluidJetEmitter jet;
    auto                  validated = decoded.kind == 0 ? rate.restore(decoded) : jet.restore(decoded);
    if (!validated) return Result<VolumeFluidEmitterSnapshot>::failure(validated.status());
    return Result<VolumeFluidEmitterSnapshot>::success(std::move(decoded));
}

Result<std::vector<VolumeFluidParticle>> decodeVolumeFluidParticles(const Value& value) {
    std::vector<VolumeFluidParticle> decoded;
    try {
        unpack(value, decoded, "fluids.volume.particles");
    } catch (const std::runtime_error& error) {
        return Result<std::vector<VolumeFluidParticle>>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Invalid, missing or unknown particle field", error.what()));
    }
    return Result<std::vector<VolumeFluidParticle>>::success(std::move(decoded));
}

Value encodeVolumeFluidEmitterCheckpoint(const VolumeFluidEmitterCheckpoint& checkpoint) {
    Value value(Value::Object{});
    value.set("schema", checkpoint.schema);
    value.set("version", int64_t(checkpoint.version));
    value.set("solver", encodeVolumeFluid(checkpoint.solver));
    value.set("emission", encodeVolumeFluidEmission(checkpoint.emission));
    value.set("controller", encodeVolumeFluidEmitterState(checkpoint.controller));
    return value;
}

Result<VolumeFluidEmitterCheckpoint> decodeVolumeFluidEmitterCheckpoint(const Value& value) {
    const auto failure = [](const char* message) {
        return Result<VolumeFluidEmitterCheckpoint>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, message, "fluids.volume.emitterCheckpoint"));
    };
    try {
        const auto* schema     = value.find("schema");
        const auto* version    = value.find("version");
        const auto* solver     = value.find("solver");
        const auto* emission   = value.find("emission");
        const auto* controller = value.find("controller");
        if (!value.isObject() || value.keys().size() != 5 || !schema || !schema->isString() ||
            schema->asString() != "eve.volume-fluid-emitter-checkpoint" || !version || !version->isInt64() ||
            version->asInt() != 1 || !solver || !emission || !controller)
            return failure("Invalid emitter checkpoint schema, version or fields");
        auto decodedSolver = decodeVolumeFluid(*solver);
        if (!decodedSolver) return Result<VolumeFluidEmitterCheckpoint>::failure(decodedSolver.status());
        auto decodedEmission = decodeVolumeFluidEmission(*emission);
        if (!decodedEmission) return Result<VolumeFluidEmitterCheckpoint>::failure(decodedEmission.status());
        auto decodedController = decodeVolumeFluidEmitterState(*controller);
        if (!decodedController) return Result<VolumeFluidEmitterCheckpoint>::failure(decodedController.status());
        return Result<VolumeFluidEmitterCheckpoint>::success(
            {schema->asString(), 1u, std::move(decodedSolver).takeValue(), std::move(decodedEmission).takeValue(),
             std::move(decodedController).takeValue()});
    } catch (const std::runtime_error&) {
        return failure("Invalid emitter checkpoint value types");
    }
}

Value encodeVolumeFluidEmission(const VolumeFluidEmission& emission) {
    Value out(Value::Object{});
    out.set("schema", "eve.volume-fluid-emission");
    out.set("version", 14);
    out.set("description", pack(emission));
    return out;
}

Result<VolumeFluidEmission> decodeVolumeFluidEmission(const Value& value) {
    VolumeFluidEmission decoded;
    try {
        const auto* schema      = value.find("schema");
        const auto* version     = value.find("version");
        const auto* description = value.find("description");
        if (!value.isObject() || value.keys().size() != 3 || !schema || !schema->isString() ||
            schema->asString() != "eve.volume-fluid-emission" || !version || !version->isInt64() ||
            (version->asInt() < 1 || version->asInt() > 14) || !description)
            throw std::runtime_error("fluids.volume.emission");
        auto migrated = *description;
        if (version->asInt() == 1) {
            if (!migrated.isObject() || migrated.find("distribution"))
                throw std::runtime_error("fluids.volume.emission/description");
            migrated.set("distribution", Value(Value::Array{}));
        }
        if (version->asInt() <= 2) {
            auto* prototype = migrated.find("prototype");
            if (!prototype || !prototype->isObject() || prototype->find("collisionFilter"))
                throw std::runtime_error("fluids.volume.emission/description/prototype");
            prototype->set("collisionFilter", int64_t(0xffff0001u));
        }
        if (version->asInt() <= 3) {
            auto* prototype = migrated.find("prototype");
            if (!prototype || !prototype->isObject() || prototype->find("radii") || prototype->find("orientation"))
                throw std::runtime_error("fluids.volume.emission/description/prototype");
            prototype->set("radii", pack(glm::vec3(0.f)));
            prototype->set("orientation", pack(glm::vec4(0, 0, 0, 1)));
        }
        if (version->asInt() <= 4) {
            auto* prototype = migrated.find("prototype");
            auto* material  = prototype && prototype->isObject() ? prototype->find("material") : nullptr;
            if (!prototype || !prototype->isObject() || prototype->find("actorGroup") ||
                prototype->find("selfCollide") || !material || !material->isObject() ||
                material->find("atmosphericPressure"))
                throw std::runtime_error("fluids.volume.emission/description/prototype");
            prototype->set("actorGroup", int64_t(0));
            prototype->set("selfCollide", true);
            material->set("atmosphericPressure", 0.0);
        }
        if (version->asInt() <= 5) {
            if (!migrated.isObject() || migrated.find("granularRadiusRandomness"))
                throw std::runtime_error("fluids.volume.emission/description");
            migrated.set("granularRadiusRandomness", 0.0);
        }
        if (version->asInt() <= 6) {
            auto* prototype = migrated.find("prototype");
            auto* material  = prototype && prototype->isObject() ? prototype->find("material") : nullptr;
            if (!material || !material->isObject() || material->find("smoothing"))
                throw std::runtime_error("fluids.volume.emission/description/prototype/material");
            material->set("smoothing", 2.0);
        }
        if (version->asInt() <= 7) {
            auto* prototype = migrated.find("prototype");
            auto* material  = prototype && prototype->isObject() ? prototype->find("material") : nullptr;
            if (!prototype || !prototype->isObject() || prototype->find("angularVelocity") || !material ||
                !material->isObject() || material->find("rollingContacts") || material->find("rollingFriction"))
                throw std::runtime_error("fluids.volume.emission/description/prototype");
            prototype->set("angularVelocity", pack(glm::vec3(0.f)));
            material->set("rollingContacts", false);
            material->set("rollingFriction", 0.0);
        }
        if (version->asInt() <= 8) {
            auto* prototype = migrated.find("prototype");
            auto* material  = prototype && prototype->isObject() ? prototype->find("material") : nullptr;
            if (!material || !material->isObject() || material->find("dynamicFriction") ||
                material->find("staticFriction"))
                throw std::runtime_error("fluids.volume.emission/description/prototype/material");
            material->set("dynamicFriction", 0.2);
            material->set("staticFriction", 0.2);
        }
        if (version->asInt() <= 9) {
            const auto* source = migrated.find("distribution");
            if (!source || !source->isArray() || source->arraySize() > 4096)
                throw std::runtime_error("fluids.volume.emission/description/distribution");
            Value::Array points;
            points.reserve(source->arraySize());
            for (size_t i = 0; i < source->arraySize(); ++i) {
                auto point = source->at(i);
                if (!point.isObject() || point.find("direction"))
                    throw std::runtime_error("fluids.volume.emission/description/distribution");
                point.set("direction", pack(glm::vec3(0, 0, 1)));
                points.push_back(std::move(point));
            }
            migrated.set("distribution", Value(std::move(points)));
        }
        if (version->asInt() <= 10) {
            if (!migrated.isObject() || migrated.find("randomVelocity"))
                throw std::runtime_error("fluids.volume.emission/description");
            migrated.set("randomVelocity", 0.0);
        }
        if (version->asInt() <= 11) {
            auto* prototype = migrated.find("prototype");
            auto* material  = prototype && prototype->isObject() ? prototype->find("material") : nullptr;
            if (!material || !material->isObject() || material->find("stickiness") || material->find("stickDistance") ||
                material->find("frictionCombine") || material->find("stickinessCombine"))
                throw std::runtime_error("fluids.volume.emission/description/prototype/material");
            material->set("stickiness", 0.0);
            material->set("stickDistance", 0.0);
            material->set("frictionCombine", 0);
            material->set("stickinessCombine", 0);
        }
        if (version->asInt() <= 12) {
            if (!migrated.isObject() || migrated.find("useShapeColor"))
                throw std::runtime_error("fluids.volume.emission/description");
            migrated.set("useShapeColor", true);
        }
        if (version->asInt() <= 13) {
            if (!migrated.isObject() || migrated.find("actorCapacity"))
                throw std::runtime_error("fluids.volume.emission/description");
            migrated.set("actorCapacity", int64_t(1000));
        }
        unpack(migrated, decoded, "fluids.volume.emission/description");
        if (decoded.actorCapacity < 1 || decoded.actorCapacity > 65536)
            throw std::runtime_error("fluids.volume.emission/description/actorCapacity");
    } catch (const std::runtime_error& error) {
        return Result<VolumeFluidEmission>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Invalid emission schema, version or fields", error.what()));
    }
    return Result<VolumeFluidEmission>::success(std::move(decoded));
}

}  // namespace eve::fluids
