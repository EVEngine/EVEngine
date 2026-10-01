#include "particles/ParticleEffect.h"

#include "common/Module.h"
#include "data/DataModule.h"
#include "data/JsonDocument.h"
#include "filesystem/FileData.h"
#include "filesystem/Filesystem.h"
#include "particles/ParticleConfig.h"
#include "particles/ParticleEmitter.h"

#include <Poco/Dynamic/Var.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Stringifier.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <sstream>
#include <unordered_set>

namespace eve::particles {
namespace {

std::vector<ParticleEffect*>& timelineRegistry() {
    static std::vector<ParticleEffect*> registry;
    return registry;
}

float asFloat(const Poco::Dynamic::Var& value, float fallback) {
    try {
        return value.convert<float>();
    } catch (...) {
        return fallback;
    }
}

int asInt(const Poco::Dynamic::Var& value, int fallback) {
    try {
        return value.convert<int>();
    } catch (...) {
        return fallback;
    }
}

bool asBool(const Poco::Dynamic::Var& value, bool fallback) {
    try {
        return value.convert<bool>();
    } catch (...) {
        return fallback;
    }
}

std::string asString(const Poco::Dynamic::Var& value) {
    try {
        return value.convert<std::string>();
    } catch (...) {
        return {};
    }
}

std::string objectJson(const Poco::JSON::Object::Ptr& object) {
    std::ostringstream out;
    Poco::JSON::Stringifier::stringify(Poco::Dynamic::Var(object), out, 0, 0);
    return out.str();
}

int objectBuffer(const Poco::JSON::Object::Ptr& object, int fallback) {
    if (!object || !object->has("buffer")) return fallback;
    return std::max(1, asInt(object->get("buffer"), fallback));
}

void readOffset(const Poco::JSON::Object::Ptr& object, float& x, float& y) {
    if (!object) return;
    if (object->has("offset")) {
        try {
            auto offset = object->getArray("offset");
            if (offset && offset->size() >= 2) {
                x = asFloat(offset->get(0), x);
                y = asFloat(offset->get(1), y);
            }
        } catch (...) {
        }
    }
    if (object->has("x")) x = asFloat(object->get("x"), x);
    if (object->has("y")) y = asFloat(object->get("y"), y);
}

std::string resolveAssetPath(const std::string& sourcePath, const std::string& referencedPath) {
    if (sourcePath.empty() || referencedPath.empty()) return referencedPath;
    const std::filesystem::path reference(referencedPath);
    if (reference.is_absolute()) return reference.generic_string();
    return (std::filesystem::path(sourcePath).parent_path() / reference).lexically_normal().generic_string();
}

bool applyParameters(ParticleEmitter* emitter, const Poco::JSON::Object::Ptr& parameters) {
    if (!emitter || !parameters) return false;
    for (const auto& parameter : *parameters)
        emitter->setFloatParameter(parameter.first, asFloat(parameter.second, 0.f));
    return true;
}

bool validTrigger(const std::string& trigger) {
    return trigger == "birth" || trigger == "death" || trigger == "collision";
}

bool validCueAction(const std::string& action) {
    return action == "start" || action == "stop" || action == "pause" || action == "reset" ||
           action == "emit" || action == "setParameter";
}

Result<void> parseEventRoutes(const Poco::JSON::Object::Ptr& root,
                              std::vector<ParticleEffectEventRoute>& out) {
    out.clear();
    if (!root || !root->has("eventRoutes")) return Result<void>::success();
    Poco::JSON::Array::Ptr routes;
    try {
        routes = root->getArray("eventRoutes");
    } catch (...) {
    }
    if (!routes)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "effect eventRoutes must be an array", "eventRoutes"));
    for (std::size_t i = 0; i < routes->size(); ++i) {
        Poco::JSON::Object::Ptr routeObject;
        try {
            routeObject = routes->getObject(static_cast<int>(i));
        } catch (...) {
        }
        if (!routeObject)
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "event route must be an object", "eventRoutes"));
        ParticleEffectEventRoute route;
        route.from             = routeObject->has("from") ? asString(routeObject->get("from")) : std::string();
        route.to               = routeObject->has("to") ? asString(routeObject->get("to")) : std::string();
        route.on               = routeObject->has("on") ? asString(routeObject->get("on")) : std::string("death");
        route.inheritVelocity  = routeObject->has("inheritVelocity")
                                     ? asFloat(routeObject->get("inheritVelocity"), 0.f)
                                     : 0.f;
        if (route.from.empty() || route.to.empty())
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "event route requires from and to emitter names",
                "eventRoutes"));
        if (!validTrigger(route.on))
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "event route on must be birth, death, or collision",
                "eventRoutes.on"));
        if (route.from == route.to)
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "event route cannot target its own emitter",
                "eventRoutes"));
        out.push_back(std::move(route));
    }
    return Result<void>::success();
}

Result<void> parseTimeline(const Poco::JSON::Object::Ptr& root, ParticleEffectTimeline& out) {
    out = {};
    if (!root || !root->has("timeline")) return Result<void>::success();
    Poco::JSON::Object::Ptr timelineObject;
    try {
        timelineObject = root->getObject("timeline");
    } catch (...) {
    }
    if (!timelineObject)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "effect timeline must be an object", "timeline"));
    out.duration = timelineObject->has("duration") ? asFloat(timelineObject->get("duration"), 0.f) : 0.f;
    out.looping  = timelineObject->has("looping") ? asBool(timelineObject->get("looping"), false) : false;
    if (!std::isfinite(out.duration) || out.duration < 0.f)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "timeline.duration must be a finite non-negative number",
            "timeline.duration"));
    if (!timelineObject->has("cues")) return Result<void>::success();
    Poco::JSON::Array::Ptr cues;
    try {
        cues = timelineObject->getArray("cues");
    } catch (...) {
    }
    if (!cues)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "timeline.cues must be an array", "timeline.cues"));
    for (std::size_t i = 0; i < cues->size(); ++i) {
        Poco::JSON::Object::Ptr cueObject;
        try {
            cueObject = cues->getObject(static_cast<int>(i));
        } catch (...) {
        }
        if (!cueObject)
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "timeline cue must be an object", "timeline.cues"));
        ParticleEffectTimelineCue cue;
        cue.time      = cueObject->has("time") ? asFloat(cueObject->get("time"), 0.f) : 0.f;
        cue.action    = cueObject->has("action") ? asString(cueObject->get("action")) : std::string();
        cue.emitter   = cueObject->has("emitter") ? asString(cueObject->get("emitter")) : std::string();
        cue.parameter = cueObject->has("parameter") ? asString(cueObject->get("parameter")) : std::string();
        cue.value     = cueObject->has("value") ? asFloat(cueObject->get("value"), 0.f) : 0.f;
        cue.count     = cueObject->has("count") ? asInt(cueObject->get("count"), 0) : 0;
        if (!std::isfinite(cue.time) || cue.time < 0.f)
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "timeline cue time must be finite and non-negative",
                "timeline.cues.time"));
        if (!validCueAction(cue.action))
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument,
                "timeline cue action must be start, stop, pause, reset, emit, or setParameter",
                "timeline.cues.action"));
        if (cue.action == "emit" && cue.count <= 0)
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "timeline emit cue requires a positive count",
                "timeline.cues.count"));
        if (cue.action == "setParameter" && cue.parameter.empty())
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "timeline setParameter cue requires a parameter name",
                "timeline.cues.parameter"));
        if ((cue.action == "start" || cue.action == "stop" || cue.action == "pause" ||
             cue.action == "reset" || cue.action == "emit") &&
            cue.emitter.empty())
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "timeline emitter cue requires an emitter name",
                "timeline.cues.emitter"));
        out.cues.push_back(std::move(cue));
    }
    std::stable_sort(out.cues.begin(), out.cues.end(),
                     [](const ParticleEffectTimelineCue& a, const ParticleEffectTimelineCue& b) {
                         return a.time < b.time;
                     });
    return Result<void>::success();
}

}  // namespace

Result<ParticleEffect*> ParticleEffect::parse(const std::string& json, const std::string& sourcePath) {
    auto*                               dataModule = eve::data::DataModule::create();
    std::string                         parseError;
    std::unique_ptr<data::JsonDocument> document(dataModule->decodeJson(json, &parseError));
    if (!document || !document->isObject())
        return Result<ParticleEffect*>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument,
            parseError.empty() ? "effect root must be an object" : parseError, "json"));

    auto root = document->object();
    if (!root || !root->has("type") || asString(root->get("type")) != "eve.particle-effect")
        return Result<ParticleEffect*>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "effect type must be eve.particle-effect", "type"));
    const int version = root->has("version") ? asInt(root->get("version"), 0) : 0;
    if (version < ParticleEffect::kMinSupportedVersion || version > ParticleEffect::kMaxSupportedVersion)
        return Result<ParticleEffect*>::failure(Diagnostic::error(
            DiagnosticCode::Unsupported,
            "unsupported particle effect version: " + std::to_string(version), "version"));

    Poco::JSON::Array::Ptr emitterArray;
    try {
        emitterArray = root->getArray("emitters");
    } catch (...) {
    }
    if (!emitterArray || emitterArray->size() == 0)
        return Result<ParticleEffect*>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "effect emitters must be a non-empty array", "emitters"));

    auto effect         = std::unique_ptr<ParticleEffect>(new ParticleEffect());
    effect->version_    = version;
    effect->sourcePath_ = sourcePath;

    Poco::JSON::Object::Ptr globalParameters;
    if (root->has("parameters")) {
        try {
            globalParameters = root->getObject("parameters");
        } catch (...) {
        }
        if (!globalParameters)
            return Result<ParticleEffect*>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "effect parameters must be an object", "parameters"));
        for (const auto& parameter : *globalParameters)
            effect->parameters_[parameter.first] = asFloat(parameter.second, 0.f);
    }

    auto routes = parseEventRoutes(root, effect->eventRoutes_);
    if (!routes)
        return Result<ParticleEffect*>::failure(routes.status());
    auto timeline = parseTimeline(root, effect->timeline_);
    if (!timeline)
        return Result<ParticleEffect*>::failure(timeline.status());
    if (version >= 2 && effect->eventRoutes_.empty() && effect->timeline_.cues.empty() &&
        effect->timeline_.duration <= 0.f) {
        // v2 may omit both, but still records schema identity for migration tests.
    }

    std::unordered_set<std::string> names;
    for (std::size_t i = 0; i < emitterArray->size(); ++i) {
        Poco::JSON::Object::Ptr layerObject;
        try {
            layerObject = emitterArray->getObject(static_cast<int>(i));
        } catch (...) {
        }
        if (!layerObject)
            return Result<ParticleEffect*>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument,
                "emitter layer " + std::to_string(i) + " must be an object", "emitters"));

        const std::string name = layerObject->has("name") ? asString(layerObject->get("name")) : std::string();
        if (name.empty())
            return Result<ParticleEffect*>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument,
                "emitter layer " + std::to_string(i) + " requires a name", "emitters.name"));
        if (!names.insert(name).second)
            return Result<ParticleEffect*>::failure(Diagnostic::error(
                DiagnosticCode::Conflict, "duplicate emitter layer name: " + name, "emitters.name"));

        Poco::JSON::Object::Ptr embeddedConfig;
        if (layerObject->has("emitter")) {
            try {
                embeddedConfig = layerObject->getObject("emitter");
            } catch (...) {
            }
            if (!embeddedConfig)
                return Result<ParticleEffect*>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument,
                    "emitter layer " + name + " has an invalid emitter object", "emitters.emitter"));
        }
        const std::string configPath =
            layerObject->has("config") ? asString(layerObject->get("config")) : std::string();
        if (embeddedConfig && !configPath.empty())
            return Result<ParticleEffect*>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument,
                "emitter layer " + name + " cannot define both emitter and config", "emitters"));
        if (!embeddedConfig && configPath.empty())
            return Result<ParticleEffect*>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument,
                "emitter layer " + name + " requires emitter or config", "emitters"));

        int buffer          = layerObject->has("buffer") ? asInt(layerObject->get("buffer"), 1000) : 1000;
        buffer              = objectBuffer(embeddedConfig, buffer);
        auto*       emitter = ParticleEmitter::createEmitter(std::max(1, buffer));
        std::string configError;
        bool        configured = false;
        if (embeddedConfig) {
            configured = applyConfigText(emitter, objectJson(embeddedConfig), &configError);
        } else {
            configured = loadConfigFile(emitter, resolveAssetPath(sourcePath, configPath), &configError);
        }
        if (!configured) {
            emitter->release();
            return Result<ParticleEffect*>::failure(Diagnostic::error(
                DiagnosticCode::Failed, "emitter layer " + name + ": " + configError, "emitters"));
        }

        applyParameters(emitter, globalParameters);
        if (layerObject->has("parameters")) {
            Poco::JSON::Object::Ptr layerParameters;
            try {
                layerParameters = layerObject->getObject("parameters");
            } catch (...) {
            }
            if (!layerParameters) {
                emitter->release();
                return Result<ParticleEffect*>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument,
                    "emitter layer " + name + " parameters must be an object", "emitters.parameters"));
            }
            applyParameters(emitter, layerParameters);
        }

        Layer layer;
        layer.name    = name;
        layer.emitter = emitter;
        readOffset(layerObject, layer.offsetX, layer.offsetY);
        const float localRotation = layerObject->has("rotation") ? asFloat(layerObject->get("rotation"), 0.f) : 0.f;
        layer.baseDirection       = emitter->getDirection() + localRotation;
        layer.baseLayer           = emitter->getLayer();
        layer.enabled             = layerObject->has("enabled") ? asBool(layerObject->get("enabled"), true) : true;
        emitter->setVisible(layer.enabled);
        if (!layer.enabled) emitter->stop();
        effect->layers_.push_back(std::move(layer));
    }

    for (const auto& route : effect->eventRoutes_) {
        if (!effect->getEmitterByName(route.from) || !effect->getEmitterByName(route.to))
            return Result<ParticleEffect*>::failure(Diagnostic::error(
                DiagnosticCode::NotFound,
                "event route references unknown emitter: " + route.from + " -> " + route.to,
                "eventRoutes"));
    }
    for (const auto& cue : effect->timeline_.cues) {
        if (!cue.emitter.empty() && !effect->getEmitterByName(cue.emitter))
            return Result<ParticleEffect*>::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "timeline cue references unknown emitter: " + cue.emitter,
                "timeline.cues.emitter"));
    }

    effect->wireEventRoutes();
    effect->rewindTimelineClock();
    effect->syncTransform();
    effect->syncLayer();
    return Result<ParticleEffect*>::success(effect.release());
}

ParticleEffect::~ParticleEffect() {
    unregisterFromTimelineTicks();
    for (auto& layer : layers_) {
        if (layer.emitter) layer.emitter->release();
        layer.emitter = nullptr;
    }
}

ParticleEffect* ParticleEffect::fromText(const std::string& json, const std::string& sourcePath,
                                         std::string* error) {
    auto parsed = tryFromText(json, sourcePath);
    if (!parsed) {
        if (error) {
            if (parsed.error())
                *error = parsed.error()->message();
            else
                *error = "effect parse failed";
        }
        parsed.ignore();
        return nullptr;
    }
    if (error) error->clear();
    return parsed.value();
}

ParticleEffect* ParticleEffect::fromFile(const std::string& path, std::string* error) {
    auto parsed = tryFromFile(path);
    if (!parsed) {
        if (error) {
            if (parsed.error())
                *error = parsed.error()->message();
            else
                *error = "effect load failed";
        }
        parsed.ignore();
        return nullptr;
    }
    if (error) error->clear();
    return parsed.value();
}

Result<ParticleEffect*> ParticleEffect::tryFromText(const std::string& json,
                                                    const std::string& sourcePath) {
    return parse(json, sourcePath);
}

Result<ParticleEffect*> ParticleEffect::tryFromFile(const std::string& path) {
    if (path.empty())
        return Result<ParticleEffect*>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "empty effect path", "path"));
    auto* filesystem = eve::ModuleManager::getInstance<eve::filesystem::Filesystem>("Filesystem");
    if (!filesystem) filesystem = eve::filesystem::Filesystem::create();
    std::unique_ptr<eve::filesystem::FileData> data;
    try {
        data.reset(filesystem->read(path));
    } catch (...) {
        return Result<ParticleEffect*>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "read failed: " + path, "path"));
    }
    if (!data || data->getSize() == 0)
        return Result<ParticleEffect*>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "empty file: " + path, "path"));
    return tryFromText(std::string(static_cast<const char*>(data->getData()), data->getSize()), path);
}

int ParticleEffect::getEmitterCount() const { return static_cast<int>(layers_.size()); }

std::string ParticleEffect::getEmitterName(int index) const {
    if (index < 0 || index >= getEmitterCount()) return {};
    return layers_[static_cast<std::size_t>(index)].name;
}

ParticleEmitter* ParticleEffect::getEmitter(int index) const {
    if (index < 0 || index >= getEmitterCount()) return nullptr;
    return layers_[static_cast<std::size_t>(index)].emitter;
}

ParticleEmitter* ParticleEffect::getEmitterByName(const std::string& name) const {
    auto layer = std::find_if(layers_.begin(), layers_.end(), [&](const Layer& value) { return value.name == name; });
    return layer == layers_.end() ? nullptr : layer->emitter;
}

std::span<const ParticleEffectEventRoute> ParticleEffect::eventRoutes() const {
    return std::span<const ParticleEffectEventRoute>(eventRoutes_.data(), eventRoutes_.size());
}

Result<Duration> ParticleEffect::naturalDuration() const {
    double maximumNonLooping = 0.0;
    double maximumLoopPeriod = 0.0;
    bool   hasEnabledLayer    = false;
    for (const auto& layer : layers_) {
        if (!layer.enabled || !layer.emitter) continue;
        hasEnabledLayer            = true;
        const double emitterLife   = layer.emitter->getEmitterLifetime();
        const double particleLife  = layer.emitter->getParticleLifetimeMax();
        if (!std::isfinite(emitterLife) || !std::isfinite(particleLife) || particleLife < 0.0)
            return Result<Duration>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "Particle effect contains invalid lifetime data", "emitters"));
        if (layer.emitter->getLooping()) {
            if (emitterLife <= 0.0)
                return Result<Duration>::failure(Diagnostic::error(
                    DiagnosticCode::Unsupported, "Particle effect contains an unbounded looping emitter",
                    "emitters"));
            maximumLoopPeriod = std::max(maximumLoopPeriod, emitterLife);
            continue;
        }
        if (emitterLife < 0.0)
            return Result<Duration>::failure(Diagnostic::error(
                DiagnosticCode::Unsupported, "Particle effect contains an unbounded emitter", "emitters"));
        maximumNonLooping = std::max(maximumNonLooping, emitterLife + particleLife);
    }
    if (!hasEnabledLayer)
        return Result<Duration>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "Particle effect has no enabled emitters", "emitters"));
    double seconds = maximumNonLooping > 0.001 ? maximumNonLooping : maximumLoopPeriod;
    if (timeline_.duration > 0.f) seconds = std::max(seconds, double(timeline_.duration));
    auto duration = Duration::fromSeconds(std::max(0.01, seconds));
    if (!duration) return Result<Duration>::failure(duration.status());
    return Result<Duration>::success(duration.value());
}

Result<void> ParticleEffect::reloadFromText(const std::string& json) {
    auto parsed = tryFromText(json, sourcePath_);
    if (!parsed) return Result<void>::failure(parsed.status());
    std::unique_ptr<ParticleEffect> candidate(parsed.value());
    adoptParsed(std::move(*candidate), true);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> ParticleEffect::reloadFromFile(const std::string& path) {
    const std::string loadPath = path.empty() ? sourcePath_ : path;
    auto              parsed   = tryFromFile(loadPath);
    if (!parsed) return Result<void>::failure(parsed.status());
    std::unique_ptr<ParticleEffect> candidate(parsed.value());
    adoptParsed(std::move(*candidate), true);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

void ParticleEffect::adoptParsed(ParticleEffect&& other, bool preserveWorldState) {
    unregisterFromTimelineTicks();
    for (auto& layer : layers_) {
        if (layer.emitter) layer.emitter->release();
        layer.emitter = nullptr;
    }
    layers_       = std::move(other.layers_);
    other.layers_.clear();
    version_      = other.version_;
    sourcePath_   = std::move(other.sourcePath_);
    parameters_   = std::move(other.parameters_);
    eventRoutes_  = std::move(other.eventRoutes_);
    timeline_     = std::move(other.timeline_);
    runtimeCues_.clear();
    for (const auto& cue : timeline_.cues) runtimeCues_.push_back(RuntimeCue{cue, false});
    timelineSeconds_ = 0.0;
    timelinePlaying_ = false;
    timelinePaused_  = false;
    if (!preserveWorldState) {
        x_        = other.x_;
        y_        = other.y_;
        rotation_ = other.rotation_;
        scale_    = other.scale_;
        layer_    = other.layer_;
        visible_  = other.visible_;
    }
    syncTransform();
    syncLayer();
    setVisible(visible_);
}

Result<void> ParticleEffect::advanceTimeline(const SimulationStep& step) {
    if (!timelinePlaying_ || timelinePaused_)
        return Result<void>::success(Status::success(StatusCode::NoOp));
    if (!step.dt || !std::isfinite(step.dt.seconds()) || step.dt.seconds() < 0.0)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "timeline step duration must be finite and non-negative",
            "step.dt"));
    if (runtimeCues_.empty() && timeline_.duration <= 0.f)
        return Result<void>::success(Status::success(StatusCode::NoOp));

    const double previous = timelineSeconds_;
    double       next     = previous + step.dt.seconds();
    auto         fired    = fireDueCues(previous, next);
    if (!fired) return fired;

    if (timeline_.duration > 0.f && next + 1e-9 >= double(timeline_.duration)) {
        if (timeline_.looping) {
            const double overflow = next - double(timeline_.duration);
            rewindTimelineClock();
            timelinePlaying_ = true;
            timelinePaused_  = false;
            registerForTimelineTicks();
            auto looped = fireDueCues(-1e-9, std::max(0.0, overflow));
            if (!looped) return looped;
            timelineSeconds_ = std::max(0.0, overflow);
            return Result<void>::success(Status::success(StatusCode::Applied));
        }
        timelineSeconds_ = double(timeline_.duration);
        timelinePlaying_ = false;
        unregisterFromTimelineTicks();
        return Result<void>::success(Status::success(StatusCode::Applied));
    }

    timelineSeconds_ = next;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

void ParticleEffect::updateTimeline(float dt) {
    if (!std::isfinite(dt) || dt < 0.f) return;
    auto duration = Duration::fromSeconds(double(dt));
    if (!duration) {
        duration.ignore();
        return;
    }
    SimulationStep step;
    step.dt = duration.value();
    advanceTimeline(step).ignore();
}

void ParticleEffect::tickRegisteredTimelines(float dt) {
    auto& registry = timelineRegistry();
    for (std::size_t i = 0; i < registry.size(); ++i) {
        if (ParticleEffect* effect = registry[i]) effect->updateTimeline(dt);
    }
}

Result<void> ParticleEffect::advanceRegisteredTimelines(const SimulationStep& step) {
    bool changed = false;
    auto& registry = timelineRegistry();
    for (std::size_t i = 0; i < registry.size(); ++i) {
        ParticleEffect* effect = registry[i];
        if (!effect) continue;
        auto advanced = effect->advanceTimeline(step);
        if (!advanced) return advanced;
        if (advanced.status().code() == StatusCode::Applied) changed = true;
        advanced.ignore();
    }
    return Result<void>::success(
        Status::success(changed ? StatusCode::Applied : StatusCode::NoOp));
}

void ParticleEffect::syncTransform() {
    const float c = std::cos(rotation_);
    const float s = std::sin(rotation_);
    for (auto& layer : layers_) {
        const float ox = layer.offsetX * scale_;
        const float oy = layer.offsetY * scale_;
        layer.emitter->setPosition(x_ + ox * c - oy * s, y_ + ox * s + oy * c);
        layer.emitter->setDirection(layer.baseDirection + rotation_);
    }
}

void ParticleEffect::syncLayer() {
    for (auto& value : layers_) value.emitter->setLayer(value.baseLayer + layer_);
}

void ParticleEffect::wireEventRoutes() {
    for (auto& layer : layers_)
        if (layer.emitter) layer.emitter->clearSubEmitters();
    for (const auto& route : eventRoutes_) {
        auto* from = getEmitterByName(route.from);
        auto* to   = getEmitterByName(route.to);
        if (from && to) from->addSubEmitter(to, route.on, route.inheritVelocity);
    }
}

void ParticleEffect::rewindTimelineClock() {
    timelineSeconds_ = 0.0;
    runtimeCues_.clear();
    for (const auto& cue : timeline_.cues) runtimeCues_.push_back(RuntimeCue{cue, false});
}

void ParticleEffect::registerForTimelineTicks() {
    if (timelineRegistered_) return;
    if (runtimeCues_.empty() && timeline_.duration <= 0.f) return;
    timelineRegistry().push_back(this);
    timelineRegistered_ = true;
}

void ParticleEffect::unregisterFromTimelineTicks() {
    if (!timelineRegistered_) return;
    auto& registry = timelineRegistry();
    registry.erase(std::remove(registry.begin(), registry.end(), this), registry.end());
    timelineRegistered_ = false;
}

Result<void> ParticleEffect::fireDueCues(double previousSeconds, double currentSeconds) {
    bool changed = false;
    for (auto& runtime : runtimeCues_) {
        if (runtime.fired) continue;
        const double cueTime = double(runtime.cue.time);
        // Inclusive of currentSeconds so t=0 cues fire on start(previous=-eps, current=0).
        if (!(cueTime > previousSeconds && cueTime <= currentSeconds + 1e-12)) continue;
        auto applied = applyCue(runtime.cue);
        if (!applied) return applied;
        runtime.fired = true;
        changed       = true;
    }
    return Result<void>::success(Status::success(changed ? StatusCode::Applied : StatusCode::NoOp));
}

Result<void> ParticleEffect::applyCue(const ParticleEffectTimelineCue& cue) {
    if (cue.action == "setParameter") {
        setFloatParameter(cue.parameter, cue.value);
        return Result<void>::success(Status::success(StatusCode::Applied));
    }
    auto* emitter = getEmitterByName(cue.emitter);
    if (!emitter)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::NotFound, "timeline cue emitter is missing: " + cue.emitter, "emitter"));
    if (cue.action == "start") {
        emitter->start();
    } else if (cue.action == "stop") {
        emitter->stop();
    } else if (cue.action == "pause") {
        emitter->pause();
    } else if (cue.action == "reset") {
        emitter->reset();
    } else if (cue.action == "emit") {
        emitter->emit(cue.count);
    } else {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "unsupported timeline cue action: " + cue.action, "action"));
    }
    return Result<void>::success(Status::success(StatusCode::Applied));
}

void ParticleEffect::setPosition(float x, float y) {
    x_ = x;
    y_ = y;
    syncTransform();
}

void ParticleEffect::setRotation(float radians) {
    rotation_ = radians;
    syncTransform();
}

void ParticleEffect::setScale(float scale) {
    scale_ = std::max(0.f, scale);
    syncTransform();
}

void ParticleEffect::setLayer(int layer) {
    layer_ = layer;
    syncLayer();
}

void ParticleEffect::setVisible(bool visible) {
    visible_ = visible;
    for (auto& layer : layers_) layer.emitter->setVisible(visible && layer.enabled);
}

void ParticleEffect::start() {
    for (auto& layer : layers_)
        if (layer.enabled) layer.emitter->start();
    rewindTimelineClock();
    timelinePlaying_ = true;
    timelinePaused_  = false;
    registerForTimelineTicks();
    fireDueCues(-1e-9, 0.0).ignore();
}

void ParticleEffect::stop() {
    for (auto& layer : layers_) layer.emitter->stop();
    timelinePlaying_ = false;
    timelinePaused_  = false;
    unregisterFromTimelineTicks();
}

void ParticleEffect::pause() {
    for (auto& layer : layers_)
        if (layer.enabled) layer.emitter->pause();
    timelinePaused_ = true;
}

void ParticleEffect::reset() {
    for (auto& layer : layers_) layer.emitter->reset();
    rewindTimelineClock();
    timelinePlaying_ = false;
    timelinePaused_  = false;
    unregisterFromTimelineTicks();
}

bool ParticleEffect::emit(const std::string& emitterName, int count) {
    auto* emitter = getEmitterByName(emitterName);
    if (!emitter || count <= 0) return false;
    emitter->emit(count);
    return true;
}

void ParticleEffect::setFloatParameter(const std::string& name, float value) {
    if (name.empty()) return;
    parameters_[name] = value;
    for (auto& layer : layers_) layer.emitter->setFloatParameter(name, value);
}

float ParticleEffect::getFloatParameter(const std::string& name) const {
    auto parameter = parameters_.find(name);
    return parameter == parameters_.end() ? 0.f : parameter->second;
}

bool ParticleEffect::hasFloatParameter(const std::string& name) const {
    return parameters_.find(name) != parameters_.end();
}

}  // namespace eve::particles
