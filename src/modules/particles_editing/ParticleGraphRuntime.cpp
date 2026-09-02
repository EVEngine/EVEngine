#include "particles_editing/ParticleGraph.h"

#include "particles/ParticleEmitter.h"

#include <utility>

namespace eve::particles_editing {
namespace {

template <class T>
EditorResult<T> runtimeError(EditorStatus status, const char* rule, std::string message) {
    return eve::editing::failed<T>(status, RuleId(rule), std::move(message));
}

const EditorValue* field(const EditorValue& value, const char* key) {
    const auto* object = value.getIf<EditorValue::Object>();
    if (!object) return nullptr;
    const auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}

const EditorValue::Array& array(const EditorValue& value, const char* key) {
    return *field(value, key)->getIf<EditorValue::Array>();
}

float number(const EditorValue& value, const char* key) {
    return static_cast<float>(*field(value, key)->getIf<double>());
}

int integer(const EditorValue& value, const char* key) {
    return static_cast<int>(*field(value, key)->getIf<int64_t>());
}

const std::string& text(const EditorValue& value, const char* key) {
    return *field(value, key)->getIf<std::string>();
}

}  // namespace

EditorResult<void> ParticleGraphRuntimeBuilder::apply(const GraphDocumentData& graph,
                                                      particles::ParticleEmitter* emitter,
                                                      const TextureResolver& textures) const {
    if (!emitter)
        return runtimeError<void>(EditorStatus::Rejected, "editor.particles.runtime-emitter",
                                  "Live particle emitter is required");
    ParticleGraphDomain domain;
    const ParticleGraphCompileResult compiled = domain.compile(graph);
    if (compiled.status != EditorStatus::Applied)
        return EditorResult<void>::failure(eve::Status(compiled.status, compiled.diagnostics));
    const EditorValue* emission = field(compiled.configuration, "emission");
    const EditorValue* motion = field(compiled.configuration, "motion");
    const EditorValue* collision = field(compiled.configuration, "collision");
    const EditorValue* renderer = field(compiled.configuration, "renderer");
    const EditorValue* output = field(compiled.configuration, "output");
    if (emitter->getBufferSize() != integer(*output, "bufferSize"))
        return runtimeError<void>(EditorStatus::Conflict, "editor.particles.runtime-buffer-size",
                                  "Live emitter buffer size differs from the graph output; recreate the emitter");
    const std::string texture = text(*renderer, "texture");
    graphics::Texture* resolvedTexture = nullptr;
    if (!texture.empty()) {
        if (!textures)
            return runtimeError<void>(EditorStatus::Rejected, "editor.particles.texture-resolver",
                                      "Particle graph references a texture but no resolver was provided");
        resolvedTexture = textures(texture);
        if (!resolvedTexture)
            return runtimeError<void>(EditorStatus::NotFound, "editor.particles.texture-not-found",
                                      "Particle texture asset could not be resolved: " + texture);
    }

    const auto& lifetime = array(*emission, "lifetime");
    const auto& areaSize = array(*emission, "areaSize");
    emitter->setEmissionRate(number(*emission, "rate"));
    emitter->setParticleLifetime(static_cast<float>(*lifetime[0].getIf<double>()),
                                 static_cast<float>(*lifetime[1].getIf<double>()));
    emitter->setEmissionArea(text(*emission, "area"),
                             static_cast<float>(*areaSize[0].getIf<double>()),
                             static_cast<float>(*areaSize[1].getIf<double>()));
    emitter->setRandomSeed(integer(*emission, "randomSeed"));
    emitter->setAutoRandomSeed(*field(*emission, "autoRandomSeed")->getIf<bool>());
    emitter->setLooping(*field(*emission, "looping")->getIf<bool>());
    emitter->setEmitterLifetime(number(*emission, "emitterLife"));

    if (motion) {
        const auto& speed = array(*motion, "speed");
        const auto& gravity = array(*motion, "gravity");
        emitter->setDirection(number(*motion, "direction"));
        emitter->setSpread(number(*motion, "spread"));
        emitter->setSpeed(static_cast<float>(*speed[0].getIf<double>()),
                          static_cast<float>(*speed[1].getIf<double>()));
        emitter->setGravity(static_cast<float>(*gravity[0].getIf<double>()),
                            static_cast<float>(*gravity[1].getIf<double>()));
        emitter->setDamping(number(*motion, "damping"));
        emitter->setSimulationSpace(text(*motion, "simulationSpace"));
    }
    if (collision) {
        emitter->setCollision(text(*collision, "mode"), number(*collision, "radius"),
                              number(*collision, "restitution"), number(*collision, "lifetimeLoss"));
        emitter->setWorldCollision(*field(*collision, "world")->getIf<bool>());
    }
    const auto& size = array(*renderer, "size");
    emitter->setRenderMode(text(*renderer, "mode"));
    emitter->setMaterialMode(text(*renderer, "material"));
    emitter->setParticleSize(static_cast<float>(*size[0].getIf<double>()),
                             static_cast<float>(*size[1].getIf<double>()));
    emitter->setSortMode(text(*renderer, "sort"));
    emitter->setGpuSimulation(*field(*renderer, "gpu")->getIf<bool>());
    emitter->setSoftParticles(*field(*renderer, "softParticles")->getIf<bool>(),
                              number(*renderer, "softDepth"), number(*renderer, "softFade"));
    if (resolvedTexture) emitter->setTexture(resolvedTexture);
    emitter->setPriority(integer(*output, "priority"));
    emitter->setMinimumQuality(integer(*output, "minimumQuality"));
    emitter->setMaxSpawnPerFrame(integer(*output, "maxSpawnPerFrame"));
    emitter->setOverflowMode(text(*output, "overflow"));
    return eve::editing::applied<void>();
}

}  // namespace eve::particles_editing
