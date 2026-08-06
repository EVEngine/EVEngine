#include "particles/Particles.h"
#include "particles/ParticleSystem.h"
#include "particles/ParticleConfig.h"
#include "graphics/Graphics.h"
#include "data/DataModule.h"
#include "data/JsonDocument.h"
#include "filesystem/Filesystem.h"
#include "filesystem/FileData.h"
#include "common/Module.h"

#include <Poco/Dynamic/Var.h>
#include <Poco/JSON/Object.h>

#include <memory>
#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::particles {
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
}

void Particles::render(graphics::Graphics *gfx) { ParticleRenderSystem::render(gfx); }

int Particles::pollConfigs() { return ParticleConfigSystem::poll(); }

int Particles::getEmitterCount() const {
    if (ecs::ComponentManager<ParticleEmitter>::inst().registy == nullptr) return 0;
    int n = 0;
    auto view = ecs::View<ParticleEmitter, ParticleEmitter::Config>();
    for (auto it = view.begin(); it != view.end(); ++it) ++n;
    return n;
}

void Particles::expose(ssq::Table &table) {
    auto cls = table.addClass(name, Particles::create, false);
    expose(cls);

    auto em = table.addClass<ParticleEmitter>(
        "Emitter", std::function<ParticleEmitter *()>([]() -> ParticleEmitter * { return nullptr; }),
        true);
    em.addFunc("setPosition", &ParticleEmitter::setPosition);
    em.addFunc("moveTo", &ParticleEmitter::moveTo);
    em.addFunc("getX", &ParticleEmitter::getX);
    em.addFunc("getY", &ParticleEmitter::getY);
    em.addFunc("setEmissionRate", &ParticleEmitter::setEmissionRate);
    em.addFunc("getEmissionRate", &ParticleEmitter::getEmissionRate);
    em.addFunc("setParticleLifetime", &ParticleEmitter::setParticleLifetime);
    em.addFunc("getParticleLifetimeMin", &ParticleEmitter::getParticleLifetimeMin);
    em.addFunc("getParticleLifetimeMax", &ParticleEmitter::getParticleLifetimeMax);
    em.addFunc("setEmitterLifetime", &ParticleEmitter::setEmitterLifetime);
    em.addFunc("getEmitterLifetime", &ParticleEmitter::getEmitterLifetime);
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
    em.addFunc("setColorStart", &ParticleEmitter::setColorStart);
    em.addFunc("setColorEnd", &ParticleEmitter::setColorEnd);
    em.addFunc("setTexture", &ParticleEmitter::setTexture);
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
