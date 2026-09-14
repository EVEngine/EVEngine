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
