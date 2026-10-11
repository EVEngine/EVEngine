#include "particles/ParticleConfig.h"

#include "common/Json.h"
#include "common/Module.h"
#include "filesystem/FileData.h"
#include "filesystem/Filesystem.h"
#include "filesystem/HotReload.h"
#include "graphics/Graphics.h"

#include <memory>

namespace eve::particles {
namespace {

using Json = eve::json::Value;

bool readVec2(const Json &o, const char *key, float &a, float &b) {
    if (!o || !o.has(key)) return false;
    const Json arr = o.get(key);
    if (!arr.isArray() || arr.size() < 2) return false;
    a = arr.at(0).asFloat(a);
    b = arr.at(1).asFloat(b);
    return true;
}

bool readVec4(const Json &o, const char *key, float &a, float &b, float &c, float &d) {
    if (!o || !o.has(key)) return false;
    const Json arr = o.get(key);
    if (!arr.isArray() || arr.size() < 4) return false;
    a = arr.at(0).asFloat(a);
    b = arr.at(1).asFloat(b);
    c = arr.at(2).asFloat(c);
    d = arr.at(3).asFloat(d);
    return true;
}

bool readVec3(const Json &o, const char *key, float &a, float &b, float &c) {
    if (!o || !o.has(key)) return false;
    const Json arr = o.get(key);
    if (!arr.isArray() || arr.size() < 3) return false;
    a = arr.at(0).asFloat(a);
    b = arr.at(1).asFloat(b);
    c = arr.at(2).asFloat(c);
    return true;
}

bool readCurveArray(const Json &arr, ParticleCurve &curve) {
    if (!arr.isArray()) return false;
    bool any = false;
    for (size_t i = 0; i < arr.size(); ++i) {
        const Json item = arr.at(i);
        if (item.isObject()) {
            curve.add(item.getFloat("t", 0.f), item.getFloat("v", 0.f));
            any = true;
        } else if (item.isArray() && item.size() >= 2) {
            curve.add(item.at(0).asFloat(0.f), item.at(1).asFloat(0.f));
            any = true;
        }
    }
    return any;
}

bool readGradientArray(const Json &arr, ParticleGradient &gradient) {
    if (!arr.isArray()) return false;
    bool any = false;
    for (size_t i = 0; i < arr.size(); ++i) {
        float      t = 0.f, r = 1.f, g = 1.f, b = 1.f, a = 1.f;
        const Json item = arr.at(i);
        if (item.isObject()) {
            t = item.getFloat("t", t);
            r = item.getFloat("r", r);
            g = item.getFloat("g", g);
            b = item.getFloat("b", b);
            a = item.has("a") ? item.getFloat("a", a) : a;
            gradient.add(t, r, g, b, a);
            any = true;
        } else if (item.isArray() && item.size() >= 5) {
            t = item.at(0).asFloat(t);
            r = item.at(1).asFloat(r);
            g = item.at(2).asFloat(g);
            b = item.at(3).asFloat(b);
            a = item.at(4).asFloat(a);
            gradient.add(t, r, g, b, a);
            any = true;
        }
    }
    return any;
}

void tryLoadTexture(ParticleEmitter *emitter, const std::string &path, bool normalMap = false) {
    if (path.empty()) return;
    auto *gfx = eve::ModuleManager::getInstance<eve::graphics::Graphics>("Graphics");
    if (!gfx) return;
    try {
        graphics::Texture *tex = gfx->newTextureFromFile(path);
        if (normalMap)
            emitter->setNormalTexture(tex);
        else
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

[[nodiscard]] eve::Result<void> applyConfigDocument(ParticleEmitter* emitter, eve::json::Value root) {
    if (!emitter || !root.isObject())
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "config root must be an object", "particles.config"));
    const Json obj = root;

    // Optional named preset first; later keys override.
    if (obj.has("preset")) {
        std::string preset = obj.getString("preset");
        if (!preset.empty()) emitter->applyPreset(preset);
    }

    if (obj.has("x") || obj.has("y")) {
        float x = emitter->getX();
        float y = emitter->getY();
        if (obj.has("x")) x = obj.getFloat("x", x);
        if (obj.has("y")) y = obj.getFloat("y", y);
        emitter->setPosition(x, y);
    }

    if (obj.has("emissionRate")) emitter->setEmissionRate(obj.getFloat("emissionRate", emitter->getEmissionRate()));
    if (obj.has("emissionRateOverDistance"))
        emitter->setEmissionRateOverDistance(
            obj.getFloat("emissionRateOverDistance", emitter->getEmissionRateOverDistance()));

    float lifeMin = emitter->getParticleLifetimeMin();
    float lifeMax = emitter->getParticleLifetimeMax();
    if (readVec2(obj, "particleLife", lifeMin, lifeMax))
        emitter->setParticleLifetime(lifeMin, lifeMax);
    else {
        if (obj.has("lifeMin")) lifeMin = obj.getFloat("lifeMin", lifeMin);
        if (obj.has("lifeMax")) lifeMax = obj.getFloat("lifeMax", lifeMax);
        if (obj.has("lifeMin") || obj.has("lifeMax")) emitter->setParticleLifetime(lifeMin, lifeMax);
    }

    if (obj.has("emitterLife")) emitter->setEmitterLifetime(obj.getFloat("emitterLife", emitter->getEmitterLifetime()));
    if (obj.has("looping")) emitter->setLooping(obj.getBool("looping", emitter->getLooping()));
    if (obj.has("playbackSpeed")) emitter->setPlaybackSpeed(obj.getFloat("playbackSpeed", emitter->getPlaybackSpeed()));
    if (obj.has("fixedTimeStep")) {
        const float step     = obj.getFloat("fixedTimeStep", emitter->getFixedTimeStep());
        const int   maxSteps = obj.has("maxSubSteps") ? obj.getInt("maxSubSteps", 8) : emitter->config()->maxSubSteps;
        emitter->setFixedTimeStep(step, maxSteps);
    }
    if (obj.has("randomSeed")) emitter->setRandomSeed(obj.getInt("randomSeed", 0));
    if (obj.has("autoRandomSeed")) emitter->setAutoRandomSeed(obj.getBool("autoRandomSeed", true));

    if (obj.has("direction")) emitter->setDirection(obj.getFloat("direction", emitter->getDirection()));
    if (obj.has("spread")) emitter->setSpread(obj.getFloat("spread", emitter->getSpread()));

    float speedMin = 0.f, speedMax = 0.f;
    if (readVec2(obj, "speed", speedMin, speedMax)) emitter->setSpeed(speedMin, speedMax);

    float ax0 = 0, ay0 = 0, ax1 = 0, ay1 = 0;
    if (readVec4(obj, "linearAcceleration", ax0, ay0, ax1, ay1)) emitter->setLinearAcceleration(ax0, ay0, ax1, ay1);

    float rad0 = 0, rad1 = 0;
    if (readVec2(obj, "radialAcceleration", rad0, rad1)) emitter->setRadialAcceleration(rad0, rad1);

    float tan0 = 0, tan1 = 0;
    if (readVec2(obj, "tangentialAcceleration", tan0, tan1)) emitter->setTangentialAcceleration(tan0, tan1);

    if (obj.has("emissionArea")) {
        const Json area = obj.get("emissionArea");
        if (area.isObject()) {
            std::string type = area.has("type") ? area.getString("type") : "none";
            float       ax   = area.has("x") ? area.getFloat("x", 0.f) : 0.f;
            float       ay   = area.has("y") ? area.getFloat("y", 0.f) : 0.f;
            emitter->setEmissionArea(type, ax, ay);
        }
    } else if (obj.has("areaType")) {
        float ax = emitter->getEmissionAreaX();
        float ay = emitter->getEmissionAreaY();
        readVec2(obj, "areaSize", ax, ay);
        emitter->setEmissionArea(obj.getString("areaType"), ax, ay);
    }

    float pw = emitter->getParticleWidth(), ph = emitter->getParticleHeight();
    if (readVec2(obj, "particleSize", pw, ph)) emitter->setParticleSize(pw, ph);

    float ss = 1.f, se = 1.f;
    if (readVec2(obj, "sizes", ss, se)) emitter->setSizes(ss, se);

    if (obj.has("sizeVariation")) emitter->setSizeVariation(obj.getFloat("sizeVariation", emitter->getSizeVariation()));

    float spin0 = 0, spin1 = 0;
    if (readVec2(obj, "spin", spin0, spin1)) emitter->setSpin(spin0, spin1);

    float sr0 = 0, sr1 = 0;
    if (readVec2(obj, "startRotation", sr0, sr1)) emitter->setStartRotation(sr0, sr1);

    if (obj.has("bursts")) {
        const Json arr = obj.get("bursts");
        if (arr.isArray()) {
            emitter->clearBursts();
            for (size_t i = 0; i < arr.size(); ++i) {
                float      t     = 0.f;
                int        count = 0;
                const Json item  = arr.at(i);
                if (item.isObject()) {
                    t     = item.getFloat("time", 0.f);
                    count = item.getInt("count", 0);
                } else if (item.isArray() && item.size() >= 2) {
                    t     = item.at(0).asFloat(0.f);
                    count = item.at(1).asInt(0);
                }
                if (count > 0) emitter->addBurst(t, count);
            }
        }
    }

    if (obj.has("prewarm")) emitter->setPrewarm(obj.getFloat("prewarm", 0.f));

    float gx = 0, gy = 0;
    if (readVec2(obj, "gravity", gx, gy)) emitter->setGravity(gx, gy);
    if (obj.has("damping")) emitter->setDamping(obj.getFloat("damping", 0.f));
    if (obj.has("limitVelocity")) emitter->setLimitVelocity(obj.getFloat("limitVelocity", 0.f));
    if (obj.has("velocityOverLifetime")) {
        const Json arr = obj.get("velocityOverLifetime");
        if (arr.isArray()) {
            emitter->clearVelocityCurve();
            readCurveArray(arr, emitter->config()->velocityCurve);
        }
    }
    if (obj.has("inheritVelocity")) emitter->setInheritVelocity(obj.getFloat("inheritVelocity", 0.f));
    if (obj.has("simulationSpace")) emitter->setSimulationSpace(obj.getString("simulationSpace"));

    if (obj.has("noise")) {
        const Json n = obj.get("noise");
        if (n.isObject()) {
            float strength = n.has("strength") ? n.getFloat("strength", 0.f) : emitter->config()->noiseStrength;
            float freq     = n.has("frequency") ? n.getFloat("frequency", 1.f) : emitter->config()->noiseFrequency;
            float speed    = n.has("speed") ? n.getFloat("speed", 1.f) : emitter->config()->noiseSpeed;
            emitter->setNoise(strength, freq, speed);
        }
    } else if (obj.has("noiseStrength")) {
        emitter->setNoise(
            obj.getFloat("noiseStrength", 0.f),
            obj.has("noiseFrequency") ? obj.getFloat("noiseFrequency", 1.f) : emitter->config()->noiseFrequency,
            obj.has("noiseSpeed") ? obj.getFloat("noiseSpeed", 1.f) : emitter->config()->noiseSpeed);
    }

    if (obj.has("collision")) {
        const Json col = obj.get("collision");
        if (col.isObject()) {
            std::string mode   = col.has("mode") ? col.getString("mode") : "none";
            float       radius = col.has("radius") ? col.getFloat("radius", 0.f) : emitter->config()->collisionRadius;
            float       restitution =
                col.has("restitution") ? col.getFloat("restitution", 0.6f) : emitter->config()->collisionRestitution;
            float loss =
                col.has("lifetimeLoss") ? col.getFloat("lifetimeLoss", 0.f) : emitter->config()->collisionLifetimeLoss;
            emitter->setCollision(mode, radius, restitution, loss);
        }
    }
    if (obj.has("collisionBounds")) {
        const Json cb = obj.get("collisionBounds");
        if (cb.isObject()) {
            bool enabled = cb.has("enabled") ? cb.getBool("enabled", false) : emitter->config()->collisionBoundsEnabled;
            float minX   = cb.has("minX") ? cb.getFloat("minX", 0.f) : emitter->config()->boundsMinX;
            float minY   = cb.has("minY") ? cb.getFloat("minY", 0.f) : emitter->config()->boundsMinY;
            float maxX   = cb.has("maxX") ? cb.getFloat("maxX", 0.f) : emitter->config()->boundsMaxX;
            float maxY   = cb.has("maxY") ? cb.getFloat("maxY", 0.f) : emitter->config()->boundsMaxY;
            emitter->setCollisionBounds(enabled, minX, minY, maxX, maxY);
        }
    }
    if (obj.has("worldCollision")) emitter->setWorldCollision(obj.getBool("worldCollision", false));
    if (obj.has("motionVectorPolicy")) emitter->setMotionVectorPolicy(obj.getString("motionVectorPolicy"));

    if (obj.has("renderMode")) {
        float stretch = obj.has("stretch") ? obj.getFloat("stretch", 1.f) : emitter->config()->stretchFactor;
        emitter->setRenderMode(obj.getString("renderMode"), stretch);
    } else if (obj.has("stretch")) {
        emitter->setRenderMode("stretched", obj.getFloat("stretch", 1.f));
    }
    if (obj.has("renderAxis")) emitter->setRenderAxis(obj.getFloat("renderAxis", 0.f));
    if (obj.has("ribbon")) {
        const Json ribbon = obj.get("ribbon");
        if (ribbon.isObject()) {
            const float width      = ribbon.has("width") ? ribbon.getFloat("width", 1.f) : 1.f;
            const float minSegment = ribbon.has("minSegmentLength") ? ribbon.getFloat("minSegmentLength", 1.f) : 1.f;
            emitter->setRibbon(width, minSegment);
        }
    }
    if (obj.has("sortMode")) emitter->setSortMode(obj.getString("sortMode"));
    if (obj.has("materialMode")) emitter->setMaterialMode(obj.getString("materialMode"));
    if (obj.has("material")) {
        const Json material = obj.get("material");
        if (material.isObject()) {
            if (material.has("mode")) emitter->setMaterialMode(material.getString("mode"));
            if (material.has("distortionStrength"))
                emitter->setDistortionStrength(material.getFloat("distortionStrength", 8.f));
            if (material.has("normalTexture")) {
                const std::string path                 = material.getString("normalTexture");
                emitter->resource()->normalTexturePath = path;
                tryLoadTexture(emitter, path, true);
            }
        }
    }
    if (obj.has("parameters")) {
        const Json parameters = obj.get("parameters");
        if (parameters.isObject()) {
            for (const auto &key : parameters.keys())
                emitter->setFloatParameter(key, parameters.getFloat(key.c_str(), 0.f));
        }
    }
    if (obj.has("parameterBindings")) {
        const Json bindings = obj.get("parameterBindings");
        if (bindings.isArray()) {
            emitter->clearFloatParameterBindings();
            for (size_t i = 0; i < bindings.size(); ++i) {
                const Json binding = bindings.at(i);
                if (!binding.isObject()) continue;
                emitter->bindFloatParameter(binding.getString("parameter"), binding.getString("target"),
                                            binding.has("scale") ? binding.getFloat("scale", 1.f) : 1.f,
                                            binding.has("offset") ? binding.getFloat("offset", 0.f) : 0.f);
            }
        }
    }
    if (obj.has("softParticles")) {
        const Json soft = obj.get("softParticles");
        if (soft.isObject()) {
            const bool  enabled = soft.has("enabled") ? soft.getBool("enabled", true) : true;
            const float depth   = soft.has("depth") ? soft.getFloat("depth", 0.5f) : 0.5f;
            const float fade    = soft.has("fadeDistance") ? soft.getFloat("fadeDistance", 0.05f) : 0.05f;
            emitter->setSoftParticles(enabled, depth, fade);
        }
    }
    if (obj.has("overflowMode")) emitter->setOverflowMode(obj.getString("overflowMode"));
    if (obj.has("maxDeltaTime")) emitter->setMaxDeltaTime(obj.getFloat("maxDeltaTime", 0.f));
    if (obj.has("gpuSimulation")) emitter->setGpuSimulation(obj.getBool("gpuSimulation", false));
    if (obj.has("priority")) emitter->setPriority(obj.getInt("priority", 0));
    if (obj.has("minimumQuality")) emitter->setMinimumQuality(obj.getInt("minimumQuality", 0));
    if (obj.has("cullingMode")) emitter->setCullingMode(obj.getString("cullingMode"));
    if (obj.has("cullDistance")) emitter->setCullDistance(obj.getFloat("cullDistance", 0.f));
    if (obj.has("maxSpawnPerFrame")) emitter->setMaxSpawnPerFrame(obj.getInt("maxSpawnPerFrame", 0));

    if (obj.has("forceFields")) {
        const Json arr = obj.get("forceFields");
        if (arr.isArray()) {
            emitter->clearForceFields();
            for (size_t i = 0; i < arr.size(); ++i) {
                const Json f = arr.at(i);
                if (!f.isObject()) continue;
                float x        = f.getFloat("x", 0.f);
                float y        = f.getFloat("y", 0.f);
                float radius   = f.getFloat("radius", 0.f);
                float strength = f.getFloat("strength", 0.f);
                float falloff  = f.has("falloff") ? f.getFloat("falloff", 1.f) : 1.f;
                emitter->addForceField(x, y, radius, strength, falloff);
            }
        }
    }

    if (obj.has("lights")) {
        const Json lg = obj.get("lights");
        if (lg.isObject()) {
            bool  enabled   = lg.has("enabled") ? lg.getBool("enabled", false) : emitter->config()->lights.enabled;
            float radius    = lg.has("radius") ? lg.getFloat("radius", 120.f) : emitter->config()->lights.radius;
            float intensity = lg.has("intensity") ? lg.getFloat("intensity", 1.f) : emitter->config()->lights.intensity;
            float lr        = emitter->config()->lights.r;
            float lg2       = emitter->config()->lights.g;
            float lb        = emitter->config()->lights.b;
            readVec3(lg, "color", lr, lg2, lb);
            if (lg.has("r")) lr = lg.getFloat("r", lr);
            if (lg.has("g")) lg2 = lg.getFloat("g", lg2);
            if (lg.has("b")) lb = lg.getFloat("b", lb);
            int maxL = lg.has("max") ? lg.getInt("max", 4) : emitter->config()->lights.max;
            emitter->setLights(enabled, radius, intensity, lr, lg2, lb, maxL);
        }
    }

    float r = 1, g = 1, b = 1, a = 1;
    if (readVec4(obj, "colorStart", r, g, b, a)) emitter->setColorStart(r, g, b, a);
    if (readVec4(obj, "colorEnd", r, g, b, a)) emitter->setColorEnd(r, g, b, a);

    if (obj.has("colorOverLife")) {
        const Json arr = obj.get("colorOverLife");
        if (arr.isArray()) {
            emitter->clearColorGradient();
            readGradientArray(arr, emitter->config()->colorGradient);
        }
    }

    if (obj.has("sizeOverLife")) {
        const Json arr = obj.get("sizeOverLife");
        if (arr.isArray()) {
            emitter->clearSizeCurve();
            readCurveArray(arr, emitter->config()->sizeCurve);
        }
    }

    if (obj.has("rotationOverLife")) {
        const Json arr = obj.get("rotationOverLife");
        if (arr.isArray()) {
            emitter->clearRotationCurve();
            readCurveArray(arr, emitter->config()->rotationCurve);
        }
    }

    if (obj.has("blendMode")) emitter->setBlendMode(obj.getString("blendMode"));

    if (obj.has("flipbook")) {
        const Json fb = obj.get("flipbook");
        if (fb.isObject()) {
            auto  c    = emitter->config();
            int   h    = fb.has("hframes") ? fb.getInt("hframes", 1) : c->hframes;
            int   v    = fb.has("vframes") ? fb.getInt("vframes", 1) : c->vframes;
            float rate = fb.has("frameRate") ? fb.getFloat("frameRate", 0.f) : c->frameRate;
            float rs   = fb.has("frameRandomStart") ? fb.getFloat("frameRandomStart", 0.f) : c->frameRandomStart;
            emitter->setFlipbook(h, v, rate, rs);
        }
    } else if (obj.has("hframes") || obj.has("vframes") || obj.has("frameRate") || obj.has("frameRandomStart")) {
        auto  c    = emitter->config();
        int   h    = obj.has("hframes") ? obj.getInt("hframes", 1) : c->hframes;
        int   v    = obj.has("vframes") ? obj.getInt("vframes", 1) : c->vframes;
        float rate = obj.has("frameRate") ? obj.getFloat("frameRate", 0.f) : c->frameRate;
        float rs   = obj.has("frameRandomStart") ? obj.getFloat("frameRandomStart", 0.f) : c->frameRandomStart;
        emitter->setFlipbook(h, v, rate, rs);
    }

    if (obj.has("layer")) emitter->setLayer(obj.getInt("layer", emitter->getLayer()));
    if (obj.has("visible")) emitter->setVisible(obj.getBool("visible", emitter->isVisible()));

    if (obj.has("texture")) {
        std::string texPath              = obj.getString("texture");
        emitter->resource()->texturePath = texPath;
        tryLoadTexture(emitter, texPath);
    }

    if (obj.has("autoReload")) emitter->resource()->autoReload = obj.getBool("autoReload", true);

    if (obj.has("autoStart") && obj.getBool("autoStart", false)) emitter->start();

    return eve::Result<void>::success();
}

bool applyConfigText(ParticleEmitter *emitter, const std::string &json, std::string *error) {
    std::string err;
    auto        doc = eve::json::Document::parse(json, &err);
    if (!doc.valid()) {
        if (error) *error = err.empty() ? "invalid json" : err;
        return false;
    }
    const Json root = doc.root();
    if (!root.isObject()) {
        if (error) *error = "config root must be object";
        return false;
    }
    auto applied = applyConfigDocument(emitter, root);
    if (!applied) {
        if (error) {
            const auto* diag = applied.error();
            *error           = (diag && !diag->message().empty()) ? diag->message() : "config root must be object";
        }
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

    auto res     = emitter->resource();
    res->path    = path;
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
