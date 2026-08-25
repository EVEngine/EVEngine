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

bool readVec3(Poco::JSON::Object::Ptr o, const char *key, float &a, float &b, float &c) {
    if (!o || !o->has(key)) return false;
    Poco::JSON::Array::Ptr arr;
    try {
        arr = o->getArray(key);
    } catch (...) {
        return false;
    }
    if (!arr || arr->size() < 3) return false;
    a = asFloat(arr->get(0), a);
    b = asFloat(arr->get(1), b);
    c = asFloat(arr->get(2), c);
    return true;
}

bool readCurveArray(Poco::JSON::Array::Ptr arr, ParticleCurve &curve) {
    if (!arr) return false;
    bool any = false;
    for (size_t i = 0; i < arr->size(); ++i) {
        try {
            if (arr->isObject(int(i))) {
                auto obj = arr->getObject(int(i));
                if (obj) {
                    curve.add(asFloat(obj->get("t"), 0.f), asFloat(obj->get("v"), 0.f));
                    any = true;
                }
            } else if (arr->isArray(int(i))) {
                auto sub = arr->getArray(int(i));
                if (sub && sub->size() >= 2) {
                    curve.add(asFloat(sub->get(0), 0.f), asFloat(sub->get(1), 0.f));
                    any = true;
                }
            }
        } catch (...) {
        }
    }
    return any;
}

bool readGradientArray(Poco::JSON::Array::Ptr arr, ParticleGradient &gradient) {
    if (!arr) return false;
    bool any = false;
    for (size_t i = 0; i < arr->size(); ++i) {
        float t = 0.f, r = 1.f, g = 1.f, b = 1.f, a = 1.f;
        try {
            if (arr->isObject(int(i))) {
                auto obj = arr->getObject(int(i));
                if (obj) {
                    t = asFloat(obj->get("t"), t);
                    r = asFloat(obj->get("r"), r);
                    g = asFloat(obj->get("g"), g);
                    b = asFloat(obj->get("b"), b);
                    a = obj->has("a") ? asFloat(obj->get("a"), a) : a;
                    gradient.add(t, r, g, b, a);
                    any = true;
                }
            } else if (arr->isArray(int(i))) {
                auto sub = arr->getArray(int(i));
                if (sub && sub->size() >= 5) {
                    t = asFloat(sub->get(0), t);
                    r = asFloat(sub->get(1), r);
                    g = asFloat(sub->get(2), g);
                    b = asFloat(sub->get(3), b);
                    a = asFloat(sub->get(4), a);
                    gradient.add(t, r, g, b, a);
                    any = true;
                }
            }
        } catch (...) {
        }
    }
    return any;
}

void tryLoadTexture(ParticleEmitter* emitter, const std::string& path, bool normalMap = false) {
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
    if (obj->has("emissionRateOverDistance"))
        emitter->setEmissionRateOverDistance(
            asFloat(obj->get("emissionRateOverDistance"), emitter->getEmissionRateOverDistance()));

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
    if (obj->has("looping"))
        emitter->setLooping(asBool(obj->get("looping"), emitter->getLooping()));
    if (obj->has("playbackSpeed"))
        emitter->setPlaybackSpeed(asFloat(obj->get("playbackSpeed"), emitter->getPlaybackSpeed()));
    if (obj->has("fixedTimeStep")) {
        const float step = asFloat(obj->get("fixedTimeStep"), emitter->getFixedTimeStep());
        const int maxSteps = obj->has("maxSubSteps")
                                 ? int(asFloat(obj->get("maxSubSteps"), 8.f))
                                 : emitter->config()->maxSubSteps;
        emitter->setFixedTimeStep(step, maxSteps);
    }
    if (obj->has("randomSeed"))
        emitter->setRandomSeed(int(asFloat(obj->get("randomSeed"), 0.f)));
    if (obj->has("autoRandomSeed"))
        emitter->setAutoRandomSeed(asBool(obj->get("autoRandomSeed"), true));

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

    float sr0 = 0, sr1 = 0;
    if (readVec2(obj, "startRotation", sr0, sr1))
        emitter->setStartRotation(sr0, sr1);

    if (obj->has("bursts")) {
        try {
            auto arr = obj->getArray("bursts");
            if (arr) {
                emitter->clearBursts();
                for (size_t i = 0; i < arr->size(); ++i) {
                    float t = 0.f;
                    int count = 0;
                    if (arr->isObject(int(i))) {
                        auto b = arr->getObject(int(i));
                        if (!b) continue;
                        t = asFloat(b->get("time"), 0.f);
                        count = int(asFloat(b->get("count"), 0.f));
                    } else if (arr->isArray(int(i))) {
                        auto sub = arr->getArray(int(i));
                        if (!sub || sub->size() < 2) continue;
                        t = asFloat(sub->get(0), 0.f);
                        count = int(asFloat(sub->get(1), 0.f));
                    }
                    if (count > 0) emitter->addBurst(t, count);
                }
            }
        } catch (...) {
        }
    }

    if (obj->has("prewarm"))
        emitter->setPrewarm(asFloat(obj->get("prewarm"), 0.f));

    float gx = 0, gy = 0;
    if (readVec2(obj, "gravity", gx, gy)) emitter->setGravity(gx, gy);
    if (obj->has("damping"))
        emitter->setDamping(asFloat(obj->get("damping"), 0.f));
    if (obj->has("limitVelocity"))
        emitter->setLimitVelocity(asFloat(obj->get("limitVelocity"), 0.f));
    if (obj->has("velocityOverLifetime")) {
        try {
            auto arr = obj->getArray("velocityOverLifetime");
            if (arr) {
                emitter->clearVelocityCurve();
                readCurveArray(arr, emitter->config()->velocityCurve);
            }
        } catch (...) {
        }
    }
    if (obj->has("inheritVelocity"))
        emitter->setInheritVelocity(asFloat(obj->get("inheritVelocity"), 0.f));
    if (obj->has("simulationSpace"))
        emitter->setSimulationSpace(asString(obj->get("simulationSpace")));

    if (obj->has("noise")) {
        try {
            auto n = obj->getObject("noise");
            if (n) {
                float strength = n->has("strength") ? asFloat(n->get("strength"), 0.f)
                                                    : emitter->config()->noiseStrength;
                float freq = n->has("frequency") ? asFloat(n->get("frequency"), 1.f)
                                                 : emitter->config()->noiseFrequency;
                float speed = n->has("speed") ? asFloat(n->get("speed"), 1.f)
                                              : emitter->config()->noiseSpeed;
                emitter->setNoise(strength, freq, speed);
            }
        } catch (...) {
        }
    } else if (obj->has("noiseStrength")) {
        emitter->setNoise(asFloat(obj->get("noiseStrength"), 0.f),
                          obj->has("noiseFrequency") ? asFloat(obj->get("noiseFrequency"), 1.f)
                                                     : emitter->config()->noiseFrequency,
                          obj->has("noiseSpeed") ? asFloat(obj->get("noiseSpeed"), 1.f)
                                                 : emitter->config()->noiseSpeed);
    }

    if (obj->has("collision")) {
        try {
            auto col = obj->getObject("collision");
            if (col) {
                std::string mode = col->has("mode") ? asString(col->get("mode")) : "none";
                float radius = col->has("radius") ? asFloat(col->get("radius"), 0.f)
                                                  : emitter->config()->collisionRadius;
                float restitution =
                    col->has("restitution") ? asFloat(col->get("restitution"), 0.6f)
                                            : emitter->config()->collisionRestitution;
                float loss = col->has("lifetimeLoss")
                                 ? asFloat(col->get("lifetimeLoss"), 0.f)
                                 : emitter->config()->collisionLifetimeLoss;
                emitter->setCollision(mode, radius, restitution, loss);
            }
        } catch (...) {
        }
    }
    if (obj->has("collisionBounds")) {
        try {
            auto cb = obj->getObject("collisionBounds");
            if (cb) {
                bool enabled = cb->has("enabled") ? asBool(cb->get("enabled"), false)
                                                  : emitter->config()->collisionBoundsEnabled;
                float minX = cb->has("minX") ? asFloat(cb->get("minX"), 0.f)
                                             : emitter->config()->boundsMinX;
                float minY = cb->has("minY") ? asFloat(cb->get("minY"), 0.f)
                                             : emitter->config()->boundsMinY;
                float maxX = cb->has("maxX") ? asFloat(cb->get("maxX"), 0.f)
                                             : emitter->config()->boundsMaxX;
                float maxY = cb->has("maxY") ? asFloat(cb->get("maxY"), 0.f)
                                             : emitter->config()->boundsMaxY;
                emitter->setCollisionBounds(enabled, minX, minY, maxX, maxY);
            }
        } catch (...) {
        }
    }
    if (obj->has("worldCollision"))
        emitter->setWorldCollision(asBool(obj->get("worldCollision"), false));

    if (obj->has("renderMode")) {
        float stretch = obj->has("stretch") ? asFloat(obj->get("stretch"), 1.f)
                                            : emitter->config()->stretchFactor;
        emitter->setRenderMode(asString(obj->get("renderMode")), stretch);
    } else if (obj->has("stretch")) {
        emitter->setRenderMode("stretched", asFloat(obj->get("stretch"), 1.f));
    }
    if (obj->has("renderAxis")) emitter->setRenderAxis(asFloat(obj->get("renderAxis"), 0.f));
    if (obj->has("ribbon")) {
        try {
            auto ribbon = obj->getObject("ribbon");
            if (ribbon) {
                const float width = ribbon->has("width") ? asFloat(ribbon->get("width"), 1.f) : 1.f;
                const float minSegment =
                    ribbon->has("minSegmentLength") ? asFloat(ribbon->get("minSegmentLength"), 1.f) : 1.f;
                emitter->setRibbon(width, minSegment);
            }
        } catch (...) {
        }
    }
    if (obj->has("sortMode")) emitter->setSortMode(asString(obj->get("sortMode")));
    if (obj->has("materialMode")) emitter->setMaterialMode(asString(obj->get("materialMode")));
    if (obj->has("material")) {
        try {
            auto material = obj->getObject("material");
            if (material) {
                if (material->has("mode")) emitter->setMaterialMode(asString(material->get("mode")));
                if (material->has("normalTexture")) {
                    const std::string path                 = asString(material->get("normalTexture"));
                    emitter->resource()->normalTexturePath = path;
                    tryLoadTexture(emitter, path, true);
                }
            }
        } catch (...) {
        }
    }
    if (obj->has("parameters")) {
        try {
            auto parameters = obj->getObject("parameters");
            if (parameters) {
                for (const auto& entry : *parameters)
                    emitter->setFloatParameter(entry.first, asFloat(entry.second, 0.f));
            }
        } catch (...) {
        }
    }
    if (obj->has("parameterBindings")) {
        try {
            auto bindings = obj->getArray("parameterBindings");
            if (bindings) {
                emitter->clearFloatParameterBindings();
                for (size_t i = 0; i < bindings->size(); ++i) {
                    if (!bindings->isObject(int(i))) continue;
                    auto binding = bindings->getObject(int(i));
                    if (!binding) continue;
                    emitter->bindFloatParameter(asString(binding->get("parameter")), asString(binding->get("target")),
                                                binding->has("scale") ? asFloat(binding->get("scale"), 1.f) : 1.f,
                                                binding->has("offset") ? asFloat(binding->get("offset"), 0.f) : 0.f);
                }
            }
        } catch (...) {
        }
    }
    if (obj->has("softParticles")) {
        try {
            auto soft = obj->getObject("softParticles");
            if (soft) {
                const bool  enabled = soft->has("enabled") ? asBool(soft->get("enabled"), true) : true;
                const float depth   = soft->has("depth") ? asFloat(soft->get("depth"), 0.5f) : 0.5f;
                const float fade    = soft->has("fadeDistance") ? asFloat(soft->get("fadeDistance"), 0.05f) : 0.05f;
                emitter->setSoftParticles(enabled, depth, fade);
            }
        } catch (...) {
        }
    }
    if (obj->has("overflowMode"))
        emitter->setOverflowMode(asString(obj->get("overflowMode")));
    if (obj->has("maxDeltaTime"))
        emitter->setMaxDeltaTime(asFloat(obj->get("maxDeltaTime"), 0.f));
    if (obj->has("gpuSimulation"))
        emitter->setGpuSimulation(asBool(obj->get("gpuSimulation"), false));
    if (obj->has("priority"))
        emitter->setPriority(int(asFloat(obj->get("priority"), 0.f)));
    if (obj->has("minimumQuality"))
        emitter->setMinimumQuality(int(asFloat(obj->get("minimumQuality"), 0.f)));
    if (obj->has("cullingMode")) emitter->setCullingMode(asString(obj->get("cullingMode")));
    if (obj->has("cullDistance"))
        emitter->setCullDistance(asFloat(obj->get("cullDistance"), 0.f));
    if (obj->has("maxSpawnPerFrame"))
        emitter->setMaxSpawnPerFrame(int(asFloat(obj->get("maxSpawnPerFrame"), 0.f)));

    if (obj->has("forceFields")) {
        try {
            auto arr = obj->getArray("forceFields");
            if (arr) {
                emitter->clearForceFields();
                for (size_t i = 0; i < arr->size(); ++i) {
                    if (!arr->isObject(int(i))) continue;
                    auto f = arr->getObject(int(i));
                    if (!f) continue;
                    float x = asFloat(f->get("x"), 0.f);
                    float y = asFloat(f->get("y"), 0.f);
                    float radius = asFloat(f->get("radius"), 0.f);
                    float strength = asFloat(f->get("strength"), 0.f);
                    float falloff = f->has("falloff") ? asFloat(f->get("falloff"), 1.f) : 1.f;
                    emitter->addForceField(x, y, radius, strength, falloff);
                }
            }
        } catch (...) {
        }
    }

    if (obj->has("lights")) {
        try {
            auto lg = obj->getObject("lights");
            if (lg) {
                bool enabled = lg->has("enabled") ? asBool(lg->get("enabled"), false)
                                                  : emitter->config()->lights.enabled;
                float radius = lg->has("radius") ? asFloat(lg->get("radius"), 120.f)
                                                 : emitter->config()->lights.radius;
                float intensity = lg->has("intensity") ? asFloat(lg->get("intensity"), 1.f)
                                                       : emitter->config()->lights.intensity;
                float lr = emitter->config()->lights.r;
                float lg2 = emitter->config()->lights.g;
                float lb = emitter->config()->lights.b;
                if (readVec3(lg, "color", lr, lg2, lb)) {
                }
                if (lg->has("r")) lr = asFloat(lg->get("r"), lr);
                if (lg->has("g")) lg2 = asFloat(lg->get("g"), lg2);
                if (lg->has("b")) lb = asFloat(lg->get("b"), lb);
                int maxL = lg->has("max") ? int(asFloat(lg->get("max"), 4.f))
                                          : emitter->config()->lights.max;
                emitter->setLights(enabled, radius, intensity, lr, lg2, lb, maxL);
            }
        } catch (...) {
        }
    }

    float r = 1, g = 1, b = 1, a = 1;
    if (readVec4(obj, "colorStart", r, g, b, a))
        emitter->setColorStart(r, g, b, a);
    if (readVec4(obj, "colorEnd", r, g, b, a))
        emitter->setColorEnd(r, g, b, a);

    if (obj->has("colorOverLifetime")) {
        try {
            auto arr = obj->getArray("colorOverLifetime");
            if (arr) {
                emitter->clearColorGradient();
                readGradientArray(arr, emitter->config()->colorGradient);
            }
        } catch (...) {
        }
    }

    if (obj->has("sizeOverLifetime")) {
        try {
            auto arr = obj->getArray("sizeOverLifetime");
            if (arr) {
                emitter->clearSizeCurve();
                readCurveArray(arr, emitter->config()->sizeCurve);
            }
        } catch (...) {
        }
    }

    if (obj->has("rotationOverLifetime")) {
        try {
            auto arr = obj->getArray("rotationOverLifetime");
            if (arr) {
                emitter->clearRotationCurve();
                readCurveArray(arr, emitter->config()->rotationCurve);
            }
        } catch (...) {
        }
    }

    if (obj->has("blendMode"))
        emitter->setBlendMode(asString(obj->get("blendMode")));

    if (obj->has("flipbook")) {
        try {
            auto fb = obj->getObject("flipbook");
            if (fb) {
                auto c = emitter->config();
                int h = fb->has("hframes") ? int(asFloat(fb->get("hframes"), 1.f)) : c->hframes;
                int v = fb->has("vframes") ? int(asFloat(fb->get("vframes"), 1.f)) : c->vframes;
                float rate = fb->has("frameRate") ? asFloat(fb->get("frameRate"), 0.f)
                                                  : c->frameRate;
                float rs = fb->has("frameRandomStart")
                               ? asFloat(fb->get("frameRandomStart"), 0.f)
                               : c->frameRandomStart;
                emitter->setFlipbook(h, v, rate, rs);
            }
        } catch (...) {
        }
    } else if (obj->has("hframes") || obj->has("vframes") || obj->has("frameRate") ||
               obj->has("frameRandomStart")) {
        auto c = emitter->config();
        int h = obj->has("hframes") ? int(asFloat(obj->get("hframes"), 1.f)) : c->hframes;
        int v = obj->has("vframes") ? int(asFloat(obj->get("vframes"), 1.f)) : c->vframes;
        float rate = obj->has("frameRate") ? asFloat(obj->get("frameRate"), 0.f) : c->frameRate;
        float rs = obj->has("frameRandomStart") ? asFloat(obj->get("frameRandomStart"), 0.f)
                                                : c->frameRandomStart;
        emitter->setFlipbook(h, v, rate, rs);
    }

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
