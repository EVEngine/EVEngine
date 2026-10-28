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

#include "fluids/VolumeFluidCodecInternal.inc"

namespace eve::fluids {
Result<VolumeFluidSnapshot> decodeVolumeFluid(const Value& value) {
    VolumeFluidSnapshot decoded;
    try {
        auto        migrated = value;
        const auto* version  = value.find("version");
        if (version && version->isInt64() && version->asInt() == 1) {
            const auto* source = value.find("colliders");
            if (!source || !source->isArray() || source->arraySize() > 1024)
                throw std::runtime_error("fluids.volume/colliders");
            Value::Array colliders;
            for (size_t i = 0; i < source->arraySize(); ++i) {
                auto collider = source->at(i);
                if (!collider.isObject() || collider.find("rotation") || collider.find("angularVelocity"))
                    throw std::runtime_error("fluids.volume/colliders");
                collider.set("rotation", pack(glm::vec4(0, 0, 0, 1)));
                collider.set("angularVelocity", pack(glm::vec3(0.f)));
                colliders.push_back(std::move(collider));
            }
            migrated.set("colliders", Value(std::move(colliders)));
            migrated.set("version", 2);
        }
        const auto* migratedVersion = migrated.find("version");
        if (migratedVersion && migratedVersion->isInt64() && migratedVersion->asInt() == 2) {
            if (migrated.find("attachments")) throw std::runtime_error("fluids.volume/attachments");
            migrated.set("attachments", Value(Value::Array{}));
            migrated.set("version", 3);
        }
        migratedVersion = migrated.find("version");
        if (migratedVersion && migratedVersion->isInt64() && migratedVersion->asInt() == 3) {
            const auto* source = migrated.find("particles");
            if (!source || !source->isArray() || source->arraySize() > 65536)
                throw std::runtime_error("fluids.volume/particles");
            Value::Array particles;
            particles.reserve(source->arraySize());
            for (size_t i = 0; i < source->arraySize(); ++i) {
                auto particle = source->at(i);
                if (!particle.isObject() || particle.find("collisionFilter"))
                    throw std::runtime_error("fluids.volume/particles");
                particle.set("collisionFilter", int64_t(0xffff0001u));
                particles.push_back(std::move(particle));
            }
            migrated.set("particles", Value(std::move(particles)));
            migrated.set("version", 4);
        }
        migratedVersion = migrated.find("version");
        if (migratedVersion && migratedVersion->isInt64() && migratedVersion->asInt() == 4) {
            const auto* source   = migrated.find("particles");
            const auto* settings = migrated.find("settings");
            const auto* spacing  = settings && settings->isObject() ? settings->find("spacing") : nullptr;
            if (!source || !source->isArray() || source->arraySize() > 65536 || !spacing || !spacing->isNumeric())
                throw std::runtime_error("fluids.volume/particles");
            const float  radius = float(spacing->isInt64() ? spacing->asInt() : spacing->asDouble()) * .5f;
            Value::Array particles;
            particles.reserve(source->arraySize());
            for (size_t i = 0; i < source->arraySize(); ++i) {
                auto particle = source->at(i);
                if (!particle.isObject() || particle.find("radii") || particle.find("orientation"))
                    throw std::runtime_error("fluids.volume/particles");
                particle.set("radii", pack(glm::vec3(radius)));
                particle.set("orientation", pack(glm::vec4(0, 0, 0, 1)));
                particles.push_back(std::move(particle));
            }
            migrated.set("particles", Value(std::move(particles)));
            migrated.set("version", 5);
        }
        migratedVersion = migrated.find("version");
        if (migratedVersion && migratedVersion->isInt64() && migratedVersion->asInt() == 5) {
            const auto* particles = migrated.find("particles");
            const auto* colliders = migrated.find("colliders");
            const auto* source    = migrated.find("attachments");
            if (!particles || !particles->isArray() || !colliders || !colliders->isArray() || !source ||
                !source->isArray())
                throw std::runtime_error("fluids.volume/attachments");
            Value::Array attachments;
            attachments.reserve(source->arraySize());
            for (size_t i = 0; i < source->arraySize(); ++i) {
                auto attachment = source->at(i);
                if (!attachment.isObject() || attachment.find("localOrientation"))
                    throw std::runtime_error("fluids.volume/attachments");
                const auto* index = attachment.find("particleIndex");
                const auto* label = attachment.find("colliderLabel");
                if (!index || !index->isInt64() || index->asInt() < 0 ||
                    size_t(index->asInt()) >= particles->arraySize() || !label || !label->isInt64())
                    throw std::runtime_error("fluids.volume/attachments");
                const auto*  particleOrientation = particles->at(size_t(index->asInt())).find("orientation");
                const Value* colliderRotation    = nullptr;
                for (size_t j = 0; j < colliders->arraySize(); ++j) {
                    const auto* candidate = colliders->at(j).find("label");
                    if (candidate && candidate->isInt64() && candidate->asInt() == label->asInt()) {
                        colliderRotation = colliders->at(j).find("rotation");
                        break;
                    }
                }
                if (!particleOrientation || !colliderRotation) throw std::runtime_error("fluids.volume/attachments");
                glm::vec4 particleQ, colliderQ;
                unpack(*particleOrientation, particleQ, "fluids.volume/particles/orientation");
                unpack(*colliderRotation, colliderQ, "fluids.volume/colliders/rotation");
                const auto world = glm::quat(particleQ.w, particleQ.x, particleQ.y, particleQ.z);
                const auto frame = glm::quat(colliderQ.w, colliderQ.x, colliderQ.y, colliderQ.z);
                const auto local = glm::normalize(glm::conjugate(frame) * world);
                attachment.set("localOrientation", pack(glm::vec4(local.x, local.y, local.z, local.w)));
                attachments.push_back(std::move(attachment));
            }
            migrated.set("attachments", Value(std::move(attachments)));
            migrated.set("version", 6);
        }
        migratedVersion = migrated.find("version");
        if (migratedVersion && migratedVersion->isInt64() && migratedVersion->asInt() == 6) {
            if (migrated.find("simplexes")) throw std::runtime_error("fluids.volume/simplexes");
            migrated.set("simplexes", Value(Value::Array{}));
            migrated.set("version", 7);
        }
        migratedVersion = migrated.find("version");
        if (migratedVersion && migratedVersion->isInt64() && migratedVersion->asInt() == 7) {
            const auto* source = migrated.find("colliders");
            if (!source || !source->isArray() || source->arraySize() > 1024)
                throw std::runtime_error("fluids.volume/colliders");
            Value::Array colliders;
            colliders.reserve(source->arraySize());
            for (size_t i = 0; i < source->arraySize(); ++i) {
                auto collider = source->at(i);
                if (!collider.isObject() || collider.find("collisionFilter"))
                    throw std::runtime_error("fluids.volume/colliders");
                collider.set("collisionFilter", int64_t(0xffff0001u));
                colliders.push_back(std::move(collider));
            }
            migrated.set("colliders", Value(std::move(colliders)));
            migrated.set("version", 8);
        }
        migratedVersion = migrated.find("version");
        if (migratedVersion && migratedVersion->isInt64() && migratedVersion->asInt() == 8) {
            const auto* source = migrated.find("particles");
            if (!source || !source->isArray() || source->arraySize() > 65536)
                throw std::runtime_error("fluids.volume/particles");
            Value::Array particles;
            particles.reserve(source->arraySize());
            for (size_t i = 0; i < source->arraySize(); ++i) {
                auto particle = source->at(i);
                if (!particle.isObject() || particle.find("actorGroup") || particle.find("selfCollide"))
                    throw std::runtime_error("fluids.volume/particles");
                particle.set("actorGroup", int64_t(0));
                particle.set("selfCollide", true);
                particles.push_back(std::move(particle));
            }
            migrated.set("particles", Value(std::move(particles)));
            migrated.set("version", 9);
        }
        migratedVersion = migrated.find("version");
        if (migratedVersion && migratedVersion->isInt64() && migratedVersion->asInt() == 9) {
            const auto* source = migrated.find("particles");
            if (!source || !source->isArray() || source->arraySize() > 65536)
                throw std::runtime_error("fluids.volume/particles");
            Value::Array particles;
            particles.reserve(source->arraySize());
            for (size_t i = 0; i < source->arraySize(); ++i) {
                auto  particle = source->at(i);
                auto* material = particle.isObject() ? particle.find("material") : nullptr;
                if (!material || !material->isObject() || material->find("atmosphericPressure"))
                    throw std::runtime_error("fluids.volume/particles/material");
                material->set("atmosphericPressure", 0.0);
                particles.push_back(std::move(particle));
            }
            migrated.set("particles", Value(std::move(particles)));
            migrated.set("version", 10);
        }
        migratedVersion = migrated.find("version");
        if (migratedVersion && migratedVersion->isInt64() && migratedVersion->asInt() == 10) {
            const auto* source = migrated.find("particles");
            if (!source || !source->isArray() || source->arraySize() > 65536)
                throw std::runtime_error("fluids.volume/particles");
            Value::Array particles;
            particles.reserve(source->arraySize());
            for (size_t i = 0; i < source->arraySize(); ++i) {
                auto  particle = source->at(i);
                auto* material = particle.isObject() ? particle.find("material") : nullptr;
                if (!material || !material->isObject() || material->find("smoothing"))
                    throw std::runtime_error("fluids.volume/particles/material");
                material->set("smoothing", 2.0);
                particles.push_back(std::move(particle));
            }
            migrated.set("particles", Value(std::move(particles)));
            migrated.set("version", 11);
        }
        migratedVersion = migrated.find("version");
        if (migratedVersion && migratedVersion->isInt64() && migratedVersion->asInt() == 11) {
            const auto* source = migrated.find("particles");
            if (!source || !source->isArray() || source->arraySize() > 65536)
                throw std::runtime_error("fluids.volume/particles");
            Value::Array particles;
            particles.reserve(source->arraySize());
            for (size_t i = 0; i < source->arraySize(); ++i) {
                auto  particle = source->at(i);
                auto* material = particle.isObject() ? particle.find("material") : nullptr;
                if (!material || !material->isObject() || particle.find("angularVelocity") ||
                    material->find("rollingContacts") || material->find("rollingFriction"))
                    throw std::runtime_error("fluids.volume/particles/material");
                particle.set("angularVelocity", pack(glm::vec3(0.f)));
                material->set("rollingContacts", false);
                material->set("rollingFriction", 0.0);
                particles.push_back(std::move(particle));
            }
            migrated.set("particles", Value(std::move(particles)));
            migrated.set("version", 12);
        }
        migratedVersion = migrated.find("version");
        if (migratedVersion && migratedVersion->isInt64() && migratedVersion->asInt() == 12) {
            const auto* source = migrated.find("particles");
            if (!source || !source->isArray() || source->arraySize() > 65536)
                throw std::runtime_error("fluids.volume/particles");
            Value::Array particles;
            particles.reserve(source->arraySize());
            for (size_t i = 0; i < source->arraySize(); ++i) {
                auto  particle = source->at(i);
                auto* material = particle.isObject() ? particle.find("material") : nullptr;
                if (!material || !material->isObject() || material->find("dynamicFriction") ||
                    material->find("staticFriction"))
                    throw std::runtime_error("fluids.volume/particles/material");
                material->set("dynamicFriction", 0.2);
                material->set("staticFriction", 0.2);
                particles.push_back(std::move(particle));
            }
            migrated.set("particles", Value(std::move(particles)));
            migrated.set("version", 13);
        }
        migratedVersion = migrated.find("version");
        if (migratedVersion && migratedVersion->isInt64() && migratedVersion->asInt() == 13) {
            if (migrated.find("sdfColliders")) throw std::runtime_error("fluids.volume/sdfColliders");
            migrated.set("sdfColliders", Value(Value::Array{}));
            migrated.set("version", 14);
        }
        migratedVersion = migrated.find("version");
        if (migratedVersion && migratedVersion->isInt64() && migratedVersion->asInt() == 14) {
            if (migrated.find("heightFieldColliders")) throw std::runtime_error("fluids.volume/heightFieldColliders");
            migrated.set("heightFieldColliders", Value(Value::Array{}));
            migrated.set("version", 15);
        }
        migratedVersion = migrated.find("version");
        if (migratedVersion && migratedVersion->isInt64() && migratedVersion->asInt() == 15) {
            for (const char* field : {"colliders", "sdfColliders", "heightFieldColliders"}) {
                const auto* source = migrated.find(field);
                if (!source || !source->isArray()) throw std::runtime_error(std::string("fluids.volume/") + field);
                Value::Array colliders;
                colliders.reserve(source->arraySize());
                for (size_t i = 0; i < source->arraySize(); ++i) {
                    auto collider = source->at(i);
                    if (!collider.isObject() || collider.find("isTrigger"))
                        throw std::runtime_error(std::string("fluids.volume/") + field + "/isTrigger");
                    collider.set("isTrigger", false);
                    colliders.push_back(std::move(collider));
                }
                migrated.set(field, Value(std::move(colliders)));
            }
            migrated.set("version", 16);
        }
        migratedVersion = migrated.find("version");
        if (migratedVersion && migratedVersion->isInt64() && migratedVersion->asInt() == 16) {
            const auto* sourceParticles = migrated.find("particles");
            if (!sourceParticles || !sourceParticles->isArray()) throw std::runtime_error("fluids.volume/particles");
            Value::Array particles;
            particles.reserve(sourceParticles->arraySize());
            for (size_t i = 0; i < sourceParticles->arraySize(); ++i) {
                auto  particle = sourceParticles->at(i);
                auto* material = particle.isObject() ? particle.find("material") : nullptr;
                if (!material || !material->isObject() || material->find("stickiness") ||
                    material->find("stickDistance") || material->find("frictionCombine") ||
                    material->find("stickinessCombine"))
                    throw std::runtime_error("fluids.volume/particles/material");
                material->set("stickiness", 0.0);
                material->set("stickDistance", 0.0);
                material->set("frictionCombine", 0);
                material->set("stickinessCombine", 0);
                particles.push_back(std::move(particle));
            }
            migrated.set("particles", Value(std::move(particles)));
            for (const char* field : {"colliders", "sdfColliders", "heightFieldColliders"}) {
                const auto* source = migrated.find(field);
                if (!source || !source->isArray()) throw std::runtime_error(std::string("fluids.volume/") + field);
                Value::Array colliders;
                colliders.reserve(source->arraySize());
                for (size_t i = 0; i < source->arraySize(); ++i) {
                    auto collider = source->at(i);
                    if (!collider.isObject() || collider.find("staticFriction") || collider.find("rollingFriction") ||
                        collider.find("stickiness") || collider.find("stickDistance") ||
                        collider.find("frictionCombine") || collider.find("stickinessCombine") ||
                        collider.find("rollingContacts"))
                        throw std::runtime_error(std::string("fluids.volume/") + field + "/material");
                    collider.set("staticFriction", *collider.find("friction"));
                    collider.set("rollingFriction", 0.0);
                    collider.set("stickiness", 0.0);
                    collider.set("stickDistance", 0.0);
                    collider.set("frictionCombine", 0);
                    collider.set("stickinessCombine", 0);
                    collider.set("rollingContacts", false);
                    colliders.push_back(std::move(collider));
                }
                migrated.set(field, Value(std::move(colliders)));
            }
            migrated.set("version", 17);
        }
        migratedVersion = migrated.find("version");
        if (migratedVersion && migratedVersion->isInt64() && migratedVersion->asInt() == 17) {
            const auto* source = migrated.find("attachments");
            if (!source || !source->isArray()) throw std::runtime_error("fluids.volume/attachments");
            Value::Array attachments;
            attachments.reserve(source->arraySize());
            for (size_t i = 0; i < source->arraySize(); ++i) {
                auto attachment = source->at(i);
                if (!attachment.isObject() || attachment.find("constrainOrientation"))
                    throw std::runtime_error("fluids.volume/attachments/constrainOrientation");
                // All pre-v18 native anchors drove orientation, so migration preserves behavior.
                attachment.set("constrainOrientation", true);
                attachments.push_back(std::move(attachment));
            }
            migrated.set("attachments", Value(std::move(attachments)));
            migrated.set("version", 18);
        }
        migratedVersion = migrated.find("version");
        if (migratedVersion && migratedVersion->isInt64() && migratedVersion->asInt() == 18) {
            const auto* source = migrated.find("attachments");
            if (!source || !source->isArray()) throw std::runtime_error("fluids.volume/attachments");
            Value::Array attachments;
            attachments.reserve(source->arraySize());
            for (size_t i = 0; i < source->arraySize(); ++i) {
                auto attachment = source->at(i);
                if (!attachment.isObject() || attachment.find("dynamic") || attachment.find("compliance") ||
                    attachment.find("breakThreshold"))
                    throw std::runtime_error("fluids.volume/attachments/dynamic");
                attachment.set("dynamic", false);
                attachment.set("compliance", 0.0);
                attachment.set("breakThreshold", 1e12);
                attachments.push_back(std::move(attachment));
            }
            migrated.set("attachments", Value(std::move(attachments)));
            migrated.set("version", 19);
        }
        migratedVersion = migrated.find("version");
        if (migratedVersion && migratedVersion->isInt64() && migratedVersion->asInt() == 19) {
            if (migrated.find("stitches")) throw std::runtime_error("fluids.volume/stitches");
            migrated.set("stitches", Value(Value::Array{}));
            migrated.set("version", 20);
        }
        unpack(migrated, decoded, "fluids.volume");
    } catch (const std::runtime_error& error) {
        return Result<VolumeFluidSnapshot>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Invalid, missing or unknown volume-fluid field", error.what()));
    }
    auto solver = VolumeFluid::create(decoded.settings);
    if (!solver) return Result<VolumeFluidSnapshot>::failure(solver.status());
    auto valid = solver.value()->restore(decoded);
    if (!valid) return Result<VolumeFluidSnapshot>::failure(valid.status());
    return Result<VolumeFluidSnapshot>::success(std::move(decoded));
}

}  // namespace eve::fluids
