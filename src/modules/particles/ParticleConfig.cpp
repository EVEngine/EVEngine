#include "particles/ParticleConfig.h"

#include "common/Module.h"
#include "data/DataModule.h"
#include "data/JsonDocument.h"
#include "filesystem/Filesystem.h"
#include "filesystem/FileData.h"
#include "filesystem/HotReload.h"
#include "graphics/Graphics.h"

#include <Poco/Dynamic/Var.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>

#include <memory>

namespace eve::particles {
namespace {

float asFloat(const Poco::Dynamic::Var &v, float fallback) {
    try {
        if (v.isEmpty()) return fallback;
        return static_cast<float>(v.convert<double>());
    } catch (...) {
        return fallback;
    }
}

bool asBool(const Poco::Dynamic::Var &v, bool fallback) {
    try {
        if (v.isEmpty()) return fallback;
        return v.convert<bool>();
    } catch (...) {
        return fallback;
    }
}

std::string asString(const Poco::Dynamic::Var &v) {
    try {
        if (v.isEmpty()) return {};
        return v.convert<std::string>();
    } catch (...) {
        return {};
    }
}

bool readVec2(Poco::JSON::Object::Ptr o, const char *key, float &a, float &b) {
    if (!o || !o->has(key)) return false;
    Poco::JSON::Array::Ptr arr;
    try {
        arr = o->getArray(key);
    } catch (...) {
        return false;
    }
    if (!arr || arr->size() < 2) return false;
    a = asFloat(arr->get(0), a);
    b = asFloat(arr->get(1), b);
    return true;
}

bool readVec4(Poco::JSON::Object::Ptr o, const char *key, float &a, float &b, float &c, float &d) {
    if (!o || !o->has(key)) return false;
    Poco::JSON::Array::Ptr arr;
    try {
        arr = o->getArray(key);
    } catch (...) {
        return false;
    }
    if (!arr || arr->size() < 4) return false;
    a = asFloat(arr->get(0), a);
    b = asFloat(arr->get(1), b);
    c = asFloat(arr->get(2), c);
    d = asFloat(arr->get(3), d);
    return true;
}

void tryLoadTexture(ParticleEmitter *emitter, const std::string &path) {
    if (path.empty()) return;
    auto *gfx = eve::ModuleManager::getInstance<eve::graphics::Graphics>("Graphics");
    if (!gfx) return;
    try {
        graphics::Texture *tex = gfx->newTextureFromFile(path);
        emitter->setTexture(tex);
        if (auto *hot = eve::ModuleManager::getInstance<eve::filesystem::HotReload>("HotReload"))
            hot->bind(path, "texture");
    } catch (...) {
        // Leave existing / null texture; config still applied.
    }
}

int64_t fileModtime(const std::string &path) {
    auto *fs = eve::ModuleManager::getInstance<eve::filesystem::Filesystem>("Filesystem");
    if (!fs) fs = eve::filesystem::Filesystem::create();
    eve::filesystem::Filesystem::Info info{};
    if (!fs->getInfo(path, info)) return -1;
    return info.modtime;
}

}  // namespace

bool applyConfigDocument(ParticleEmitter *emitter, data::JsonDocument *doc) {
    if (!emitter || !doc || !doc->isObject()) return false;
    auto obj = doc->object();
    if (!obj) return false;

    // Optional named preset first; later keys override.
    if (obj->has("preset")) {
        std::string preset = asString(obj->get("preset"));
        if (!preset.empty()) emitter->applyPreset(preset);
    }

    if (obj->has("x") || obj->has("y")) {
        float x = emitter->getX();
        float y = emitter->getY();
        if (obj->has("x")) x = asFloat(obj->get("x"), x);
        if (obj->has("y")) y = asFloat(obj->get("y"), y);
        emitter->setPosition(x, y);
    }

    if (obj->has("emissionRate"))
        emitter->setEmissionRate(asFloat(obj->get("emissionRate"), emitter->getEmissionRate()));

    float lifeMin = emitter->getParticleLifetimeMin();
    float lifeMax = emitter->getParticleLifetimeMax();
    if (readVec2(obj, "particleLifetime", lifeMin, lifeMax))
        emitter->setParticleLifetime(lifeMin, lifeMax);
    else {
        if (obj->has("lifeMin")) lifeMin = asFloat(obj->get("lifeMin"), lifeMin);
        if (obj->has("lifeMax")) lifeMax = asFloat(obj->get("lifeMax"), lifeMax);
        if (obj->has("lifeMin") || obj->has("lifeMax"))
            emitter->setParticleLifetime(lifeMin, lifeMax);
    }

    if (obj->has("emitterLife"))
        emitter->setEmitterLifetime(asFloat(obj->get("emitterLife"), emitter->getEmitterLifetime()));

    if (obj->has("direction"))
        emitter->setDirection(asFloat(obj->get("direction"), emitter->getDirection()));
    if (obj->has("spread"))
        emitter->setSpread(asFloat(obj->get("spread"), emitter->getSpread()));

    float speedMin = 0.f, speedMax = 0.f;
    if (readVec2(obj, "speed", speedMin, speedMax))
        emitter->setSpeed(speedMin, speedMax);

    float ax0 = 0, ay0 = 0, ax1 = 0, ay1 = 0;
    if (readVec4(obj, "linearAcceleration", ax0, ay0, ax1, ay1))
        emitter->setLinearAcceleration(ax0, ay0, ax1, ay1);

    float rad0 = 0, rad1 = 0;
    if (readVec2(obj, "radialAcceleration", rad0, rad1))
        emitter->setRadialAcceleration(rad0, rad1);

    float tan0 = 0, tan1 = 0;
    if (readVec2(obj, "tangentialAcceleration", tan0, tan1))
        emitter->setTangentialAcceleration(tan0, tan1);

    if (obj->has("emissionArea")) {
        try {
            auto area = obj->getObject("emissionArea");
            if (area) {
                std::string type = area->has("type") ? asString(area->get("type")) : "none";
                float ax = area->has("x") ? asFloat(area->get("x"), 0.f) : 0.f;
                float ay = area->has("y") ? asFloat(area->get("y"), 0.f) : 0.f;
                emitter->setEmissionArea(type, ax, ay);
            }
        } catch (...) {
        }
    } else if (obj->has("areaType")) {
        float ax = emitter->getEmissionAreaX();
        float ay = emitter->getEmissionAreaY();
        readVec2(obj, "areaSize", ax, ay);
        emitter->setEmissionArea(asString(obj->get("areaType")), ax, ay);
    }

    float pw = emitter->getParticleWidth(), ph = emitter->getParticleHeight();
    if (readVec2(obj, "particleSize", pw, ph))
        emitter->setParticleSize(pw, ph);

    float ss = 1.f, se = 1.f;
    if (readVec2(obj, "sizes", ss, se))
        emitter->setSizes(ss, se);

    if (obj->has("sizeVariation"))
        emitter->setSizeVariation(
            asFloat(obj->get("sizeVariation"), emitter->getSizeVariation()));

    float spin0 = 0, spin1 = 0;
    if (readVec2(obj, "spin", spin0, spin1)) emitter->setSpin(spin0, spin1);

    float r = 1, g = 1, b = 1, a = 1;
    if (readVec4(obj, "colorStart", r, g, b, a))
        emitter->setColorStart(r, g, b, a);
    if (readVec4(obj, "colorEnd", r, g, b, a))
        emitter->setColorEnd(r, g, b, a);

    if (obj->has("layer"))
        emitter->setLayer(static_cast<int>(asFloat(obj->get("layer"), float(emitter->getLayer()))));
    if (obj->has("visible"))
        emitter->setVisible(asBool(obj->get("visible"), emitter->isVisible()));

    if (obj->has("texture")) {
        std::string texPath = asString(obj->get("texture"));
        emitter->resource()->texturePath = texPath;
        tryLoadTexture(emitter, texPath);
    }

    if (obj->has("autoReload"))
        emitter->resource()->autoReload = asBool(obj->get("autoReload"), true);

    if (obj->has("autoStart") && asBool(obj->get("autoStart"), false))
        emitter->start();

    return true;
}

bool applyConfigText(ParticleEmitter *emitter, const std::string &json, std::string *error) {
    auto *dm = eve::data::DataModule::create();
    std::string err;
    std::unique_ptr<data::JsonDocument> doc(dm->decodeJson(json, &err));
    if (!doc) {
        if (error) *error = err.empty() ? "invalid json" : err;
        return false;
    }
    if (!applyConfigDocument(emitter, doc.get())) {
        if (error) *error = "config root must be object";
        return false;
    }
    return true;
}

bool loadConfigFile(ParticleEmitter *emitter, const std::string &path, std::string *error) {
    if (!emitter || path.empty()) {
        if (error) *error = "empty path";
        return false;
    }
    auto *fs = eve::ModuleManager::getInstance<eve::filesystem::Filesystem>("Filesystem");
    if (!fs) fs = eve::filesystem::Filesystem::create();

    std::unique_ptr<eve::filesystem::FileData> data;
    try {
        data.reset(fs->read(path));
    } catch (...) {
        if (error) *error = "read failed: " + path;
        return false;
    }
    if (!data || data->getSize() == 0) {
        if (error) *error = "empty file: " + path;
        return false;
    }

    std::string text(static_cast<const char *>(data->getData()), data->getSize());
    if (!applyConfigText(emitter, text, error)) return false;

    auto res = emitter->resource();
    res->path = path;
    res->modtime = fileModtime(path);
    // Prefer OS watch; mtime remains a fallback in ParticleConfigSystem.
    fs->watch(path);
    if (auto *hot = eve::ModuleManager::getInstance<eve::filesystem::HotReload>("HotReload"))
        hot->bind(path, "particle");
    return true;
}

bool reloadConfigFile(ParticleEmitter *emitter, std::string *error) {
    if (!emitter) return false;
    const std::string &path = emitter->resource()->path;
    if (path.empty()) {
        if (error) *error = "no config path";
        return false;
    }
    return loadConfigFile(emitter, path, error);
}

}  // namespace eve::particles
