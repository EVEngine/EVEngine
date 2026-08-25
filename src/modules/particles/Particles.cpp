#include "particles/Particles.h"
#include "common/Module.h"
#include "data/DataModule.h"
#include "data/JsonDocument.h"
#include "filesystem/FileData.h"
#include "filesystem/Filesystem.h"
#include "graphics/Graphics.h"
#include "particles/ParticleConfig.h"
#include "particles/ParticleRuntime.h"
#include "particles/ParticleSystem.h"
#include "particles/ParticlesCapabilities.h"

#include <Poco/Dynamic/Var.h>
#include <Poco/JSON/Object.h>

#include <memory>
#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::particles {

Particles::Particles() { registerParticlesCapabilities(); }

namespace {

int bufferFromJson(const std::string &json, int fallback) {
    auto *dm = eve::data::DataModule::create();
    std::string err;
    std::unique_ptr<data::JsonDocument> doc(dm->decodeJson(json, &err));
    if (!doc || !doc->isObject()) return fallback;
    auto obj = doc->object();
    if (!obj || !obj->has("buffer")) return fallback;
    try {
        int n = obj->getValue<int>("buffer");
        return n > 0 ? n : fallback;
    } catch (...) {
        return fallback;
    }
}

}  // namespace

Module_IMPL(Particles, new Particles());

ParticleEmitter *Particles::newEmitter(int bufferSize) {
    return ParticleEmitter::createEmitter(bufferSize);
}

ParticleEmitter *Particles::newEmitterFromFile(const std::string &path) {
    auto *fs = eve::ModuleManager::getInstance<eve::filesystem::Filesystem>("Filesystem");
    if (!fs) fs = eve::filesystem::Filesystem::create();

    std::unique_ptr<eve::filesystem::FileData> data;
    try {
        data.reset(fs->read(path));
    } catch (...) {
        return nullptr;
    }
    if (!data || data->getSize() == 0) return nullptr;

    std::string text(static_cast<const char *>(data->getData()), data->getSize());
    int buffer = bufferFromJson(text, 1000);
    ParticleEmitter *e = ParticleEmitter::createEmitter(buffer);
    if (!loadConfigFile(e, path, nullptr)) {
        // Entity stays in ECS registry; still return for inspection, or null?
        // Prefer null on failed apply after create — leave entity (ECS has no destroy).
        return e;  // path may have failed; emitter still usable
    }
    return e;
}

void Particles::update(float dt) {
    ParticleConfigSystem::poll();
    ParticleSimSystem::update(dt);
    ParticleLightSystem::update();
}

void Particles::render(graphics::Graphics *gfx) { ParticleRenderSystem::render(gfx); }

int Particles::pollConfigs() { return ParticleConfigSystem::poll(); }

int Particles::getEmitterCount() const {
    if (ecs::current()->getManager<ParticleEmitter>() == nullptr) return 0;
    int n = 0;
    auto view = ecs::View<ParticleEmitter, ParticleEmitter::Config>();
    for (auto it = view.begin(); it != view.end(); ++it) ++n;
    return n;
}

void Particles::setBudget(int maxParticles, int maxSimulatedEmitters) {
    auto &budget = particleBudgetConfig();
    budget.maxParticles = maxParticles > 0 ? maxParticles : 0;
    budget.maxSimulatedEmitters = maxSimulatedEmitters > 0 ? maxSimulatedEmitters : 0;
}

int Particles::getMaxParticles() const { return particleBudgetConfig().maxParticles; }

int Particles::getMaxSimulatedEmitters() const {
    return particleBudgetConfig().maxSimulatedEmitters;
}

void Particles::setQualityLevel(int quality) {
    particleBudgetConfig().qualityLevel = quality < 0 ? 0 : (quality > 3 ? 3 : quality);
}

int Particles::getQualityLevel() const { return particleBudgetConfig().qualityLevel; }

int Particles::getLastSimulatedEmitters() const {
    return particleFrameStats().emittersSimulated;
}
int Particles::getLastCulledEmitters() const { return particleFrameStats().emittersCulled; }
int Particles::getLastBudgetSkippedEmitters() const {
    const auto &stats = particleFrameStats();
    return stats.emittersBudgetSkipped + stats.emittersQualitySkipped;
}
int Particles::getLastParticleCount() const { return particleFrameStats().particlesAfter; }
int Particles::getLastSpawnedParticles() const { return particleFrameStats().particlesSpawned; }
int Particles::getLastDroppedSpawns() const { return particleFrameStats().droppedSpawns; }
int   Particles::getLastGpuResidentEmitters() const { return particleFrameStats().gpuResidentEmitters; }
int   Particles::getLastGpuResidentParticles() const { return particleFrameStats().gpuResidentParticles; }
int Particles::getLastRenderedParticles() const { return particleFrameStats().renderedParticles; }
float Particles::getLastSimulationMs() const {
    return static_cast<float>(particleFrameStats().simulationMs);
}
float Particles::getLastRenderMs() const {
    return static_cast<float>(particleFrameStats().renderMs);
}

void Particles::expose(ssq::Table &table) {
    auto cls = table.addClass(name, Particles::create, false);
    expose(cls);
    cls.addFunc("setBudget", &Particles::setBudget);
    cls.addFunc("getMaxParticles", &Particles::getMaxParticles);
    cls.addFunc("getMaxSimulatedEmitters", &Particles::getMaxSimulatedEmitters);
    cls.addFunc("setQualityLevel", &Particles::setQualityLevel);
    cls.addFunc("getQualityLevel", &Particles::getQualityLevel);
    cls.addFunc("getLastSimulatedEmitters", &Particles::getLastSimulatedEmitters);
    cls.addFunc("getLastCulledEmitters", &Particles::getLastCulledEmitters);
    cls.addFunc("getLastBudgetSkippedEmitters", &Particles::getLastBudgetSkippedEmitters);
    cls.addFunc("getLastParticleCount", &Particles::getLastParticleCount);
    cls.addFunc("getLastSpawnedParticles", &Particles::getLastSpawnedParticles);
    cls.addFunc("getLastDroppedSpawns", &Particles::getLastDroppedSpawns);
    cls.addFunc("getLastGpuResidentEmitters", &Particles::getLastGpuResidentEmitters);
    cls.addFunc("getLastGpuResidentParticles", &Particles::getLastGpuResidentParticles);
    cls.addFunc("getLastRenderedParticles", &Particles::getLastRenderedParticles);
    cls.addFunc("getLastSimulationMs", &Particles::getLastSimulationMs);
    cls.addFunc("getLastRenderMs", &Particles::getLastRenderMs);

    auto em = table.addClass<ParticleEmitter>(
        "Emitter", std::function<ParticleEmitter *()>([]() -> ParticleEmitter * { return nullptr; }),
        true);
    em.addFunc("setPosition", &ParticleEmitter::setPosition);
    em.addFunc("moveTo", &ParticleEmitter::moveTo);
    em.addFunc("getX", &ParticleEmitter::getX);
    em.addFunc("getY", &ParticleEmitter::getY);
    em.addFunc("setEmissionRate", &ParticleEmitter::setEmissionRate);
    em.addFunc("getEmissionRate", &ParticleEmitter::getEmissionRate);
    em.addFunc("setEmissionRateOverDistance", &ParticleEmitter::setEmissionRateOverDistance);
    em.addFunc("getEmissionRateOverDistance", &ParticleEmitter::getEmissionRateOverDistance);
    em.addFunc("setParticleLifetime", &ParticleEmitter::setParticleLifetime);
    // Alias — scripts often say "Life" for the same knob.
    em.addFunc("setParticleLife", &ParticleEmitter::setParticleLifetime);
    em.addFunc("getParticleLifetimeMin", &ParticleEmitter::getParticleLifetimeMin);
    em.addFunc("getParticleLifetimeMax", &ParticleEmitter::getParticleLifetimeMax);
    em.addFunc("setEmitterLifetime", &ParticleEmitter::setEmitterLifetime);
    // Aliases — scripts may say Life/Time for the same knob.
    em.addFunc("setEmitterLife", &ParticleEmitter::setEmitterLifetime);
    em.addFunc("setEmitterTime", &ParticleEmitter::setEmitterLifetime);
    em.addFunc("getEmitterLifetime", &ParticleEmitter::getEmitterLifetime);
    em.addFunc("setLooping", &ParticleEmitter::setLooping);
    em.addFunc("getLooping", &ParticleEmitter::getLooping);
    em.addFunc("setPlaybackSpeed", &ParticleEmitter::setPlaybackSpeed);
    em.addFunc("getPlaybackSpeed", &ParticleEmitter::getPlaybackSpeed);
    em.addFunc("setFixedTimeStep", &ParticleEmitter::setFixedTimeStep);
    em.addFunc("getFixedTimeStep", &ParticleEmitter::getFixedTimeStep);
    em.addFunc("setRandomSeed", &ParticleEmitter::setRandomSeed);
    em.addFunc("getRandomSeed", &ParticleEmitter::getRandomSeed);
    em.addFunc("setAutoRandomSeed", &ParticleEmitter::setAutoRandomSeed);
    em.addFunc("getAutoRandomSeed", &ParticleEmitter::getAutoRandomSeed);
    em.addFunc("setDirection", &ParticleEmitter::setDirection);
    em.addFunc("getDirection", &ParticleEmitter::getDirection);
    em.addFunc("setSpread", &ParticleEmitter::setSpread);
    em.addFunc("getSpread", &ParticleEmitter::getSpread);
    em.addFunc("setSpeed", &ParticleEmitter::setSpeed);
    em.addFunc("setLinearAcceleration", &ParticleEmitter::setLinearAcceleration);
    em.addFunc("setRadialAcceleration", &ParticleEmitter::setRadialAcceleration);
    em.addFunc("setTangentialAcceleration", &ParticleEmitter::setTangentialAcceleration);
    em.addFunc("setEmissionArea", &ParticleEmitter::setEmissionArea);
    em.addFunc("getEmissionAreaType", &ParticleEmitter::getEmissionAreaType);
    em.addFunc("getEmissionAreaX", &ParticleEmitter::getEmissionAreaX);
    em.addFunc("getEmissionAreaY", &ParticleEmitter::getEmissionAreaY);
    em.addFunc("setParticleSize", &ParticleEmitter::setParticleSize);
    em.addFunc("getParticleWidth", &ParticleEmitter::getParticleWidth);
    em.addFunc("getParticleHeight", &ParticleEmitter::getParticleHeight);
    em.addFunc("setSizes", &ParticleEmitter::setSizes);
    em.addFunc("setSizeVariation", &ParticleEmitter::setSizeVariation);
    em.addFunc("getSizeVariation", &ParticleEmitter::getSizeVariation);
    em.addFunc("setSpin", &ParticleEmitter::setSpin);
    em.addFunc("setStartRotation", &ParticleEmitter::setStartRotation);
    em.addFunc("addBurst", &ParticleEmitter::addBurst);
    em.addFunc("clearBursts", &ParticleEmitter::clearBursts);
    em.addFunc("setPrewarm", &ParticleEmitter::setPrewarm);
    em.addFunc("getPrewarmSeconds", &ParticleEmitter::getPrewarmSeconds);
    em.addFunc("setGravity", &ParticleEmitter::setGravity);
    em.addFunc("setDamping", &ParticleEmitter::setDamping);
    em.addFunc("setLimitVelocity", &ParticleEmitter::setLimitVelocity);
    em.addFunc("clearVelocityCurve", &ParticleEmitter::clearVelocityCurve);
    em.addFunc("addVelocityCurvePoint", &ParticleEmitter::addVelocityCurvePoint);
    em.addFunc("setInheritVelocity", &ParticleEmitter::setInheritVelocity);
    em.addFunc("setSimulationSpace", &ParticleEmitter::setSimulationSpace);
    em.addFunc("setNoise", &ParticleEmitter::setNoise);
    em.addFunc("setGpuSimulation", &ParticleEmitter::setGpuSimulation);
    em.addFunc("getGpuSimulation", &ParticleEmitter::getGpuSimulation);
    em.addFunc("isGpuSimulationActive", &ParticleEmitter::isGpuSimulationActive);
    em.addFunc("isGpuFeatureSetSupported", &ParticleEmitter::isGpuFeatureSetSupported);
    em.addFunc("getSimulationBackend", &ParticleEmitter::getSimulationBackend);
    em.addFunc("getGpuFallbackReason", &ParticleEmitter::getGpuFallbackReason);
    em.addFunc("setFloatParameter", &ParticleEmitter::setFloatParameter);
    em.addFunc("getFloatParameter", &ParticleEmitter::getFloatParameter);
    em.addFunc("hasFloatParameter", &ParticleEmitter::hasFloatParameter);
    em.addFunc("clearFloatParameters", &ParticleEmitter::clearFloatParameters);
    em.addFunc("bindFloatParameter", &ParticleEmitter::bindFloatParameter);
    em.addFunc("clearFloatParameterBindings", &ParticleEmitter::clearFloatParameterBindings);
    em.addFunc("getResolvedParameterScale", &ParticleEmitter::getResolvedParameterScale);
    em.addFunc("setPriority", &ParticleEmitter::setPriority);
    em.addFunc("getPriority", &ParticleEmitter::getPriority);
    em.addFunc("setMinimumQuality", &ParticleEmitter::setMinimumQuality);
    em.addFunc("getMinimumQuality", &ParticleEmitter::getMinimumQuality);
    em.addFunc("setCullingMode", &ParticleEmitter::setCullingMode);
    em.addFunc("getCullingMode", &ParticleEmitter::getCullingMode);
    em.addFunc("setCullDistance", &ParticleEmitter::setCullDistance);
    em.addFunc("getCullDistance", &ParticleEmitter::getCullDistance);
    em.addFunc("setMaxSpawnPerFrame", &ParticleEmitter::setMaxSpawnPerFrame);
    em.addFunc("getMaxSpawnPerFrame", &ParticleEmitter::getMaxSpawnPerFrame);
    em.addFunc("setCollision", &ParticleEmitter::setCollision);
    em.addFunc("setCollisionBounds", &ParticleEmitter::setCollisionBounds);
    em.addFunc("setWorldCollision", &ParticleEmitter::setWorldCollision);
    em.addFunc("setRenderMode", &ParticleEmitter::setRenderMode);
    em.addFunc("setRibbon", &ParticleEmitter::setRibbon);
    em.addFunc("setSoftParticles", &ParticleEmitter::setSoftParticles);
    em.addFunc("isSoftParticlesActive", &ParticleEmitter::isSoftParticlesActive);
    em.addFunc("setRenderAxis", &ParticleEmitter::setRenderAxis);
    em.addFunc("setSortMode", &ParticleEmitter::setSortMode);
    em.addFunc("getSortMode", &ParticleEmitter::getSortMode);
    em.addFunc("setMaterialMode", &ParticleEmitter::setMaterialMode);
    em.addFunc("getMaterialMode", &ParticleEmitter::getMaterialMode);
    em.addFunc("setDistortionStrength", &ParticleEmitter::setDistortionStrength);
    em.addFunc("getDistortionStrength", &ParticleEmitter::getDistortionStrength);
    em.addFunc("setOverflowMode", &ParticleEmitter::setOverflowMode);
    em.addFunc("setMaxDeltaTime", &ParticleEmitter::setMaxDeltaTime);
    em.addFunc("addSubEmitter", &ParticleEmitter::addSubEmitter);
    em.addFunc("clearSubEmitters", &ParticleEmitter::clearSubEmitters);
    em.addFunc("addForceField", &ParticleEmitter::addForceField);
    em.addFunc("clearForceFields", &ParticleEmitter::clearForceFields);
    em.addFunc("setShader", &ParticleEmitter::setShader);
    em.addFunc("getShader", &ParticleEmitter::getShader);
    em.addFunc("setLights", &ParticleEmitter::setLights);
    em.addFunc("getLightsEnabled", &ParticleEmitter::getLightsEnabled);
    em.addFunc("setBlendMode", &ParticleEmitter::setBlendMode);
    em.addFunc("getBlendMode", &ParticleEmitter::getBlendMode);
    em.addFunc("setFlipbook", &ParticleEmitter::setFlipbook);
    em.addFunc("clearColorGradient", &ParticleEmitter::clearColorGradient);
    em.addFunc("addColorStop", &ParticleEmitter::addColorStop);
    em.addFunc("clearSizeCurve", &ParticleEmitter::clearSizeCurve);
    em.addFunc("addSizeCurvePoint", &ParticleEmitter::addSizeCurvePoint);
    em.addFunc("clearRotationCurve", &ParticleEmitter::clearRotationCurve);
    em.addFunc("addRotationCurvePoint", &ParticleEmitter::addRotationCurvePoint);
    em.addFunc("setColorStart", &ParticleEmitter::setColorStart);
    em.addFunc("setColorEnd", &ParticleEmitter::setColorEnd);
    em.addFunc("setTexture", &ParticleEmitter::setTexture);
    em.addFunc("setNormalTexture", &ParticleEmitter::setNormalTexture);
    em.addFunc("getNormalTexture", &ParticleEmitter::getNormalTexture);
    em.addFunc("setCanvas", &ParticleEmitter::setCanvas);
    em.addFunc("setCamera", &ParticleEmitter::setCamera);
    em.addFunc("setLayer", &ParticleEmitter::setLayer);
    em.addFunc("getLayer", &ParticleEmitter::getLayer);
    em.addFunc("setVisible", &ParticleEmitter::setVisible);
    em.addFunc("isVisible", &ParticleEmitter::isVisible);
    em.addFunc("start", &ParticleEmitter::start);
    em.addFunc("stop", &ParticleEmitter::stop);
    em.addFunc("pause", &ParticleEmitter::pause);
    em.addFunc("reset", &ParticleEmitter::reset);
    em.addFunc("emit", &ParticleEmitter::emit);
    em.addFunc("isActive", &ParticleEmitter::isActive);
    em.addFunc("isPaused", &ParticleEmitter::isPaused);
    em.addFunc("isStopped", &ParticleEmitter::isStopped);
    em.addFunc("getCount", &ParticleEmitter::getCount);
    em.addFunc("getBufferSize", &ParticleEmitter::getBufferSize);
    em.addFunc("applyPreset", &ParticleEmitter::applyPreset);
    em.addFunc("applyConfig", &ParticleEmitter::applyConfig);
    em.addFunc("loadConfig", &ParticleEmitter::loadConfig);
    em.addFunc("reloadConfig", &ParticleEmitter::reloadConfig);
    em.addFunc("setAutoReload", &ParticleEmitter::setAutoReload);
    em.addFunc("getAutoReload", &ParticleEmitter::getAutoReload);
    em.addFunc("getConfigPath", &ParticleEmitter::getConfigPath);
    em.addFunc("attachToBone", &ParticleEmitter::attachToBone);
    em.addFunc("attachToBoneByName", &ParticleEmitter::attachToBoneByName);
    em.addFunc("attachToSpineBone", &ParticleEmitter::attachToSpineBone);
    em.addFunc("attachToSpineBoneByName", &ParticleEmitter::attachToSpineBoneByName);
    em.addFunc("attachToSkeleton2D", &ParticleEmitter::attachToSkeleton2D);
    em.addFunc("attachToSkeleton3D", &ParticleEmitter::attachToSkeleton3D);
    em.addFunc("setAttachOffset", &ParticleEmitter::setAttachOffset);
    em.addFunc("setAttachPlane", &ParticleEmitter::setAttachPlane);
    em.addFunc("setAttachScale", &ParticleEmitter::setAttachScale);
    em.addFunc("setFollowBoneRotation", &ParticleEmitter::setFollowBoneRotation);
    em.addFunc("detach", &ParticleEmitter::detach);
    em.addFunc("isAttached", &ParticleEmitter::isAttached);
    em.addFunc("getAttachBone", &ParticleEmitter::getAttachBone);
    em.addFunc("getAttachKind", &ParticleEmitter::getAttachKind);
    em.addFunc("syncAttach", &ParticleEmitter::syncAttach);
    em.addFunc("setSkinSource", &ParticleEmitter::setSkinSource);
    em.addFunc("setSkinBoneFilter", &ParticleEmitter::setSkinBoneFilter);
    em.addFunc("setSkinBoneFilterByName", &ParticleEmitter::setSkinBoneFilterByName);
    em.addFunc("setSkinPlane", &ParticleEmitter::setSkinPlane);
    em.addFunc("setSkinScale", &ParticleEmitter::setSkinScale);
    em.addFunc("clearSkinSource", &ParticleEmitter::clearSkinSource);
    em.addFunc("hasSkinSource", &ParticleEmitter::hasSkinSource);
    em.addFunc("emitFromSkin", &ParticleEmitter::emitFromSkin);
}

void Particles::expose(ssq::Class &cls) {
    cls.addFunc("getName", &Particles::getName);
    cls.addFunc("newEmitter", &Particles::newEmitter);
    cls.addFunc("newEmitterFromFile", &Particles::newEmitterFromFile);
    cls.addFunc("update", &Particles::update);
    cls.addFunc("render", &Particles::render);
    cls.addFunc("pollConfigs", &Particles::pollConfigs);
    cls.addFunc("getEmitterCount", &Particles::getEmitterCount);
}

}  // namespace eve::particles
