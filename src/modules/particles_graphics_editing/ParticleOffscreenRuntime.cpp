#include "particles_graphics_editing/ParticleOffscreen.h"

#include "graphics/Canvas.h"
#include "particles/ParticleEmitter.h"
#include "particles/ParticleSystem.h"

#include <cmath>

namespace eve::particles_graphics_editing {
namespace {
template <class T> EditorResult<T> fail(EditorStatus status, const char* rule, std::string message) {
    return EditorResult<T>::error(status, RuleId(rule), std::move(message));
}
const EditorValue* field(const EditorValue& value, const char* key) {
    const auto* object=value.getIf<EditorValue::Object>(); if(!object)return nullptr;
    auto found=object->find(key); return found==object->end()?nullptr:&found->second;
}
class EmitterGuard {
public:
    explicit EmitterGuard(particles::ParticleEmitter* emitter):emitter_(emitter){}
    ~EmitterGuard(){if(emitter_)emitter_->release();}
    particles::ParticleEmitter* get()const{return emitter_;}
private: particles::ParticleEmitter* emitter_=nullptr;
};
}

EditorResult<void> ParticleEmitterOffscreenPresenter::draw(
    const ParticleOffscreenPreviewRequest& request, const ParticleGraphCompileResult& compiled,
    const ParticleGraphPreviewResult& estimate, graphics::Graphics* graphics,
    graphics::Canvas* canvas) {
    if (!graphics || !canvas || compiled.documentRevision != request.graph.revision ||
        estimate.documentRevision != request.graph.revision)
        return fail<void>(EditorStatus::Conflict,"editor.particles.preview-stale",
                          "Particle runtime preview requires current graph and graphics targets");
    const EditorValue* output=field(compiled.configuration,"output");
    const EditorValue* bufferValue=output?field(*output,"bufferSize"):nullptr;
    const auto* buffer=bufferValue?bufferValue->getIf<int64_t>():nullptr;
    if(!buffer||*buffer<1||*buffer>request.particleBudget)
        return fail<void>(EditorStatus::Rejected,"editor.particles.preview-buffer",
                          "Particle preview buffer is invalid or exceeds its budget");
    EmitterGuard emitter(particles::ParticleEmitter::createEmitter(static_cast<int>(*buffer)));
    if(!emitter.get())return fail<void>(EditorStatus::Failed,"editor.particles.preview-emitter",
                                        "Could not create an isolated particle emitter");
    auto applied=ParticleGraphRuntimeBuilder().apply(request.graph,emitter.get(),textures_);
    if(!applied.isAccepted())return applied;
    emitter.get()->setGpuSimulation(false);
    emitter.get()->setAutoRandomSeed(false);
    emitter.get()->setCanvas(canvas);
    emitter.get()->setPosition(static_cast<float>(request.width)*0.5f,
                               static_cast<float>(request.height)*0.5f);
    emitter.get()->start();
    const int fullSteps=static_cast<int>(std::floor(estimate.simulatedSeconds/request.fixedStep));
    const double remainder=estimate.simulatedSeconds-static_cast<double>(fullSteps)*request.fixedStep;
    std::uint64_t tick=0;
    for(int i=0;i<fullSteps+(remainder>1e-12?1:0);++i){
        const double seconds=i<fullSteps?request.fixedStep:remainder;
        auto duration=eve::Duration::fromSeconds(seconds);
        if(!duration)return fail<void>(EditorStatus::Rejected,"editor.particles.preview-step",
                                       "Particle preview fixed step is invalid");
        auto advanced=emitter.get()->advance({eve::SimulationTick(++tick),duration.value()});
        if(!advanced)return fail<void>(EditorStatus::Failed,"editor.particles.preview-advance",
                                       "Particle emitter rejected deterministic preview stepping");
    }
    particles::ParticleRenderSystem::renderEmitter(graphics,emitter.get());
    return EditorResult<void>::applied();
}

}  // namespace eve::particles_graphics_editing
